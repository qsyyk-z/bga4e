#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
17_predict_image.py
===================
对单张图片做「检测 + 分类」，输出带标注的图片和 JSON 结果。

用法：
    python scripts/17_predict_image.py --image path/to/test.jpg
    python scripts/17_predict_image.py --image test.jpg --out report/demo.jpg --conf 0.35
"""
import argparse
import json
from pathlib import Path

import torch
from PIL import Image, ImageDraw, ImageFont

from cardtools import TF_VAL, crop_square, load_classifier

try:
    FONT_MJ = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 15)
except Exception:
    FONT_MJ = ImageFont.load_default()
try:
    FONT_PK = ImageFont.truetype("C:/Windows/Fonts/seguisym.ttf", 16)
except Exception:
    FONT_PK = FONT_MJ

ROOT = Path(__file__).absolute().parents[1]
MODELS = ROOT / "models"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--conf", type=float, default=0.35)
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--domains", default="mahjong,poker")
    args = ap.parse_args()
    domains = [d.strip() for d in args.domains.split(",") if d.strip()]

    from ultralytics import YOLO
    det = YOLO(str(MODELS / "detector" / "best.pt"))
    clfs = {d: load_classifier(d, MODELS) for d in domains}

    img_path = Path(args.image)
    img = Image.open(img_path).convert("RGB")
    W, H = img.size
    res = det.predict(str(img_path), imgsz=args.imgsz, conf=args.conf, verbose=False)[0]

    out = []
    for b in res.boxes:
        x1, y1, x2, y2 = [float(v) for v in b.xyxy[0]]
        det_cls = int(b.cls.item())
        domain = "mahjong" if det_cls == 0 else "poker"
        if domain not in clfs:
            continue
        crop = crop_square(img, (x1, y1, x2, y2), pad=0.10)
        with torch.no_grad():
            prob = torch.softmax(clfs[domain][0](TF_VAL(crop).unsqueeze(0)), 1)[0]
            c = int(prob.argmax())
            pconf = float(prob[c])
        out.append({"domain": domain, "class": clfs[domain][1][c], "class_idx": c,
                    "class_conf": round(pconf, 4), "det_conf": round(float(b.conf.item()), 4),
                    "box": [round(x1, 1), round(y1, 1), round(x2, 1), round(y2, 1)]})

    im = img.copy()
    dr = ImageDraw.Draw(im)
    for r in out:
        x1, y1, x2, y2 = r["box"]
        ok = r["class_conf"] >= 0.5
        color = (0, 200, 0) if ok else (255, 160, 0)
        dr.rectangle([x1, y1, x2, y2], outline=color, width=2)
        label = f"{r['class']} {r['class_conf']:.2f}" if ok else "?"
        font = FONT_MJ if r["domain"] == "mahjong" else FONT_PK
        bbox = dr.textbbox((x1 + 2, max(0, y1 - 18)), label, font=font)
        dr.rectangle(bbox, fill=(0, 0, 0))
        dr.text((x1 + 2, max(0, y1 - 18)), label, fill=color, font=font)

    out_path = Path(args.out) if args.out else ROOT / "report" / f"predict_{img_path.stem}.jpg"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    im.save(out_path, quality=90)
    print(json.dumps({"image": str(img_path), "output": str(out_path), "results": out},
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
