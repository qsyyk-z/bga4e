# ESP32-S3 固件工程（bga4e）

这是一个**可直接编译烧录**的 ESP-IDF 工程：摄像头采集 → 中央裁剪 → ESP-DL int8 分类 → LCD 显示。
基于 `02_Face_recognition_project` 的 BSP、摄像头、LCD、任务框架修改而来。

---

## 1. 硬件假设

与 02 工程同一套板子：

- ESP32-S3，16 MB Flash，8 MB PSRAM
- OV2640 / OV3660 摄像头，QVGA 320×240，RGB565
- 320×240 SPI LCD
- BOOT 键（GPIO0）

如果你的板子引脚不同，修改：

- `components/BSP/CAMERA/camera.c` 的 `camera_config_t`
- `components/BSP/LCD/lcd.c`、`components/BSP/XL9555/xl9555.c`

---

## 2. 编译与烧录

```bash
cd firmware_app

# 首次
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录 + 串口监视（把 COMx 换成你的端口）
idf.py -p COMx flash monitor
```

要求 ESP-IDF **v5.5.x**（与 02 工程一致）。模型系数已经以 C 数组形式放在：

```
main/model/card_cls_mahjong.cpp   # 麻将 42 类，int8
main/model/card_cls_poker.cpp     # 扑克 52 类，int8
```

分区表 `partitions-16MiB.csv` 给 app 15 MB，足够放两个模型。

---

## 3. 使用方法

1. 上电后 LCD 显示 `CARD RECOGNITION`；
2. 把**一张牌放在画面中央**（建议距离 10–20 cm，牌占画面 50% 以上）；
3. 屏幕左上角显示识别结果与置信度；
4. **短按 BOOT**：在麻将 / 扑克模式之间切换；
5. **长按 BOOT**：显示/隐藏置信度。

> 当前固件是“单牌识别”：每次识别画面中央的一张牌。这样最稳，也避免在 S3 上跑检测网络。
> 多牌检测可以后续接 `ColorDetector` 或把 YOLOv8n 检测头部署到 ESP-DL。

---

## 4. 显示标签（ASCII）

LCD 字体只支持 ASCII，因此用短代码显示：

### 麻将 42 类

| 代码 | 牌 | 代码 | 牌 | 代码 | 牌 |
| --- | --- | --- | --- | --- | --- |
| 1B/2B/…/9B | 1–9 条 | 1W/2W/…/9W | 1–9 万 | 1P/2P/…/9P | 1–9 筒 |
| PL | 梅 | SP | 春 | OR | 兰 |
| SU | 夏 | CH | 菊 | AU | 秋 |
| BA | 竹 | WI | 冬 | EA | 东 |
| GR | 发 | NO | 北 | CE | 中 |
| SO | 南 | WH | 白 | WE | 西 |

### 扑克 52 类

`2D`–`10D`、`JD`、`QD`、`KD`、`AD` 分别表示方块 2–10、J、Q、K、A；`H` 红桃、`S` 黑桃、`C` 梅花同理。
例如 `10H` = 红桃 10，`AS` = 黑桃 A。

---

## 5. 预处理（必须与训练一致）

- 摄像头输出 RGB565，中央取 `min(w,h) * 0.8` 的正方形；
- 最近邻缩放到 **96×96**；
- 转成 RGB（ESP-DL 的 `convert_pixel_rgb565_to_rgb888` 输出是 B,G,R，这里已交换成 R,G,B）；
- `x = (pixel/255 - 0.5) / 0.5`，再乘 `2^7` 量化成 int8（输入指数 **-7**）；
- 分类器输出 42/52 个 logits，指数 **-4**，softmax 得到置信度。

对应代码：`main/APP/esp_card_recognition.cpp` 的 `card_prepare_input()`。

---

## 6. 模型来源

| 模型 | 训练代码 | ESP-DL 系数 |
| --- | --- | --- |
| 麻将 42 类 | `../scripts/cardnet.py` + `13_train_classifier.py` | `card_cls_mahjong.cpp` |
| 扑克 52 类 | 同上 | `card_cls_poker.cpp` |

系数由 `02_Face_recognition_project/components/esp-dl/tools/quantization_tool` 的官方 Calibrator 导出（int8 per-channel entropy, esp32s3），输入指数 -7，输出指数 -4。

---

## 7. 说明

- 本工程按 ESP-IDF 5.5 + ESP-DL 1.x API 编写，**尚未在真实板子上烧录验证**；首次上板若遇到摄像头/LCD 引脚或模型内存问题，请先对照 02 工程的 `sdkconfig` 与 BSP 配置。
- 如果只需要训练/评估，不需要本目录；训练代码在 `../scripts/`，模型在 `../models/`。
