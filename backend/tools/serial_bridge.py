"""ESP32-S3 串口 -> 后端桥接。

固件在每帧识别后输出：
  I (12345) CARD_RESULT: {"mode":"mahjong","tile":"3W","conf":0.923}
本脚本读取串口，调用后端：
  POST /api/games/{game_id}/recognitions

用法：
  python tools/serial_bridge.py --port COM5 --game <game_id> --player 0
  python tools/serial_bridge.py --port COM5 --game <game_id> --dry-run

注意：同一张牌会连续多帧输出，脚本默认 1.5 秒内相同结果只上报一次。
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request

try:
    import serial
except ImportError:
    print("需要 pyserial：pip install pyserial", file=sys.stderr)
    raise

RESULT_RE = re.compile(r"CARD_RESULT:?\s*(\{.*\})")


def post_json(url: str, payload: dict) -> dict:
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="COM5 / /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--base-url", default="http://127.0.0.1:8000")
    ap.add_argument("--game", required=True, help="牌局 ID（网页左侧可见）")
    ap.add_argument("--player", type=int, default=0, help="0-3，当前玩家")
    ap.add_argument("--min-conf", type=float, default=0.45)
    ap.add_argument("--interval", type=float, default=1.5, help="相同结果的最小上报间隔秒")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)
    print(f"串口 {args.port} @ {args.baud}，牌局 {args.game}，玩家 {args.player}")
    last_key = None
    last_ts = 0.0
    try:
        while True:
            raw = ser.readline().decode("utf-8", errors="replace").strip()
            if not raw:
                continue
            m = RESULT_RE.search(raw)
            if not m:
                continue
            try:
                result = json.loads(m.group(1))
            except json.JSONDecodeError:
                continue
            if result.get("mode") != "mahjong":
                continue
            tile = result.get("tile")
            conf = float(result.get("conf", 0))
            if not tile or conf < args.min_conf:
                continue
            key = (tile, round(conf, 2))
            now = time.time()
            if key == last_key and now - last_ts < args.interval:
                continue
            last_key, last_ts = key, now

            payload = {
                "player": args.player,
                "tile": tile,
                "confidence": conf,
                "top3": [{"tile": tile, "confidence": conf}],
                "source": "esp32-serial",
            }
            if args.dry_run:
                print("DRY", json.dumps(payload, ensure_ascii=False))
                continue
            try:
                resp = post_json(f"{args.base_url}/api/games/{args.game}/recognitions", payload)
                advice = resp.get("advice") or {}
                print(f"{tile} {conf:.2f} -> {advice.get('action')} "
                      f"{advice.get('tile_name') or ''} {advice.get('reason', '')[:40]}")
            except urllib.error.HTTPError as exc:
                print(f"HTTP {exc.code}: {exc.read().decode('utf-8', errors='replace')[:200]}")
            except Exception as exc:  # noqa: BLE001
                print(f"上报失败: {exc}")
    except KeyboardInterrupt:
        print("\n退出")
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
