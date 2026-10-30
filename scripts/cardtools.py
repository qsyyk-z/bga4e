#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""cardtools.py —— 推理/评估共用的小工具。"""
import json
from pathlib import Path

import torch
from PIL import Image
from torchvision import transforms as T

from cardnet import CardNet

INPUT = 96
MEAN, STD = (0.5, 0.5, 0.5), (0.5, 0.5, 0.5)
TF_VAL = T.Compose([T.Resize(112), T.CenterCrop(INPUT), T.ToTensor(), T.Normalize(MEAN, STD)])


def iou(a, b):
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
    inter = iw * ih
    ua = (ax2 - ax1) * (ay2 - ay1) + (bx2 - bx1) * (by2 - by1) - inter
    return inter / ua if ua > 0 else 0.0


def crop_square(img: Image.Image, box, pad=0.10, size=112, fill=(114, 114, 114)):
    """box 为像素坐标 (x1,y1,x2,y2)，外扩 pad 后补成正方形。"""
    W, H = img.size
    x1, y1, x2, y2 = box
    bw, bh = x2 - x1, y2 - y1
    x1 = max(0, x1 - bw * pad)
    y1 = max(0, y1 - bh * pad)
    x2 = min(W, x2 + bw * pad)
    y2 = min(H, y2 + bh * pad)
    crop = img.crop((x1, y1, x2, y2))
    side = max(crop.size)
    canvas = Image.new("RGB", (side, side), fill)
    canvas.paste(crop, ((side - crop.width) // 2, (side - crop.height) // 2))
    return canvas.resize((size, size), Image.LANCZOS)


def load_classifier(domain, models_dir):
    models_dir = Path(models_dir)
    ckpt = torch.load(models_dir / f"classifier_{domain}" / "best.pt", map_location="cpu", weights_only=False)
    model = CardNet(ckpt["n_classes"], width=ckpt.get("width", 1.0))
    model.load_state_dict(ckpt["model"])
    model.eval()
    meta = json.loads((models_dir / f"classifier_{domain}" / "classes.json").read_text(encoding="utf-8"))
    return model, [c["name"] for c in meta["classes"]], meta["classes"]


@torch.no_grad()
def classify(model, crop):
    x = TF_VAL(crop).unsqueeze(0)
    prob = torch.softmax(model(x), 1)[0]
    p, c = float(prob.max()), int(prob.argmax())
    return c, p
