# 摄像头网页实时预览

摄像头固件内置 SoftAP + HTTP MJPEG 服务，不需要串口日志，也不依赖电脑上的后端。

## 使用步骤

1. 烧录摄像头固件（COM10）：

   ```powershell
   cd D:\M08\bga4e\firmware_app
   .\build_camera.ps1 -p COM10 flash
   ```

2. **烧录完成后必须退出下载模式**：松开 BOOT，按一下 RESET；或者拔掉 USB 重新插上（不要按住 BOOT）。
   如果 COM10 仍打印 `waiting for download`，说明 GPIO0/BOOT 还是低电平，应用不会启动，网页也不会出现。

3. 电脑或手机连接 WiFi：

   - SSID：`BGA4E-CAM`
   - 密码：`bga4e1234`

4. 浏览器打开：

   <http://192.168.4.1/>

## 页面内容

- `/`：实时 MJPEG 画面，带识别框和置信度文字。
- `/stream`：原始 MJPEG 流，可单独嵌入。
- `/api/latest`：最近一帧识别 JSON，例如
  `{"mode":"mahjong","name":"五万","conf":0.93,"box":[x,y,w,h]}` 或
  `{"mode":"poker","rank":"K","suit":"H","conf":0.93,"box":[x,y,w,h]}`。
- `/api/status`：WiFi、摄像头就绪状态、客户端数、编码帧率和帧数。

## 说明

- 只有网页打开 `/stream` 时才会把 RGB565 帧转成 JPEG，避免影响 LCD 本地预览的帧率。
- 最多同时支持 2 个 `/stream` 客户端。
- 摄像头画面是 QVGA（320×240）RGB565，网页端由 `frame2jpg()` 转 JPEG。
- 这个页面和 4.3C 显示板是两套独立固件；4.3C 的 `SD读牌` 仍通过 TF 卡上的 `/bga4e/cards.json` 读取。
- 若页面能打开但没有画面，先看 `/api/status` 的 `camera` 是否为 1；为 0 表示 OV5640 初始化还没成功，需要继续检查摄像头排线和 I2C。
