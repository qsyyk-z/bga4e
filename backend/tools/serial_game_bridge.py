"""方案 D：PC 后端 -> USB 串口 -> 4.3C LVGL，完全绕开 WiFi/HTTP。

用法：
  cd backend
  .\.venv\Scripts\python.exe tools\serial_game_bridge.py `
      --port COM9 --player 0 --interval 2

说明：
  - COM9 是 4.3C 的 ESP32-S3 native USB Serial/JTAG 口；
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
    print("TX", payload.get("type"), payload.get("action", ""),
          payload.get("wall_remaining", ""), flush=True)
    ser.write(line.encode("utf-8"))
    ser.flush()


def probe(ser: serial.Serial) -> None:
    """Verify that this is the firmware protocol UART, not a native USB port."""
    ser.reset_input_buffer()
    ser.write(b'BGA4E {"type":"ping"}\n')
    ser.flush()
    deadline = time.monotonic() + 1.5
    while time.monotonic() < deadline:
        raw = ser.readline()
        if raw.decode("utf-8", errors="replace").strip() == 'BGA4E {"type":"pong"}':
            print("4.3C BGA4E UART 握手成功", flush=True)
            return
    raise RuntimeError(
        "未收到 4.3C 握手响应；请确认使用 CH343 的 COM 口而不是 native USB 口，"
        "并确认烧录了包含 SerialTask 的固件"
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="4.3C USB 串口，例如 COM9")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--base-url", default="http://127.0.0.1:8000")
    ap.add_argument("--player", type=int, default=0)
    ap.add_argument("--interval", type=float, default=2.0, help="牌局刷新间隔秒")
    ap.add_argument("--advice-interval", type=float, default=6.0,
                    help="自动建议间隔秒；0 表示只响应按钮请求")
    ap.add_argument("--echo", action="store_true", help="打印 4.3C 的普通日志")
    args = ap.parse_args()

    base = args.base_url.rstrip("/")
    ser = serial.Serial(
        args.port, args.baud, timeout=0.2, write_timeout=5,
        rtscts=False, dsrdtr=False, xonxoff=False,
    )
    print(f"串口 {args.port} @ {args.baud}，后端 {base}，玩家 {args.player}", flush=True)

    last_summary = 0.0
    last_advice = 0.0
    try:
        try:
            probe(ser)
        except Exception as exc:
            print(f"串口握手失败: {exc}", flush=True)
            return 2
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
