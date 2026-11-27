#!/usr/bin/env python3
"""Live camera preview over the ESP32-S3 USB serial cable.

The camera firmware writes JPEG frames with this binary framing:

    magic[4] = A5 5A 42 47
    uint32 LE jpeg_len
    uint32 LE seq
    jpeg bytes

This script reads the same USB port used by esptool, then serves a local page at
http://127.0.0.1:8080/ . It does not use the board's WiFi/SoftAP.

Usage:
    python backend/tools/camera_usb_viewer.py --serial COM10
    python backend/tools/camera_usb_viewer.py --serial COM10 --http-port 8080 --open
"""

from __future__ import annotations

import argparse
import json
import struct
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Optional

try:
    import serial
except ImportError as exc:  # pragma: no cover - operator-facing message
    raise SystemExit("缺少 pyserial，请先运行: pip install pyserial") from exc

MAGIC = b"\xa5\x5a\x42\x47"
MAX_JPEG = 2 * 1024 * 1024
BOUNDARY = b"frame"


class CameraStore:
    def __init__(self) -> None:
        self._lock = threading.Condition()
        self._jpeg: Optional[bytes] = None
        self._seq = 0
        self._frames = 0
        self._bytes = 0
        self._last_at = 0.0
        self._fps = 0.0
        self._fps_ts = 0.0
        self._fps_frames = 0

    def put(self, jpeg: bytes, seq: int) -> None:
        now = time.monotonic()
        with self._lock:
            self._jpeg = jpeg
            self._seq = seq
            self._frames += 1
            self._bytes += len(jpeg)
            self._last_at = now
            self._fps_frames += 1
            if self._fps_ts == 0.0:
                self._fps_ts = now
            elif now - self._fps_ts >= 1.0:
                self._fps = self._fps_frames / (now - self._fps_ts)
                self._fps_ts = now
                self._fps_frames = 0
            self._lock.notify_all()

    def wait(self, after_seq: int, timeout: float = 5.0) -> Optional[bytes]:
        with self._lock:
            self._lock.wait_for(
                lambda: self._jpeg is not None and self._seq > after_seq,
                timeout=timeout,
            )
            return self._jpeg

    def snapshot(self) -> Optional[bytes]:
        with self._lock:
            return self._jpeg

    def status(self, port: str, error: str = "") -> dict:
        with self._lock:
            age_ms = int((time.monotonic() - self._last_at) * 1000) if self._last_at else -1
            return {
                "serial": port,
                "error": error,
                "seq": self._seq,
                "frames": self._frames,
                "jpeg_bytes": len(self._jpeg) if self._jpeg else 0,
                "total_jpeg_bytes": self._bytes,
                "fps": round(self._fps, 2),
                "age_ms": age_ms,
            }


STORE = CameraStore()
SERIAL_ERROR = ""


def serial_reader(port: str, baud: int) -> None:
    global SERIAL_ERROR
    try:
        ser = serial.Serial(port, baud, timeout=0.2)
        ser.setDTR(False)
        ser.setRTS(False)
    except Exception as exc:  # noqa: BLE001 - show the actual OS error
        SERIAL_ERROR = str(exc)
        print(f"[camera] 打开 {port} 失败: {exc}")
        return

    print(f"[camera] 正在读取 {port} 的 USB JPEG 流")
    buf = bytearray()
    while True:
        try:
            data = ser.read(4096)
        except Exception as exc:  # noqa: BLE001
            SERIAL_ERROR = str(exc)
            print(f"[camera] 串口读取失败: {exc}")
            break
        if not data:
            continue
        buf.extend(data)

        while True:
            idx = buf.find(MAGIC)
            if idx < 0:
                if len(buf) > len(MAGIC):
                    del buf[: -len(MAGIC)]
                break
            if idx:
                del buf[:idx]
            if len(buf) < 12:
                break
            jpeg_len, seq = struct.unpack_from("<II", buf, 4)
            if jpeg_len == 0 or jpeg_len > MAX_JPEG:
                del buf[:4]
                continue
            if len(buf) < 12 + jpeg_len:
                break
            jpeg = bytes(buf[12 : 12 + jpeg_len])
            del buf[: 12 + jpeg_len]
            if jpeg.startswith(b"\xff\xd8"):
                STORE.put(jpeg, seq)

    ser.close()


