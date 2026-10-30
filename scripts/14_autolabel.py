#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
14_autolabel.py
===============
模型辅助补标注（半自动）。

对训练/验证集中「漏标」或「原始框被清洗规则删掉（多张牌粘连）」的位置：
  1. 用第一版检测器以较低阈值推理；
  2. 与已有 GT 做 IoU 匹配，未匹配的高分预测视为候选新目标；
  3. 用分类器复核，置信度足够高才写入 datasets/det_auto；
  4. 输出新增框清单与可视化，便于人工抽查。

注意：不修改原始数据；det_auto = 清洗 GT + 自动补框。

用法：
    python scripts/14_autolabel.py --conf 0.25 --add-conf 0.60 --cls-conf 0.85
"""
import argparse
import json
import os
import shutil
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from cardtools import classify, crop_square, iou, load_classifier

try:
    FONT = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
except Exception:
    FONT = ImageFont.load_default()

ROOT = Path(__file__).absolute().parents[1]
WORKSPACE = ROOT.parents[1]
DET = ROOT / "datasets" / "det"
AUTO = ROOT / "datasets" / "det_auto"
META = ROOT / "datasets" / "meta"
MODELS = ROOT / "models"
REPORT = ROOT / "report"

RAW = {
    "mahjong": WORKSPACE / "麻将" / "mahjong_yolo_real",
    "poker": WORKSPACE / "扑克" / "poker_yolo_real",
}
PREFIX = {"mahjong": "mj", "poker": "pk"}
DET_CLS = {"mahjong": 0, "poker": 1}
IMG_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def link_or_copy(src: Path, dst: Path):
    if dst.exists():
        return
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--conf", type=float, default=0.25)
    ap.add_argument("--add-conf", type=float, default=0.60)
    ap.add_argument("--cls-conf", type=float, default=0.85)
    ap.add_argument("--iou", type=float, default=0.45)
    ap.add_argument("--splits", default="train,valid")
    ap.add_argument("--max-vis", type=int, default=36)
    args = ap.parse_args()

    from ultralytics import YOLO

    model = YOLO(str(MODELS / "detector" / "best.pt"))
    clfs = {d: load_classifier(d, MODELS) for d in ("mahjong", "poker")}
    ann = json.loads((META / "annotations_clean.json").read_text(encoding="utf-8"))

    stats = Counter()
    added_list = []
    preview = []

    for split in args.splits.split(","):
        (AUTO / "images" / split).mkdir(parents=True, exist_ok=True)
        (AUTO / "labels" / split).mkdir(parents=True, exist_ok=True)
        for domain in ("mahjong", "poker"):
            det_cls = DET_CLS[domain]
            seen = {r["name"] for r in ann[domain][split]}
            records = list(ann[domain][split])
            # 清洗时因为“整排/粘连框”被全部删掉的图片：也纳入补标，GT 视为空
            raw_idir = RAW[domain] / split / "images"
            if raw_idir.exists():
                for raw_img in sorted(raw_idir.iterdir()):
                    if raw_img.suffix.lower() not in IMG_EXTS:
                        continue
                    name = f"{PREFIX[domain]}_{raw_img.stem}"
                    if name in seen:
                        continue
                    records.append({"image": str(raw_img), "det_image": str(raw_img),
                                    "name": name, "boxes": []})
                    stats[f"{domain}_recovered_images"] += 1
            for rec in records:
                src = Path(rec["det_image"])
                img = Image.open(src).convert("RGB")
                W, H = img.size
                gts = [{"box": ((x - w / 2) * W, (y - h / 2) * H, (x + w / 2) * W, (y + h / 2) * H)}
                       for (c, x, y, w, h) in rec["boxes"]]
                res = model.predict(str(src), imgsz=640, conf=args.conf, batch=16, verbose=False)[0]
                preds = [{"box": tuple(float(v) for v in b.xyxy[0]), "conf": float(b.conf.item()),
                          "cls": int(b.cls.item())} for b in res.boxes if int(b.cls.item()) == det_cls]

                used = set()
                for gt in gts:
                    best, bi = 0.0, -1
                    for j, p in enumerate(preds):
                        if j in used:
                            continue
                        v = iou(gt["box"], p["box"])
                        if v > best:
                            best, bi = v, j
                    if best >= args.iou:
                        used.add(bi)
                        stats[f"{domain}_gt_matched"] += 1
                    else:
                        stats[f"{domain}_gt_missed"] += 1
                stats[f"{domain}_gt"] += len(gts)

                new_boxes = []
                for j, p in enumerate(preds):
                    if j in used or p["conf"] < args.add_conf:
                        continue
                    x1, y1, x2, y2 = p["box"]
                    w, h = (x2 - x1) / W, (y2 - y1) / H
                    area = w * h
                    ar = w / max(h, 1e-9)
                    if not (5e-4 < area < (0.08 if domain == "mahjong" else 0.45)):
                        continue
                    if not (0.2 < ar < 1.8):
                        continue
                    crop = crop_square(img, p["box"])
                    c, pconf = classify(clfs[domain][0], crop)
                    if pconf < args.cls_conf:
                        continue
                    new_boxes.append((c, p, pconf))
                    stats[f"{domain}_added"] += 1
                    added_list.append({
                        "split": split, "domain": domain, "image": rec["name"], "class": c,
                        "class_name": clfs[domain][1][c], "det_conf": round(p["conf"], 4),
                        "cls_conf": round(pconf, 4),
                        "box": [round(v, 5) for v in (x1 / W, y1 / H, (x2 - x1) / W, (y2 - y1) / H)],
                    })
                    if len(preview) < args.max_vis:
                        preview.append((crop, f"{clfs[domain][1][c]} {pconf:.2f}"))

                lines = [f"{det_cls} {x:.6f} {y:.6f} {w:.6f} {h:.6f}" for (_, x, y, w, h) in rec["boxes"]]
                for (c, p, pconf) in new_boxes:
                    x1, y1, x2, y2 = p["box"]
                    x, y, w, h = (x1 + x2) / 2 / W, (y1 + y2) / 2 / H, (x2 - x1) / W, (y2 - y1) / H
                    lines.append(f"{det_cls} {x:.6f} {y:.6f} {w:.6f} {h:.6f}")
                (AUTO / "labels" / split / f"{rec['name']}.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
                out_img = AUTO / "images" / split / (rec["name"] + Path(rec["image"]).suffix)
                link_or_copy(src, out_img)

    # 测试集原样保留，便于最终评估
    (AUTO / "images" / "test").mkdir(parents=True, exist_ok=True)
    (AUTO / "labels" / "test").mkdir(parents=True, exist_ok=True)
    for src in (DET / "images" / "test").glob("*"):
        link_or_copy(src, AUTO / "images" / "test" / src.name)
    for src in (DET / "labels" / "test").glob("*.txt"):
        shutil.copy2(src, AUTO / "labels" / "test" / src.name)

    yaml_text = (
        f"path: {AUTO.as_posix()}\n"
        "train: images/train\n"
        "val: images/valid\n"
        "test: images/test\n"
        "names:\n"
        "  0: mahjong_tile\n"
        "  1: poker_card\n"
    )
    (AUTO / "data.yaml").write_text(yaml_text, encoding="utf-8")

    report = {"config": vars(args), "stats": dict(stats), "added": added_list}
    (REPORT / "autolabel.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    if preview:
        cell, cols = 140, 6
        rows = (len(preview) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cell, rows * cell), (20, 20, 20))
        dr = ImageDraw.Draw(sheet)
        for k, (im, txt) in enumerate(preview):
            r, c = divmod(k, cols)
            sheet.paste(im.resize((cell, cell)), (c * cell, r * cell))
            dr.text((c * cell + 3, r * cell + 3), txt, fill=(0, 255, 0), font=FONT)
        sheet.save(REPORT / "autolabel_preview.jpg", quality=90)

    print(json.dumps(dict(stats), ensure_ascii=False, indent=2))
    print(f"added={len(added_list)} -> {AUTO}")


if __name__ == "__main__":
    main()
