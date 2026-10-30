#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
01_inspect_data.py
==================
原始数据集体检 + 可视化，用于人工核对标注。

输出：
  report/audit_raw.json      原始数据统计（图片/框/类别/异常）
  report/preview/*.jpg       带框预览图
  report/mahjong_class_sheets/class_XX.png   麻将每类 8 个裁剪样本

用法：
    python scripts/01_inspect_data.py
"""
import json
import random
from collections import Counter, defaultdict
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).absolute().parents[1]
WORKSPACE = ROOT.parents[1]
POKER_DIR = WORKSPACE / "扑克" / "poker_yolo_real"
MAHJONG_DIR = WORKSPACE / "麻将" / "mahjong_yolo_real"
REPORT = ROOT / "report"
PREVIEW = REPORT / "preview"
SHEETS = REPORT / "mahjong_class_sheets"
for d in (PREVIEW, SHEETS):
    d.mkdir(parents=True, exist_ok=True)

SPLITS = ("train", "valid", "test")
IMG_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

try:
    FONT = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 14)
except Exception:
    FONT = ImageFont.load_default()

POKER_NAMES = [
    "10 Diamonds", "10 Hearts", "10 Spades", "10 Trefoils",
    "2 Diamonds", "2 Hearts", "2 Spades", "2 Trefoils",
    "3 Diamonds", "3 Hearts", "3 Spades", "3 Trefoils",
    "4 Diamonds", "4 Hearts", "4 Spades", "4 Trefoils",
    "5 Diamonds", "5 Hearts", "5 Spades", "5 Trefoils",
    "59(junk)",
    "6 Diamonds", "6 Hearts", "6 Spades", "6 Trefoils",
    "7 Diamonds", "7 Hearts", "7 Spades", "7 Trefoils",
    "8 Diamonds", "8 Hearts", "8 Spades", "8 Trefoils",
    "9 Diamonds", "9 Hearts", "9 Spades", "9 Trefoils",
    "A Diamonds", "A Hearts", "A Spades", "A Trefoils",
    "J Diamonds", "J Hearts", "J Spades", "J Trefoils",
    "K Diamonds", "K Hearts", "K Spades", "K Trefoils",
    "Q Diamonds", "Q Hearts", "Q Spades", "Q Trefoils",
]


def read_labels(path: Path):
    if not path.exists():
        return None
    rows = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        s = line.split()
        if len(s) >= 5:
            rows.append((int(float(s[0])), *map(float, s[1:5])))
    return rows


def image_files(split_dir: Path):
    return sorted(p for p in split_dir.iterdir() if p.suffix.lower() in IMG_EXTS)


def audit(name, root, names):
    stats = {"dataset": name, "splits": {}, "classes": {}, "issues": {}}
    issues = Counter()
    class_counter = Counter()
    for split in SPLITS:
        idir, ldir = root / split / "images", root / split / "labels"
        imgs = image_files(idir) if idir.exists() else []
        n_boxes = 0
        for img in imgs:
            lab = read_labels(ldir / f"{img.stem}.txt")
            if lab is None:
                issues["missing_label"] += 1
                continue
            if not lab:
                issues["empty_label"] += 1
                continue
            seen = set()
            for (c, x, y, w, h) in lab:
                n_boxes += 1
                class_counter[c] += 1
                if c < 0 or (names is not None and c >= len(names)):
                    issues["class_out_of_range"] += 1
                if not (0 <= x <= 1 and 0 <= y <= 1 and 0 < w <= 1 and 0 < h <= 1):
                    issues["bbox_out_of_range"] += 1
                if w * h < 0.001:
                    issues["tiny_bbox"] += 1
                key = (c, round(x, 5), round(y, 5), round(w, 5), round(h, 5))
                if key in seen:
                    issues["duplicate_bbox"] += 1
                seen.add(key)
        stats["splits"][split] = {"images": len(imgs), "boxes": n_boxes}
    stats["classes"] = {str(k): v for k, v in sorted(class_counter.items())}
    stats["issues"] = dict(issues)
    return stats


def draw_preview(name, root, names, n=6):
    random.seed(2026)
    files = []
    for split in SPLITS:
        if (root / split / "images").exists():
            files += image_files(root / split / "images")
    for idx, img_path in enumerate(random.sample(files, min(n, len(files)))):
        split = img_path.parent.parent.name
        rows = read_labels(root / split / "labels" / f"{img_path.stem}.txt") or []
        im = Image.open(img_path).convert("RGB")
        dr = ImageDraw.Draw(im)
        W, H = im.size
        for (c, x, y, w, h) in rows:
            x1, y1 = (x - w / 2) * W, (y - h / 2) * H
            x2, y2 = (x + w / 2) * W, (y + h / 2) * H
            dr.rectangle([x1, y1, x2, y2], outline=(255, 40, 40), width=2)
            label = names[c] if names and c < len(names) else str(c)
            dr.text((x1 + 2, max(0, y1 - 14)), label, fill=(255, 255, 0), font=FONT)
        im.save(PREVIEW / f"{name}_{idx:02d}.jpg", quality=88)
    print(f"[preview] {name}: {PREVIEW}")


def mahjong_sheets(names, per_class=8):
    by_class = defaultdict(list)
    for split in SPLITS:
        idir, ldir = MAHJONG_DIR / split / "images", MAHJONG_DIR / split / "labels"
        for img in image_files(idir):
            for (c, x, y, w, h) in read_labels(ldir / f"{img.stem}.txt") or []:
                by_class[c].append((img, x, y, w, h))
    random.seed(7)
    for c in by_class:
        random.shuffle(by_class[c])
        cell = 100
        sheet = Image.new("RGB", (per_class * cell, cell + 22), (25, 25, 25))
        dr = ImageDraw.Draw(sheet)
        dr.text((3, 3), f"class {c}: {names[c] if names and c < len(names) else '?'}", fill=(0, 255, 0), font=FONT)
        for k, (img, x, y, w, h) in enumerate(by_class[c][:per_class]):
            im = Image.open(img).convert("RGB")
            W, H = im.size
            crop = im.crop((max(0, (x - w / 2) * W), max(0, (y - h / 2) * H),
                            min(W, (x + w / 2) * W), min(H, (y + h / 2) * H)))
            side = max(crop.size)
            cv = Image.new("RGB", (side, side), (0, 0, 0))
            cv.paste(crop, ((side - crop.width) // 2, (side - crop.height) // 2))
            sheet.paste(cv.resize((cell, cell), Image.LANCZOS), (k * cell, 22))
        sheet.save(SHEETS / f"class_{c:02d}.png")
    print(f"[sheets] {SHEETS}")


def main():
    poker = audit("poker", POKER_DIR, POKER_NAMES)
    mj = audit("mahjong", MAHJONG_DIR, None)
    (REPORT / "audit_raw.json").write_text(json.dumps({"poker": poker, "mahjong": mj},
                                                      ensure_ascii=False, indent=2), encoding="utf-8")
    for st in (poker, mj):
        print(f"\n=== {st['dataset']} ===")
        for s, v in st["splits"].items():
            print(f"  {s:5s}: {v['images']:5d} imgs, {v['boxes']:6d} boxes")
        print("  issues:", st["issues"] or "none", "classes:", len(st["classes"]))
    draw_preview("poker", POKER_DIR, POKER_NAMES)
    draw_preview("mahjong", MAHJONG_DIR, None)
    mahjong_sheets(None)


if __name__ == "__main__":
    main()
