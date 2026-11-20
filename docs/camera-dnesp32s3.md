# DNESP32S3 / OV5640 camera node

This board is the ALIENTEK **DNESP32S3** with an **OV5640** DVP camera, not the
generic WROVER-KIT wiring that the old BSP assumed.  The camera firmware lives
in `firmware_app/`; the display firmware lives in `xiaozhi/`.

## Wiring used by the firmware

| Signal | ESP32-S3 pin |
|---|---|
| Camera SCCB SDA | GPIO39 |
| Camera SCCB SCL | GPIO38 |
| Camera VSYNC | GPIO47 |
| Camera HREF | GPIO48 |
| Camera PCLK | GPIO45 |
| Camera D0..D7 | GPIO4,5,6,7,15,16,17,18 |
| Camera XCLK | not connected (OV5640 module clock) |
| Camera PWDN | XL9555 P0.4 |
| Camera RESET | XL9555 P0.5 |
| XL9555 SDA / SCL | GPIO41 / GPIO42 |
| LCD MOSI / CLK / MISO | GPIO11 / GPIO12 / GPIO13 |
| LCD DC / CS | GPIO40 / GPIO21 |

The previous BSP used GPIO47 as LCD MOSI, which is camera VSYNC on this board;
it also used the wrong XL9555 and camera pins.  That is why the camera probe
returned `ESP_ERR_NOT_FOUND` and the display init could disturb the camera.

## Build and flash

From a normal PowerShell window:

```powershell
cd D:\M08\bga4e\firmware_app
.\build_camera.ps1 build
.\build_camera.ps1 -p COM10 flash
```

`COM10` is the camera board's native USB Serial/JTAG port.  If `idf.py` reports
`Write timeout`, the board is still running the application instead of the ROM
download loader.  Enter download mode manually:

1. Unplug the camera board's USB cable.
2. Hold **BOOT** (GPIO0) down.
3. Plug the USB cable back in while still holding BOOT.
4. Release BOOT after Windows shows `COM10`.

Then run the flash command again.  Holding BOOT only while tapping RST is less
reliable on this USB-Serial/JTAG port.

## Serial protocol

Each recognised card is one line:

```json
{"mode":"mahjong","name":"五万","conf":0.93,"box":[x,y,w,h]}
{"mode":"poker","rank":"K","suit":"H","conf":0.93,"box":[x,y,w,h]}
```

The PC bridge reads those lines and writes the per-game JSON consumed by the
4.3C display:

```powershell
python backend\tools\camera_json_bridge.py --port COM10 --out data\camera
```

The bridge writes `game_*.jsonl` and a `latest.json` snapshot.  Copy the chosen
snapshot to the 4.3C TF card as `/bga4e/cards.json`; the display's `SD读牌`
button loads it.  A live camera-to-display transport is not implemented: the
camera board has no SD slot and the two boards are separate USB devices.
