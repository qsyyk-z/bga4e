#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
12_train_detector.py
====================
训练 2 类牌面检测器（0 = 麻将牌，1 = 扑克牌），基于 YOLOv8n。

用法（在项目根目录）：
    python scripts/12_train_detector.py --epochs 50 --imgsz 640 --batch 16
    python scripts/12_train_detector.py --data datasets/det_auto/data.yaml --model runs/det/yolov8n_2cls/weights/best.pt --epochs 15 --name yolov8n_auto
"""
import argparse
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).absolute().parents[1]
DATA = ROOT / "datasets" / "det" / "data.yaml"
RUNS = ROOT / "runs"
MODELS = ROOT / "models" / "detector"
PRETRAINED = ROOT / "models" / "pretrained" / "yolov8n.pt"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=str(DATA))
    ap.add_argument("--model", default="yolov8n.pt")
    ap.add_argument("--epochs", type=int, default=50)
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--device", default="0")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--patience", type=int, default=20)
    ap.add_argument("--name", default="yolov8n_2cls")
    ap.add_argument("--project", default=str(RUNS / "det"))
    ap.add_argument("--cache", default="false", choices=["disk", "ram", "false"])
    ap.add_argument("--resume", action="store_true")
    args = ap.parse_args()
    cache = False if args.cache == "false" else args.cache

    from ultralytics import YOLO

    MODELS.mkdir(parents=True, exist_ok=True)
    Path(args.project).mkdir(parents=True, exist_ok=True)

    weights = args.model
    if weights == "yolov8n.pt" and PRETRAINED.exists():
        weights = str(PRETRAINED)

    model = YOLO(weights)
    model.train(
        data=str(args.data),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        device=args.device,
        workers=args.workers,
        project=str(args.project),
        name=args.name,
        exist_ok=True,
        patience=args.patience,
        cache=cache,
        amp=True,
        plots=True,
        seed=42,
        deterministic=False,
        degrees=8.0,
        translate=0.10,
        scale=0.55,
        shear=2.0,
        perspective=0.0005,
        flipud=0.0,
        fliplr=0.5,
        mosaic=1.0,
        mixup=0.05,
        hsv_h=0.015,
        hsv_s=0.5,
        hsv_v=0.4,
        resume=args.resume,
        verbose=True,
    )

    run_dir = Path(args.project) / args.name
    best = run_dir / "weights" / "best.pt"
    shutil.copy2(best, MODELS / "best.pt")
    shutil.copy2(run_dir / "weights" / "last.pt", MODELS / "last.pt")

    # 测试集评估
    model = YOLO(str(best))
    metrics = model.val(data=str(args.data), split="test", imgsz=args.imgsz,
                        batch=args.batch, device=args.device, plots=True,
                        project=str(args.project), name=f"{args.name}_test", exist_ok=True)
    summary = {
        "weights": str(best),
        "imgsz": args.imgsz,
        "epochs": args.epochs,
        "metrics": {k: float(v) for k, v in metrics.results_dict.items()},
        "per_class": {},
        "speed_ms": {k: float(v) for k, v in metrics.speed.items()},
    }
    names = metrics.names
    for i, c in enumerate(metrics.box.ap_class_index):
        summary["per_class"][names[int(c)]] = {
            "mAP50": float(metrics.box.ap50[i]),
            "mAP50-95": float(metrics.box.ap[i]),
        }

    # 导出 ONNX：320 用于端侧，640 作为高精度参考
    onnx320 = model.export(format="onnx", imgsz=320, opset=12, simplify=True, dynamic=False)
    shutil.copy2(onnx320, MODELS / "best_320.onnx")
    onnx640 = model.export(format="onnx", imgsz=640, opset=12, simplify=True, dynamic=False)
    shutil.copy2(onnx640, MODELS / "best_640.onnx")
    summary["onnx_320"] = str(MODELS / "best_320.onnx")
    summary["onnx_640"] = str(MODELS / "best_640.onnx")

    (MODELS / "metrics.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
