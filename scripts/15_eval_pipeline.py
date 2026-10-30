#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
15_eval_pipeline.py
===================
端到端评估：检测器（2 类）+ 分类器（麻将/扑克）。

指标：
  * det_recall      测试集 GT 框被检出的比例（IoU>=0.5）
  * cls_acc_matched 已检出框中分类正确的比例
  * e2e_acc         端到端正确率（检出且分类正确 / 全部 GT）
  * 按 domain 和按类别拆分，并保存混淆对与可视化

用法：
    python scripts/15_eval_pipeline.py --conf 0.35
"""
import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

import torch
from PIL import Image, ImageDraw, ImageFont

from cardtools import TF_VAL, crop_square, iou, load_classifier

try:
    FONT_MJ = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 18)
except Exception:
    FONT_MJ = ImageFont.load_default()
try:
    FONT_PK = ImageFont.truetype("C:/Windows/Fonts/seguisym.ttf", 18)
except Exception:
    FONT_PK = FONT_MJ

ROOT = Path(__file__).absolute().parents[1]
META = ROOT / "datasets" / "meta"
MODELS = ROOT / "models"
REPORT = ROOT / "report"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--conf", type=float, default=0.35)
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--max-vis", type=int, default=12)
    ap.add_argument("--domains", default="mahjong,poker")
    args = ap.parse_args()
    domains = [d.strip() for d in args.domains.split(",") if d.strip()]

    from ultralytics import YOLO
    det = YOLO(str(MODELS / "detector" / "best.pt"))
    clfs = {d: load_classifier(d, MODELS) for d in domains}

    ann = json.loads((META / "annotations_clean.json").read_text(encoding="utf-8"))
    stats = defaultdict(Counter)
    per_class = defaultdict(lambda: {"gt": 0, "det": 0, "cls_ok": 0})
    conf_mat = defaultdict(Counter)
    vis_items = []

    for domain in domains:
        det_cls = 0 if domain == "mahjong" else 1
        names = clfs[domain][1]
        for rec in ann[domain]["test"]:
            img_path = Path(rec["det_image"])
            img = Image.open(img_path).convert("RGB")
            W, H = img.size
            res = det.predict(str(img_path), imgsz=args.imgsz, conf=args.conf, verbose=False)[0]
            preds = []
            for b in res.boxes:
                if int(b.cls.item()) != det_cls:
                    continue
                x1, y1, x2, y2 = [float(v) for v in b.xyxy[0]]
                crop = crop_square(img, (x1, y1, x2, y2), pad=0.10)
                with torch.no_grad():
                    logits = clfs[domain][0](TF_VAL(crop).unsqueeze(0))
                    prob = torch.softmax(logits, 1)
                    c = int(prob.argmax(1))
                    pconf = float(prob[0, c])
                preds.append({"box": (x1 / W, y1 / H, x2 / W, y2 / H),
                              "conf": float(b.conf.item()), "cls": c, "pconf": pconf})

            gts = [{"cls": c, "box": (x - w / 2, y - h / 2, x + w / 2, y + h / 2)}
                   for (c, x, y, w, h) in rec["boxes"]]
            used = set()
            for gt in gts:
                per_class[(domain, gt["cls"])]["gt"] += 1
                stats[domain]["gt"] += 1
                best, bi = 0.0, -1
                for j, p in enumerate(preds):
                    if j in used:
                        continue
                    v = iou(gt["box"], p["box"])
                    if v > best:
                        best, bi = v, j
                if best >= 0.5:
                    used.add(bi)
                    stats[domain]["det"] += 1
                    per_class[(domain, gt["cls"])]["det"] += 1
                    if preds[bi]["cls"] == gt["cls"]:
                        stats[domain]["cls_ok"] += 1
                        per_class[(domain, gt["cls"])]["cls_ok"] += 1
                    else:
                        conf_mat[domain][(gt["cls"], preds[bi]["cls"])] += 1
                else:
                    stats[domain]["miss"] += 1
                if len(vis_items) < args.max_vis and best >= 0.5:
                    vis_items.append((domain, img, preds, gts, names))

    out = {}
    for domain in domains:
        s = stats[domain]
        n = max(s["gt"], 1)
        out[domain] = {"gt": s["gt"], "detected": s["det"], "missed": s["miss"],
                       "det_recall": round(s["det"] / n, 4),
                       "cls_acc_matched": round(s["cls_ok"] / max(s["det"], 1), 4),
                       "e2e_acc": round(s["cls_ok"] / n, 4)}
    tot = sum(stats[d]["gt"] for d in domains)
    ok = sum(stats[d]["cls_ok"] for d in domains)
    out["overall"] = {"gt": tot, "e2e_correct": ok, "e2e_acc": round(ok / max(tot, 1), 4),
                      "det_recall": round(sum(stats[d]["det"] for d in domains) / max(tot, 1), 4)}
    out["confusion"] = {d: {f"{a}->{b}": n for (a, b), n in conf_mat[d].items()} for d in conf_mat}
    out["per_class"] = {d: [{"idx": i, "name": clfs[d][1][i], **per_class[(d, i)]}
                            for i in range(len(clfs[d][1]))] for d in domains}
    (REPORT / "pipeline_metrics.json").write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(out["overall"], ensure_ascii=False, indent=2))
    for d in domains:
        print(d, out[d])

    for k, (domain, img, preds, gts, names) in enumerate(vis_items[:args.max_vis]):
        im = img.copy()
        dr = ImageDraw.Draw(im)
        W, H = im.size
        for p in preds:
            x1, y1, x2, y2 = p["box"]
            dr.rectangle([x1 * W, y1 * H, x2 * W, y2 * H], outline=(0, 255, 0), width=2)
            label = names[p["cls"]] if p["pconf"] > 0.5 else "?"
            font = FONT_MJ if domain == "mahjong" else FONT_PK
            dr.text((x1 * W + 2, y1 * H + 2), f"{label} {p['pconf']:.2f}", fill=(0, 255, 0), font=font)
        for g in gts:
            x1, y1, x2, y2 = g["box"]
            dr.rectangle([x1 * W, y1 * H, x2 * W, y2 * H], outline=(255, 60, 60), width=1)
        im.save(REPORT / f"pipeline_{k:02d}_{domain}.jpg", quality=88)


if __name__ == "__main__":
    main()
