#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""00_check_env.py —— 训练环境自检。"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).absolute().parents[1]


def main():
    print("python:", sys.version)
    try:
        import torch
        print("torch:", torch.__version__, "cuda:", torch.cuda.is_available())
        if torch.cuda.is_available():
            print("gpu:", torch.cuda.get_device_name(0))
    except Exception as e:
        print("torch: MISSING", e)
    try:
        import ultralytics
        print("ultralytics:", ultralytics.__version__)
    except Exception as e:
        print("ultralytics: MISSING", e)
    try:
        import cv2
        print("opencv:", cv2.__version__)
        sample = next((ROOT / "datasets" / "det" / "images" / "train").glob("*.jpg"), None)
        if sample:
            from ultralytics.utils.patches import imread
            print("ultralytics imread:", imread(sample) is not None, sample.name)
    except Exception as e:
        print("opencv: MISSING", e)

    meta = ROOT / "datasets" / "meta" / "clean_report.json"
    if meta.exists():
        rep = json.loads(meta.read_text(encoding="utf-8"))
        print("dataset det:", rep["det"])
    for p in (ROOT / "models" / "detector" / "best.pt",
              ROOT / "models" / "classifier_mahjong" / "best.pt",
              ROOT / "models" / "classifier_poker" / "best.pt"):
        print(("FOUND " if p.exists() else "missing ") + str(p))


if __name__ == "__main__":
    main()
