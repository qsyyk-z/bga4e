#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
10_prepare_dataset.py
=====================
把原始「扑克 / 麻将」两个 YOLO 数据集清洗、重映射、合并成：

1. datasets/det/                              —— 2 类检测数据集（0=麻将牌, 1=扑克牌）
2. datasets/cls/<domain>/<split>/<class_id>/  —— 分类裁剪图（112x112）
3. datasets/meta/*.json                       —— 清洗报告、类别统计、裁剪索引

清洗规则（可在 CONFIG 中调整）
------------------------------
* 扑克类别按 class_maps.json 的 src_to_idx 重映射；源类别 19（仅 1 个 J♦ 噪声框）丢弃，
  源类别 20（脏类 59，实为 10♦）合并进 10♦。
* 坐标越界、宽高非正、过小、过大（整排/多张牌粘连）、长宽比异常、完全重复的框删除。
* 图片与标签一一对应；没有标签文件的图片跳过并计数。
* 分类裁剪：框外扩 10% 作为上下文，补成正方形后缩放到 112x112（训练时随机裁剪到 96）。

用法：
    python scripts/10_prepare_dataset.py [--workers 8] [--skip-cls]
"""
import argparse
import csv
import json
import shutil
from collections import Counter, defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).absolute().parents[1]
WORKSPACE = ROOT.parents[1]
RAW = {
    "mahjong": WORKSPACE / "麻将" / "mahjong_yolo_real",
    "poker": WORKSPACE / "扑克" / "poker_yolo_real",
}
OUT = ROOT / "datasets"
DET = OUT / "det"
CLS = OUT / "cls"
META = OUT / "meta"

SPLITS = ("train", "valid", "test")
IMG_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
PREFIX = {"mahjong": "mj", "poker": "pk"}
DET_CLASS = {"mahjong": 0, "poker": 1}

CONFIG = {
    "mahjong": {"min_area": 6e-4, "max_area": 0.05, "min_ar": 0.22, "max_ar": 1.55},
    "poker": {"min_area": 8e-3, "max_area": 0.45, "min_ar": 0.30, "max_ar": 1.80},
}

Image.MAX_IMAGE_PIXELS = None


def load_maps():
    return json.loads((META / "class_maps.json").read_text(encoding="utf-8"))


def read_labels(path: Path):
    rows = []
    if not path.exists():
        return rows
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        s = line.split()
        if len(s) < 5:
            continue
        try:
            rows.append((int(float(s[0])), float(s[1]), float(s[2]), float(s[3]), float(s[4])))
        except ValueError:
            continue
    return rows


def clean(domain: str, rows, maps, drops: Counter):
    cfg = CONFIG[domain]
    if domain == "poker":
        remap = {int(k): v for k, v in maps["poker"]["src_to_idx"].items()}
    else:
        remap = {i: i for i in range(maps["mahjong"]["num_classes"])}

    kept, seen = [], set()
    for (src_cls, x, y, w, h) in rows:
        if src_cls not in remap:
            drops["class_unmapped"] += 1
            continue
        new_cls = remap[src_cls]
        if new_cls is None:
            drops["class_dropped_noise"] += 1
            continue
        if not (0.0 <= x <= 1.0 and 0.0 <= y <= 1.0 and 0.0 < w <= 1.0 and 0.0 < h <= 1.0):
            drops["bbox_out_of_range"] += 1
            continue
        area = w * h
        if area < cfg["min_area"]:
            drops["too_small"] += 1
            continue
        if area > cfg["max_area"]:
            drops["too_large_or_merged"] += 1
            continue
        ar = w / max(h, 1e-9)
        if ar < cfg["min_ar"] or ar > cfg["max_ar"]:
            drops["bad_aspect"] += 1
            continue
        key = (new_cls, round(x, 4), round(y, 4), round(w, 4), round(h, 4))
        if key in seen:
            drops["duplicate"] += 1
            continue
        seen.add(key)
        kept.append((new_cls, x, y, w, h))
    return kept


def list_images(split_dir: Path):
    return sorted(p for p in split_dir.iterdir() if p.suffix.lower() in IMG_EXTS)


def build_detector(maps, annotations, report):
    if DET.exists():
        shutil.rmtree(DET)
    for split in SPLITS:
        (DET / "images" / split).mkdir(parents=True, exist_ok=True)
        (DET / "labels" / split).mkdir(parents=True, exist_ok=True)

    for domain, root in RAW.items():
        for split in SPLITS:
            idir, ldir = root / split / "images", root / split / "labels"
            n_img = n_box = n_kept = 0
            for img in list_images(idir):
                n_img += 1
                rows = read_labels(ldir / f"{img.stem}.txt")
                n_box += len(rows)
                kept = clean(domain, rows, maps, report["drops"][domain])
                if not kept:
                    continue
                name = f"{PREFIX[domain]}_{img.stem}"
                shutil.copy2(img, DET / "images" / split / f"{name}.jpg")
                lines = [f"{DET_CLASS[domain]} {x:.6f} {y:.6f} {w:.6f} {h:.6f}" for (c, x, y, w, h) in kept]
                (DET / "labels" / split / f"{name}.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
                n_kept += len(kept)
                annotations[domain][split].append({
                    "image": str(img),
                    "det_image": str(DET / "images" / split / f"{name}.jpg"),
                    "name": name,
                    "boxes": [[c, x, y, w, h] for (c, x, y, w, h) in kept],
                })
            report["det"][domain][split] = {"images": n_img, "boxes_raw": n_box, "boxes_kept": n_kept}
            print(f"[det] {domain:8s} {split:5s}: imgs={n_img:5d} raw={n_box:6d} kept={n_kept:6d}")

    yaml_text = (
        f"path: {DET.as_posix()}\n"
        "train: images/train\n"
        "val: images/valid\n"
        "test: images/test\n"
        "names:\n"
        "  0: mahjong_tile\n"
        "  1: poker_card\n"
    )
    (DET / "data.yaml").write_text(yaml_text, encoding="utf-8")


def crop_one_image(args):
    domain, split, task_id, img_path, boxes = args
    img = Image.open(img_path).convert("RGB")
    W, H = img.size
    out = []
    for k, (cls, x, y, w, h) in enumerate(boxes):
        pad = 0.10
        x1 = max(0, (x - w / 2 - w * pad) * W)
        y1 = max(0, (y - h / 2 - h * pad) * H)
        x2 = min(W, (x + w / 2 + w * pad) * W)
        y2 = min(H, (y + h / 2 + h * pad) * H)
        if x2 - x1 < 4 or y2 - y1 < 4:
            continue
        crop = img.crop((x1, y1, x2, y2))
        side = max(crop.size)
        canvas = Image.new("RGB", (side, side), (114, 114, 114))
        canvas.paste(crop, ((side - crop.width) // 2, (side - crop.height) // 2))
        canvas = canvas.resize((112, 112), Image.LANCZOS)
        fname = f"{task_id:05d}_{k:02d}.jpg"
        dst_dir = CLS / domain / split / f"{cls:02d}"
        dst_dir.mkdir(parents=True, exist_ok=True)
        canvas.save(dst_dir / fname, quality=92)
        out.append((f"{domain}/{split}/{cls:02d}/{fname}", domain, split, cls, str(img_path),
                    round(x, 6), round(y, 6), round(w, 6), round(h, 6)))
    return out


def build_classifier(maps, annotations, workers):
    if CLS.exists():
        shutil.rmtree(CLS)
    for domain in ("mahjong", "poker"):
        for split in SPLITS:
            (CLS / domain / split).mkdir(parents=True, exist_ok=True)

    for split in SPLITS:
        tasks = []
        tid = 0
        for domain in ("mahjong", "poker"):
            for rec in annotations[domain][split]:
                tasks.append((domain, split, tid, Path(rec["image"]), rec["boxes"]))
                tid += 1
        rows = []
        with ProcessPoolExecutor(max_workers=workers) as ex:
            futures = [ex.submit(crop_one_image, t) for t in tasks]
            done = 0
            for f in as_completed(futures):
                rows.extend(f.result())
                done += 1
                if done % 500 == 0:
                    print(f"[cls] {split}: {done}/{len(tasks)} images, {len(rows)} crops")
        with (CLS / f"index_{split}.csv").open("w", newline="", encoding="utf-8") as fp:
            w = csv.writer(fp)
            w.writerow(["crop", "domain", "split", "cls", "source_image", "x", "y", "w", "h"])
            for r in sorted(rows):
                w.writerow(r)
        print(f"[cls] {split}: {len(rows)} crops")

        stats = defaultdict(lambda: defaultdict(int))
        for r in rows:
            stats[r[1]][r[3]] += 1
        for domain in ("mahjong", "poker"):
            n = maps[domain]["num_classes"]
            print(f"  {domain:8s} {split:5s} " + " ".join(f"{i}:{stats[domain][i]}" for i in range(n)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--skip-cls", action="store_true")
    args = ap.parse_args()

    META.mkdir(parents=True, exist_ok=True)
    maps = load_maps()
    report = {"drops": {d: Counter() for d in ("mahjong", "poker")},
              "det": {d: {} for d in ("mahjong", "poker")}}
    annotations = {d: {s: [] for s in SPLITS} for d in ("mahjong", "poker")}

    build_detector(maps, annotations, report)
    (META / "annotations_clean.json").write_text(json.dumps(annotations, ensure_ascii=False), encoding="utf-8")

    if not args.skip_cls:
        build_classifier(maps, annotations, args.workers)

    cls_count = {d: Counter() for d in ("mahjong", "poker")}
    for d in ("mahjong", "poker"):
        for s in SPLITS:
            for rec in annotations[d][s]:
                for (c, *_rest) in rec["boxes"]:
                    cls_count[d][(s, c)] += 1

    report_out = {
        "drops": {d: dict(report["drops"][d]) for d in report["drops"]},
        "det": report["det"],
        "class_counts": {
            d: {str(i): {s: cls_count[d][(s, i)] for s in SPLITS} for i in range(maps[d]["num_classes"])}
            for d in ("mahjong", "poker")
        },
    }
    (META / "clean_report.json").write_text(json.dumps(report_out, ensure_ascii=False, indent=2), encoding="utf-8")
    print("\n=== drops ===")
    for d, c in report["drops"].items():
        print(d, dict(c))
    print("\n[ok] datasets/det, datasets/cls, datasets/meta")


if __name__ == "__main__":
    main()