PAGE = r"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BGA4E USB 摄像头</title>
<style>
  :root{color-scheme:dark}
  body{margin:0;background:#0e1116;color:#e6edf3;font-family:system-ui,-apple-system,"Microsoft YaHei",sans-serif}
  main{max-width:960px;margin:0 auto;padding:22px}
  h1{font-size:21px;margin:0 0 4px}
  .sub{color:#8b98a5;font-size:13px;line-height:1.7;margin:0 0 16px}
  .live{display:inline-flex;align-items:center;gap:7px;background:#13251b;border:1px solid #245c3a;color:#7ee787;border-radius:999px;padding:5px 10px;font-size:12px;font-weight:600}
  .dot{width:7px;height:7px;border-radius:50%;background:#3fb950;box-shadow:0 0 8px #3fb950}
  .frame{background:#000;border:1px solid #26303a;border-radius:12px;overflow:hidden;line-height:0}
  img{width:100%;height:auto;display:block;min-height:180px;object-fit:contain}
  .row{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-top:12px;font-size:13px;color:#8b98a5;flex-wrap:wrap}
  code{color:#79c0ff;background:#161b22;border:1px solid #26303a;border-radius:5px;padding:2px 5px}
  .err{color:#ff7b72}
</style>
</head>
<body><main>
  <h1>BGA4E · OV5640 实时画面</h1>
  <p class="sub">通过 USB 串口读取，不使用开发板热点。<br>
  图像由摄像头端编码为 JPEG，叠加识别框后经 <code>COM</code> 传到本页。</p>
  <div class="live"><span class="dot"></span><span id="live">等待画面</span></div>
  <div class="frame"><img id="stream" src="/stream" alt="camera live"></div>
  <div class="row">
    <span id="status">连接中…</span>
    <span id="fps"></span>
  </div>
</main>
<script>
async function poll(){
  try {
    const r = await fetch('/api/status?t=' + Date.now());
    const j = await r.json();
    const live = document.getElementById('live');
    const st = document.getElementById('status');
    if (j.error) {
      live.textContent = '串口未连接';
      st.className = 'err';
      st.textContent = j.error;
    } else if (j.age_ms >= 0 && j.age_ms < 1500) {
      live.textContent = 'LIVE';
      st.className = '';
      st.textContent = '已接收 ' + j.frames + ' 帧 · 最新 ' + j.jpeg_bytes + ' B · 延迟 ' + j.age_ms + ' ms';
    } else {
      live.textContent = '等待摄像头';
      st.className = '';
      st.textContent = '串口已打开，暂无新帧';
    }
    document.getElementById('fps').textContent = j.fps ? (j.fps + ' fps') : '';
  } catch (e) {
    document.getElementById('live').textContent = '页面连接中断';
    document.getElementById('status').textContent = String(e);
  }
}
setInterval(poll, 1000);
poll();
</script>
</body></html>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "BGA4ECamera/1.0"

    def log_message(self, fmt: str, *args) -> None:  # noqa: A003
        return

    def _send(self, status: int, body: bytes, ctype: str, cache: str = "no-store") -> None:
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0]
        if path == "/":
            self._send(200, PAGE.encode("utf-8"), "text/html; charset=utf-8")
        elif path == "/api/status":
            body = json.dumps(STORE.status(getattr(self.server, "serial_port", "")), ensure_ascii=False)
            self._send(200, body.encode("utf-8"), "application/json; charset=utf-8")
        elif path in ("/snapshot.jpg", "/latest.jpg"):
            jpeg = STORE.snapshot()
            if jpeg is None:
                self._send(503, b"no frame yet", "text/plain; charset=utf-8")
            else:
                self._send(200, jpeg, "image/jpeg")
        elif path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            last_seq = 0
            while True:
                jpeg = STORE.wait(last_seq, timeout=10.0)
                with STORE._lock:  # read sequence under the same lock
                    last_seq = STORE._seq
                if jpeg is None:
                    continue
                try:
                    self.wfile.write(b"--" + BOUNDARY + b"\r\n")
                    self.wfile.write(b"Content-Type: image/jpeg\r\n")
                    self.wfile.write(f"Content-Length: {len(jpeg)}\r\n\r\n".encode("ascii"))
                    self.wfile.write(jpeg)
                    self.wfile.write(b"\r\n")
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError, OSError):
                    break
                time.sleep(0.06)
        else:
            self._send(404, b"not found", "text/plain; charset=utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="BGA4E USB camera live viewer")
    parser.add_argument("--serial", default="COM10", help="camera USB serial port (default: COM10)")
    parser.add_argument("--baud", type=int, default=115200, help="serial baud (USB CDC ignores it)")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--open", action="store_true", help="open the page in the default browser")
    args = parser.parse_args()

    t = threading.Thread(target=serial_reader, args=(args.serial, args.baud), daemon=True)
    t.start()

    server = ThreadingHTTPServer(("127.0.0.1", args.http_port), Handler)
    server.serial_port = args.serial
    url = f"http://127.0.0.1:{args.http_port}/"
    print(f"[camera] 网页已启动: {url}")
    print("[camera] 浏览器打开上面的地址即可；按 Ctrl+C 停止")
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[camera] 停止")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
