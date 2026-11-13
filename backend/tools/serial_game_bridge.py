"""方案 D：PC 后端 -> USB 串口 -> 4.3C LVGL，完全绕开 WiFi/HTTP。

用法：
  cd backend
  .\.venv\Scripts\python.exe tools\serial_game_bridge.py `
      --port COM8 --player 0 --interval 2

说明：
  - COM8 是 4.3C 的 CH343 USB-UART 口（不是 native USB 口）；
  - 运行前关闭 idf.py monitor / 串口助手，否则串口被占用；
  - 固件里已经带 SerialTask，收到 "BGA4E {...}" 行后自动切换串口模式。
"""
from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.request

try:
    import serial
except ImportError:
    print("需要 pyserial：.venv\\Scripts\\python.exe -m pip install pyserial")
    raise


def get_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=4) as resp:
        return json.loads(resp.read().decode("utf-8"))


def post_json(url: str, payload: dict) -> dict:
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(
        url, data=data, method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=4) as resp:
        return json.loads(resp.read().decode("utf-8"))


def summary_payload(base: str, player: int) -> dict:
    s = get_json(f"{base}/api/current/summary?player={player}")
    return {
        "type": "summary",
        "current_name": s.get("current_name"),
        "current_player": s.get("current_player"),
        "wall_remaining": s.get("wall_remaining"),
        "turn": s.get("turn"),
        "last_discard": s.get("last_discard"),
        "my_hand_names": s.get("my_hand_names", []),
        "players": [
            {
                "name": p.get("name"),
                "discard_names": p.get("discard_names", []),
            }
            for p in s.get("players", [])
        ],
    }


def advice_payload(base: str, player: int) -> dict:
    a = post_json(f"{base}/api/current/advice",
                  {"player": player, "use_llm": False})
    return {
        "type": "advice",
        "action": a.get("action"),
        "tile_name": a.get("tile_name"),
        "reason": a.get("reason"),
        "shanten": a.get("shanten"),
        "ukeire": a.get("ukeire"),
        "needs_hand": a.get("needs_hand", False),
    }


def send(ser: serial.Serial, payload: dict) -> None:
    line = "BGA4E " + json.dumps(payload, ensure_ascii=False) + "\n"
    ser.write(line.encode("utf-8"))
    ser.flush()
    print("TX", payload.get("type"), payload.get("action", ""), payload.get("wall_remaining", ""))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="CH343 COM，例如 COM8")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--base-url", default="http://127.0.0.1:8000")
    ap.add_argument("--player", type=int, default=0)
    ap.add_argument("--interval", type=float, default=2.0, help="牌局刷新间隔秒")
    ap.add_argument("--advice-interval", type=float, default=6.0,
                    help="自动建议间隔秒；0 表示只响应按钮请求")
    ap.add_argument("--echo", action="store_true", help="打印 4.3C 的普通日志")
    args = ap.parse_args()

    base = args.base_url.rstrip("/")
    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    print(f"串口 {args.port} @ {args.baud}，后端 {base}，玩家 {args.player}")

    last_summary = 0.0
    last_advice = 0.0
    try:
        while True:
            now = time.time()
            if now - last_summary >= args.interval:
                try:
                    send(ser, summary_payload(base, args.player))
                    last_summary = now
                except Exception as exc:  # noqa: BLE001
                    print("后端 summary 失败:", exc)

            if args.advice_interval > 0 and now - last_advice >= args.advice_interval:
                try:
                    send(ser, advice_payload(base, args.player))
                    last_advice = now
                except Exception as exc:  # noqa: BLE001
                    print("后端 advice 失败:", exc)

            # 读取 4.3C 的日志和 BGA4E_REQ 请求
            while ser.in_waiting:
                raw = ser.readline()
                if not raw:
                    break
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if line.startswith("BGA4E_REQ"):
                    print("RX 请求建议")
                    try:
                        send(ser, advice_payload(base, args.player))
                        last_advice = time.time()
                    except Exception as exc:  # noqa: BLE001
                        print("后端 advice 失败:", exc)
                elif args.echo:
                    print("LOG", line)

            time.sleep(0.05)
    except KeyboardInterrupt:
        print("\n退出")
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
