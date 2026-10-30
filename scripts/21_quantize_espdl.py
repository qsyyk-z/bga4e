#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
21_quantize_espdl.py
====================
用 02_Face_recognition_project 自带的 ESP-DL 量化工具，把分类器 ONNX 转成
ESP32-S3 可用的 int8 系数（C++ .cpp/.hpp）。

必须用 Python 3.7 运行（官方 .pyd 只提供旧版 Python 二进制）：

    <project>\.venv37\Scripts\python.exe scripts\21_quantize_espdl.py --domain mahjong

输出：
    firmware/<domain>/espdl_coeff/card_cls_coefficient.cpp/.hpp
    firmware/<domain>/espdl_coeff/quant_table.pickle
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).absolute().parents[1]
TOOL = ROOT.parents[0] / "02_Face_recognition_project" / "components" / "esp-dl" / "tools" / "quantization_tool"
sys.path.insert(0, str(TOOL / "windows"))
sys.path.insert(0, str(TOOL))

from optimizer import optimize_fp_model  # noqa: E402
from calibrator import Calibrator  # noqa: E402
import onnx  # noqa: E402

INPUT = 96
MEAN = np.array([0.5, 0.5, 0.5], dtype=np.float32)
STD = np.array([0.5, 0.5, 0.5], dtype=np.float32)


def preprocess(path):
    im = Image.open(path).convert("RGB").resize((112, 112), Image.BILINEAR)
    left = (112 - INPUT) // 2
    im = im.crop((left, left, left + INPUT, left + INPUT))
    x = np.asarray(im, dtype=np.float32) / 255.0
    x = (x - MEAN) / STD
    return np.transpose(x, (2, 0, 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--domain", choices=["mahjong", "poker"], required=True)
    ap.add_argument("--calib", type=int, default=300)
    ap.add_argument("--quant", default="int8", choices=["int8", "int16"])
    args = ap.parse_args()

    model_path = ROOT / "models" / f"classifier_{args.domain}" / "classifier.onnx"
    out_dir = ROOT / "firmware" / args.domain / "espdl_coeff"
    out_dir.mkdir(parents=True, exist_ok=True)

    optimized = optimize_fp_model(str(model_path))
    print("[opt] ->", optimized)
    model_proto = onnx.load(optimized)

    files = sorted((ROOT / "datasets" / "cls" / args.domain / "valid").glob("*/*.jpg"))[:args.calib]
    calib = np.stack([preprocess(f) for f in files]).astype(np.float32)
    print("[calib] shape", calib.shape, "files", len(files))

    if args.quant == "int8":
        calib_tool = Calibrator("int8", "per-channel", "entropy")
    else:
        calib_tool = Calibrator("int16", "per-tensor", "minmax")
    calib_tool.set_providers(["CPUExecutionProvider"])
    pickle_path = out_dir / "quant_table.pickle"
    calib_tool.generate_quantization_table(model_proto, calib, str(pickle_path))
    calib_tool.export_coefficient_to_cpp(model_proto, str(pickle_path), "esp32s3",
                                        str(out_dir), "card_cls", True)
    print("[ok]", out_dir)
    for p in sorted(out_dir.iterdir()):
        print("  ", p.name, p.stat().st_size)


if __name__ == "__main__":
    main()
