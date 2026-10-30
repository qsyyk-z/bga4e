# CardNet 逐层形状

输入: `[1, 3, 96, 96]`（NCHW）

| # | layer | type | in (C,H,W) | out (C,H,W) | params |
| ---: | --- | --- | --- | --- | ---: |
| 1 | stem.0 | Conv2d | (3, 96, 96) | (32, 48, 48) | 864 |
| 2 | stem.1 | BatchNorm2d | (32, 48, 48) | (32, 48, 48) | 64 |
| 3 | stem.2 | ReLU | (32, 48, 48) | (32, 48, 48) | 0 |
| 4 | features.0.dw.0 | Conv2d | (32, 48, 48) | (32, 24, 24) | 288 |
| 5 | features.0.dw.1 | BatchNorm2d | (32, 24, 24) | (32, 24, 24) | 64 |
| 6 | features.0.dw.2 | ReLU | (32, 24, 24) | (32, 24, 24) | 0 |
| 7 | features.0.pw.0 | Conv2d | (32, 24, 24) | (64, 24, 24) | 2048 |
| 8 | features.0.pw.1 | BatchNorm2d | (64, 24, 24) | (64, 24, 24) | 128 |
| 9 | features.0.pw.2 | ReLU | (64, 24, 24) | (64, 24, 24) | 0 |
| 10 | features.1.dw.0 | Conv2d | (64, 24, 24) | (64, 24, 24) | 576 |
| 11 | features.1.dw.1 | BatchNorm2d | (64, 24, 24) | (64, 24, 24) | 128 |
| 12 | features.1.dw.2 | ReLU | (64, 24, 24) | (64, 24, 24) | 0 |
| 13 | features.1.pw.0 | Conv2d | (64, 24, 24) | (64, 24, 24) | 4096 |
| 14 | features.1.pw.1 | BatchNorm2d | (64, 24, 24) | (64, 24, 24) | 128 |
| 15 | features.1.pw.2 | ReLU | (64, 24, 24) | (64, 24, 24) | 0 |
| 16 | features.2.dw.0 | Conv2d | (64, 24, 24) | (64, 12, 12) | 576 |
| 17 | features.2.dw.1 | BatchNorm2d | (64, 12, 12) | (64, 12, 12) | 128 |
| 18 | features.2.dw.2 | ReLU | (64, 12, 12) | (64, 12, 12) | 0 |
| 19 | features.2.pw.0 | Conv2d | (64, 12, 12) | (96, 12, 12) | 6144 |
| 20 | features.2.pw.1 | BatchNorm2d | (96, 12, 12) | (96, 12, 12) | 192 |
| 21 | features.2.pw.2 | ReLU | (96, 12, 12) | (96, 12, 12) | 0 |
| 22 | features.3.dw.0 | Conv2d | (96, 12, 12) | (96, 12, 12) | 864 |
| 23 | features.3.dw.1 | BatchNorm2d | (96, 12, 12) | (96, 12, 12) | 192 |
| 24 | features.3.dw.2 | ReLU | (96, 12, 12) | (96, 12, 12) | 0 |
| 25 | features.3.pw.0 | Conv2d | (96, 12, 12) | (96, 12, 12) | 9216 |
| 26 | features.3.pw.1 | BatchNorm2d | (96, 12, 12) | (96, 12, 12) | 192 |
| 27 | features.3.pw.2 | ReLU | (96, 12, 12) | (96, 12, 12) | 0 |
| 28 | features.4.dw.0 | Conv2d | (96, 12, 12) | (96, 6, 6) | 864 |
| 29 | features.4.dw.1 | BatchNorm2d | (96, 6, 6) | (96, 6, 6) | 192 |
| 30 | features.4.dw.2 | ReLU | (96, 6, 6) | (96, 6, 6) | 0 |
| 31 | features.4.pw.0 | Conv2d | (96, 6, 6) | (128, 6, 6) | 12288 |
| 32 | features.4.pw.1 | BatchNorm2d | (128, 6, 6) | (128, 6, 6) | 256 |
| 33 | features.4.pw.2 | ReLU | (128, 6, 6) | (128, 6, 6) | 0 |
| 34 | features.5.dw.0 | Conv2d | (128, 6, 6) | (128, 6, 6) | 1152 |
| 35 | features.5.dw.1 | BatchNorm2d | (128, 6, 6) | (128, 6, 6) | 256 |
| 36 | features.5.dw.2 | ReLU | (128, 6, 6) | (128, 6, 6) | 0 |
| 37 | features.5.pw.0 | Conv2d | (128, 6, 6) | (128, 6, 6) | 16384 |
| 38 | features.5.pw.1 | BatchNorm2d | (128, 6, 6) | (128, 6, 6) | 256 |
| 39 | features.5.pw.2 | ReLU | (128, 6, 6) | (128, 6, 6) | 0 |
| 40 | features.6.dw.0 | Conv2d | (128, 6, 6) | (128, 3, 3) | 1152 |
| 41 | features.6.dw.1 | BatchNorm2d | (128, 3, 3) | (128, 3, 3) | 256 |
| 42 | features.6.dw.2 | ReLU | (128, 3, 3) | (128, 3, 3) | 0 |
| 43 | features.6.pw.0 | Conv2d | (128, 3, 3) | (192, 3, 3) | 24576 |
| 44 | features.6.pw.1 | BatchNorm2d | (192, 3, 3) | (192, 3, 3) | 384 |
| 45 | features.6.pw.2 | ReLU | (192, 3, 3) | (192, 3, 3) | 0 |
| 46 | features.7.dw.0 | Conv2d | (192, 3, 3) | (192, 3, 3) | 1728 |
| 47 | features.7.dw.1 | BatchNorm2d | (192, 3, 3) | (192, 3, 3) | 384 |
| 48 | features.7.dw.2 | ReLU | (192, 3, 3) | (192, 3, 3) | 0 |
| 49 | features.7.pw.0 | Conv2d | (192, 3, 3) | (192, 3, 3) | 36864 |
| 50 | features.7.pw.1 | BatchNorm2d | (192, 3, 3) | (192, 3, 3) | 384 |
| 51 | features.7.pw.2 | ReLU | (192, 3, 3) | (192, 3, 3) | 0 |
| 52 | features.8.dw.0 | Conv2d | (192, 3, 3) | (192, 3, 3) | 1728 |
| 53 | features.8.dw.1 | BatchNorm2d | (192, 3, 3) | (192, 3, 3) | 384 |
| 54 | features.8.dw.2 | ReLU | (192, 3, 3) | (192, 3, 3) | 0 |
| 55 | features.8.pw.0 | Conv2d | (192, 3, 3) | (256, 3, 3) | 49152 |
| 56 | features.8.pw.1 | BatchNorm2d | (256, 3, 3) | (256, 3, 3) | 512 |
| 57 | features.8.pw.2 | ReLU | (256, 3, 3) | (256, 3, 3) | 0 |
| 58 | features.9 | Conv2d | (256, 3, 3) | (512, 3, 3) | 131072 |
| 59 | features.10 | BatchNorm2d | (512, 3, 3) | (512, 3, 3) | 1024 |
| 60 | features.11 | ReLU | (512, 3, 3) | (512, 3, 3) | 0 |
| 61 | pool | AdaptiveAvgPool2d | (512, 3, 3) | (512, 1, 1) | 0 |
| 62 | drop | Dropout | (512,) | (512,) | 0 |
| 63 | fc | Linear | (512,) | (52,) | 26676 |
