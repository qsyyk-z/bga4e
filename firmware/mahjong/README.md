# mahjong 部署包

## 文件

| 文件 | 说明 |
| --- | --- |
| `classifier.onnx` | FP32 分类器，输入 `[1,3,96,96]` |
| `classifier_int8.onnx` | int8 分类器（PC 验证，ONNX Runtime QOperator） |
| `classes.json` | 类别顺序、mean/std、输入尺寸 |
| `metrics.json` | 精度与 int8 对比 |
| `model_layers.md` | 逐层输入输出形状，写 ESP-DL C++ 模型时对照 |
| `calib_list.txt` | 推荐校准图（300 张） |
| `detector_320.onnx` | 可选：2 类检测器 320×320 |
| `espdl_coeff/card_cls.cpp/.hpp` | **ESP-DL int8 量化系数**（官方 Calibrator 导出，ESP32-S3） |
| `card_classifier_model.hpp` | **ESP-DL C++ 模型类**，层结构与 CardNet 一一对应 |

## 预处理（必须与训练一致）

1. 由检测框外扩 **10%**，补成正方形（灰底 114,114,114）；
2. 缩放到 **96×96**；
3. `x = (pixel / 255 - 0.5) / 0.5`，即 `[-1, 1]`；
4. NCHW（ESP-DL 内部为 HWC，接入时按库的 Tensor 约定转换）。

## 类别

1条, 1万, 1筒, 梅, 春, 2条, 2万, 2筒, 兰, 夏, 3条, 3万, 3筒, 菊, 秋, 4条, 4万, 4筒, 竹, 冬, 5条, 5万, 5筒, 6条, 6万, 6筒, 7条, 7万, 7筒, 8条, 8万, 8筒, 9条, 9万, 9筒, 东, 发, 北, 中, 南, 白, 西

## 输出

`logits`，长度 42；取 argmax，置信度 < 0.5 建议显示 `?`。

## ESP-DL 端侧用法

```cpp
#include "card_classifier_model.hpp"

// 1) 96x96x3 RGB888 -> int8，输入指数 -7
Tensor<int8_t> input;
input.set_element((int8_t *)input_buf)   // 96*96*3
     .set_exponent(-7)
     .set_shape({96, 96, 3})             // HWC
     .set_auto_free(false);

// 2) 前向
CardClassifierMahjong model;
model.forward(input);

// 3) 取 42 个 logits（指数 -4），argmax
int8_t *score = model.fc.get_output().get_element_ptr();
```

输入量化：

```c
float v = (pixel / 255.0f - 0.5f) / 0.5f;      // [-1, 1]
input_buf[i] = (int8_t)DL_CLIP(v * (1 << 7), -128, 127);
```

## 量化

本目录的 `espdl_coeff/` 已由 `02_Face_recognition_project` 自带的 ESP-DL `Calibrator`
（int8, per-channel, entropy, esp32s3）实际导出，可直接加入 `main/CMakeLists.txt` 编译。
量化时输入指数 -7，各层输出指数见导出日志；`card_classifier_model.hpp` 已按这些指数写好。
