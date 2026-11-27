# 摄像头 USB 实时预览

当前摄像头固件**不启动开发板热点**。OV5640 的 JPEG 画面直接走 USB 串口（和烧录同一个 COM 口），由电脑上的小网页显示。

## 固件

```powershell
cd D:\M08\bga4e\firmware_app
.\build_camera.ps1 -p COM10 flash
```

烧录后松开 BOOT、按一下 RESET（或拔插 USB，不要按住 BOOT）。正常启动时 COM10 会看到：

```text
Detected OV5640 camera
Camera initialized successfully
```

然后固件不再打印识别日志，而是发送二进制 JPEG 帧：

```text
magic[4] = A5 5A 42 47
uint32 LE jpeg_len
uint32 LE seq
jpeg bytes
```

## 电脑端网页

```powershell
cd D:\M08\bga4e
.\firmware_app\run_camera_viewer.ps1
```

或手动运行：

```powershell
python backend\tools\camera_usb_viewer.py --serial COM10 --http-port 8080
```

浏览器打开 <http://127.0.0.1:8080/>。

页面显示：

- 摄像头实时 MJPEG 画面（含识别框、模式和置信度叠加）
- 已接收帧数、最新 JPEG 大小、延迟和约帧率
- `/snapshot.jpg`：当前帧
- `/api/status`：串口/帧状态 JSON

## 说明

- 只使用 USB 线，不连接、也不依赖 `BGA4E-CAM` 热点。
- 打开网页会占用 COM10；需要重新烧录时先关闭网页。
- 画面约 2 fps，瓶颈是端侧 AI 识别，不是 USB；用于取景和对牌已经够用。
- 如果页面显示“串口未连接”，检查 COM10 是否被串口监视器占用，以及驱动是否正常。
