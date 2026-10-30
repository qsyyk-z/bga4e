#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
20_export_espdl_package.py
==========================
把麻将分类器整理成 ESP-DL 部署包：

firmware/mahjong/
├── classifier.onnx          FP32 ONNX
├── classifier_int8.onnx     int8 ONNX（PC 验证用）
├── classes.json             类别顺序 / 预处理参数
├── metrics.json             精度
├── model_layers.md          逐层输入输出形状（写 C++ 模型时对照）
├── calib_list.txt           校准图清单
└── README.md                端侧接入说明

用法：
    python scripts/20_export_espdl_package.py --domain mahjong
"""
import argparse
import json
import shutil
from pathlib import Path

import torch

from cardnet import CardNet

ROOT = Path(__file__).absolute().parents[1]
MODELS = ROOT / "models"
CLS = ROOT / "datasets" / "cls"
FW = ROOT / "firmware"


def layer_table(model, input_size=96):
    rows = []
    hooks = []

    def make_hook(name):
        def hook(module, inp, out):
            if isinstance(out, (tuple, list)):
                out = out[0]
            rows.append((name, module.__class__.__name__,
                         tuple(inp[0].shape[1:]) if inp and hasattr(inp[0], "shape") else (),
                         tuple(out.shape[1:]) if hasattr(out, "shape") else (),
                         sum(p.numel() for p in module.parameters(recurse=False))))
        return hook

    for name, m in model.named_modules():
        if len(list(m.children())) == 0 and name:
            hooks.append(m.register_forward_hook(make_hook(name)))
    with torch.no_grad():
        model(torch.randn(1, 3, input_size, input_size))
    for h in hooks:
        h.remove()
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--domain", default="mahjong")
    ap.add_argument("--calib", type=int, default=300)
    args = ap.parse_args()

    out = FW / args.domain
    out.mkdir(parents=True, exist_ok=True)
    src = MODELS / f"classifier_{args.domain}"

    for name in ("classifier.onnx", "classifier_int8.onnx", "classes.json", "metrics.json"):
        p = src / name
        if p.exists():
            shutil.copy2(p, out / name)
    shutil.copy2(MODELS / "detector" / "best_320.onnx", out / "detector_320.onnx")
    shutil.copy2(MODELS / "detector" / "metrics.json", out / "detector_metrics.json")

    classes = json.loads((src / "classes.json").read_text(encoding="utf-8"))
    model = CardNet(classes["num_classes"], width=1.0)
    ckpt = torch.load(src / "best.pt", map_location="cpu", weights_only=False)
    model.load_state_dict(ckpt["model"])
    model.eval()

    rows = layer_table(model, classes["input_size"])
    lines = ["# CardNet 逐层形状", "",
             f"输入: `[1, 3, {classes['input_size']}, {classes['input_size']}]`（NCHW）", "",
             "| # | layer | type | in (C,H,W) | out (C,H,W) | params |",
             "| ---: | --- | --- | --- | --- | ---: |"]
    for i, (name, typ, ishape, oshape, params) in enumerate(rows, 1):
        lines.append(f"| {i} | {name} | {typ} | {ishape} | {oshape} | {params} |")
    (out / "model_layers.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    calib_files = sorted((CLS / args.domain / "valid").glob("*/*.jpg"))[:args.calib]
    (out / "calib_list.txt").write_text("\n".join(str(p) for p in calib_files) + "\n", encoding="utf-8")

    readme = f"""# {args.domain} 部署包

## 文件

| 文件 | 说明 |
| --- | --- |
| `classifier.onnx` | FP32 分类器，输入 `[1,3,{classes['input_size']},{classes['input_size']}]` |
| `classifier_int8.onnx` | int8 分类器（PC 验证，ONNX Runtime QOperator） |
| `classes.json` | 类别顺序、mean/std、输入尺寸 |
| `metrics.json` | 精度与 int8 对比 |
| `model_layers.md` | 逐层输入输出形状，写 ESP-DL C++ 模型时对照 |
| `calib_list.txt` | 推荐校准图（{len(calib_files)} 张） |
| `detector_320.onnx` | 可选：2 类检测器 320×320 |

## 预处理（必须与训练一致）

1. 由检测框外扩 **10%**，补成正方形（灰底 114,114,114）；
2. 缩放到 **{classes['input_size']}×{classes['input_size']}**；
3. `x = (pixel / 255 - 0.5) / 0.5`，即 `[-1, 1]`；
4. NCHW（ESP-DL 内部为 HWC，接入时按库的 Tensor 约定转换）。

## 类别

{', '.join(c['name'] for c in classes['classes'])}

## 输出

`logits`，长度 {classes['num_classes']}；取 argmax，置信度 < 0.5 建议显示 `?`。

## 量化

参见仓库根目录 `firmware/README.md` 的 ESP-DL 量化流程。`classifier_int8.onnx` 只是 PC 端验证结果，端侧请用官方 `Calibrator` 生成 int8/int16 系数。
"""
    (out / "README.md").write_text(readme, encoding="utf-8")
    print(f"[ok] {out}")
    print("\n".join(lines[:12]))


if __name__ == "__main__":
    main()
