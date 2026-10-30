# 端侧部署说明（ESP32-S3 + ESP-DL）

本目录不重复放 `02_Face_recognition_project` 的 BSP / 摄像头 / LCD 代码，而是说明如何把本项目训练出的模型接到那套工程里。

---

## 1. 推荐架构

```
OV2640/OV3660 摄像头 (RGB565)
        │
        ▼
   ColorDetector / 轻量检测  →  牌的矩形区域
        │
        ▼
   dl_image resize/crop → 96×96 RGB888
        │
        ▼
   CardNet（ESP-DL Conv/DW/FC）→ 42/52 类 logits
        │
        ▼
   fb_gfx 画框 + 牌名
```

- **定位**：优先用 ESP-DL 自带的 `ColorDetector`（HSV 分割 + 连通域），按面积/长宽比过滤；这比在 S3 上跑 YOLOv8n 更稳。
- **识别**：用本项目的 `CardNet`，输入 96×96，int8 约 0.3 MB。
- **显示**：复用 `components/modules/ai/who_ai_utils.cpp` 与 `components/fb_gfx/fb_gfx.c`。

---

## 2. 与 02 工程的最小改动

1. 新建 `main/APP/esp_card_recognition.cpp`，复制 `esp_face_recognition.cpp` 的任务框架：
   - `esp_camera_process_handler` 不变；
   - `esp_ai_process_handler` 里把 `HumanFaceDetectMSR01` / `FaceRecognition112V1S16` 换成「牌定位 + CardNet」；
   - 按键状态机、队列、互斥锁原样保留。
2. `main/CMakeLists.txt` 增加 `model/` 与 `espdl_coeff/` 到 include 路径，例如：
   ```cmake
   idf_component_register(SRCS "APP/esp_card_recognition.cpp" "model/card_cls.cpp"
                          INCLUDE_DIRS "." "APP" "model" "model/espdl_coeff")
   ```
3. `partitions-16MiB.csv` 不变；模型系数以 C 数组形式链接进固件。
4. 麻将与扑克两个 `card_cls.hpp` 使用相同的 `card_cls` 命名空间，**一个固件里只链接其中一个**；若要同时支持两种牌，把其中一个 namespace 改名。

---

## 3. 分类器输入预处理

训练时的预处理必须与端侧一致：

```c
// 1) 从 camera_fb_t 的 RGB565 转 RGB888
// 2) 用 dl_image 的 resize 缩放到 96x96
// 3) 归一化: x = (pixel / 255.0 - 0.5) / 0.5  → [-1, 1]
// 4) 按 ESP-DL 的量化指数转 int8/int16
float v = (pixel / 255.0f - 0.5f) / 0.5f;
model_input[i] = (int8_t)DL_CLIP(v * (1 << -input_exponent), -128, 127);
```

`models/classifier_*/classes.json` 里有 `input_size`、`mean`、`std` 与类别顺序。

---

## 4. ESP-DL 量化与系数导出

本项目已经实际跑通 02 自带的 ESP-DL 量化工具，产物在：

```
firmware/mahjong/espdl_coeff/card_cls.cpp / card_cls.hpp   # int8 系数 + 量化指数
firmware/mahjong/card_classifier_model.hpp                 # ESP-DL C++ 模型类
firmware/poker/espdl_coeff/...
firmware/poker/card_classifier_model.hpp
```

流程（`scripts/21_quantize_espdl.py`，必须用 Python 3.7）：

1. `optimizer.py` 做 BN 折叠、动态 batch；
2. 200 张 `datasets/cls/<domain>/valid` 校准图，预处理与训练一致；
3. `Calibrator('int8', 'per-channel', 'entropy')` 生成量化表；
4. `export_coefficient_to_cpp(..., 'esp32s3', ...)` 输出 `card_cls.cpp/.hpp`；
5. `card_classifier_model.hpp` 按导出的逐层指数，用 `Conv2D` / `DepthwiseConv2D` / `GlobalAveragePool2D` / `FullyConnected` 搭出与 `scripts/cardnet.py` 完全一致的层序。

Windows 上官方 `.pyd` 只支持旧 Python，已用 pyenv 安装 3.7.9 并建 `.venv37`：

```powershell
pyenv install 3.7.9
pyenv shell 3.7.9
python -m venv .venv37
.\.venv37\Scripts\pip install numpy==1.21.6 onnx==1.9.0 onnxruntime==1.7.0 onnxoptimizer==0.2.6 protobuf==3.20.3 Pillow==9.5.0
.\.venv37\Scripts\python.exe scripts\21_quantize_espdl.py --domain mahjong
```

> 未在真实 ESP32-S3 板子上编译/烧录验证；C++ 模型按 ESP-DL 1.x API 编写，层指数取自实际量化日志。

---

## 5. 资源估算

| 模型 | 输入 | 参数量 | int8 大小 | 备注 |
| --- | --- | ---: | ---: | --- |
| CardNet（麻将） | 96×96×3 | 0.31 M | ~0.3 MB | 单帧一次 |
| CardNet（扑克） | 96×96×3 | 0.31 M | ~0.3 MB | 与麻将二选一 |
| YOLOv8n | 320×320×3 | 3.0 M | ~3 MB | 端侧跑检测需额外适配 |

ESP32-S3 + 8 MB PSRAM：只跑分类器非常宽裕；若同时跑检测器，需要评估帧率与内存。

---

## 6. 调试建议

1. 先把 `datasets/det/test` 里的图片通过 `scripts/17_predict_image.py` 跑一遍，确认 PC 端结果；
2. 端侧先只做「固定裁剪 + 分类」，验证预处理一致；
3. 再接 `ColorDetector` 做自动定位；
4. 最后调阈值（分类置信度 < 0.5 显示 `?`）。
