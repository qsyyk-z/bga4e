#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
16_quantize_onnx.py
===================
瀵硅缁冨ソ鐨勫垎绫诲櫒鍋?ONNX 闈欐€?int8 閲忓寲锛坧er-channel锛夛紝骞堕獙璇佺簿搴︽崯澶便€?杩欎竴姝ョ敤浜?PC/杈圭紭绔揩閫熼獙璇侊紱ESP-DL 鐨勯噺鍖栬鍙傝 README 涓殑瀹樻柟宸ュ叿閾捐鏄庛€?
鐢ㄦ硶锛?    python scripts/16_quantize_onnx.py --domain mahjong
"""
import argparse
import json
import random
import sys
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

import numpy as np
from PIL import Image
from onnxruntime.quantization import (CalibrationDataReader, QuantFormat, QuantType,
                                     quantize_static)

ROOT = Path(__file__).absolute().parents[1]
CLS = ROOT / "datasets" / "cls"
MODELS = ROOT / "models"
INPUT = 96
MEAN = np.array([0.5, 0.5, 0.5], dtype=np.float32)
STD = np.array([0.5, 0.5, 0.5], dtype=np.float32)


def preprocess(path: Path) -> np.ndarray:
    im = Image.open(path).convert("RGB").resize((112, 112), Image.BILINEAR)
    left = (112 - INPUT) // 2
    im = im.crop((left, left, left + INPUT, left + INPUT))
    x = np.asarray(im, dtype=np.float32) / 255.0
    x = (x - MEAN) / STD
    return np.transpose(x, (2, 0, 1))[None, ...].astype(np.float32)


class Reader(CalibrationDataReader):
    def __init__(self, files, input_name):
        self.data = [preprocess(f) for f in files]
        self.input_name = input_name
        self.i = 0

    def get_next(self):
        if self.i >= len(self.data):
            return None
        item = {self.input_name: self.data[self.i]}
        self.i += 1
        return item


def collect(domain, split, max_n=600):
    files = []
    root = CLS / domain / split
    for d in sorted(root.iterdir()):
        if d.is_dir():
            files.extend(sorted(d.glob("*.jpg")))
    random.seed(0)
    random.shuffle(files)
    return files[:max_n]


def eval_onnx(path, files, labels):
    import onnxruntime as ort
    sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    name = sess.get_inputs()[0].name
    correct = 0
    for f, y in zip(files, labels):
        out = sess.run(None, {name: preprocess(f)})[0]
        correct += int(out.argmax(1)[0] == y)
    return correct / max(len(files), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--domain", choices=["mahjong", "poker"], required=True)
    ap.add_argument("--calib", type=int, default=500)
    args = ap.parse_args()

    out_dir = MODELS / f"classifier_{args.domain}"
    fp32 = out_dir / "classifier.onnx"
    int8 = out_dir / "classifier_int8.onnx"

    calib_files = collect(args.domain, "valid", args.calib)
    quantize_static(str(fp32), str(int8), Reader(calib_files, "input"),
                    quant_format=QuantFormat.QOperator, per_channel=True,
                    activation_type=QuantType.QUInt8, weight_type=QuantType.QInt8)
    print(f"[{args.domain}] quantized -> {int8} ({int8.stat().st_size/1024:.1f} KB)")

    test_files = collect(args.domain, "test", 10000)
    labels = [int(f.parent.name) for f in test_files]
    acc_fp32 = eval_onnx(fp32, test_files, labels)
    acc_int8 = eval_onnx(int8, test_files, labels)
    metrics_path = out_dir / "metrics.json"
    metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
    metrics["quantization"] = {"int8_top1": round(acc_int8, 4), "fp32_top1_onnx": round(acc_fp32, 4),
                               "int8_size_kb": round(int8.stat().st_size / 1024, 1),
                               "fp32_size_kb": round(fp32.stat().st_size / 1024, 1)}
    metrics_path.write_text(json.dumps(metrics, ensure_ascii=False, indent=2), encoding="utf-8")
    print(metrics["quantization"])


if __name__ == "__main__":
    main()

