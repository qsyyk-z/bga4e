#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
19_eval_export.py
=================
对已训练好的分类器 checkpoint 做「测试集评估 + classes.json + 混淆矩阵 + ONNX 导出」。

适用场景：训练被中断、但 best.pt 已经可用；或只想重新评估/导出。

用法：
    python scripts/19_eval_export.py --domain mahjong
    python scripts/19_eval_export.py --domain poker --ckpt models/classifier_poker/best.pt
"""
import argparse
import json
import sys
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torchvision import transforms as T
from torchvision.datasets import ImageFolder

from cardnet import CardNet

ROOT = Path(__file__).absolute().parents[1]
CLS = ROOT / "datasets" / "cls"
MODELS = ROOT / "models"
INPUT = 96
MEAN = (0.5, 0.5, 0.5)
STD = (0.5, 0.5, 0.5)


def val_tf():
    return T.Compose([T.Resize(112), T.CenterCrop(INPUT), T.ToTensor(), T.Normalize(MEAN, STD)])


def evaluate(model, loader, device):
    model.eval()
    nc = model.fc.out_features
    cm = np.zeros((nc, nc), dtype=np.int64)
    top1 = top3 = 0
    loss_sum = 0.0
    crit = nn.CrossEntropyLoss()
    with torch.no_grad():
        for x, y in loader:
            x, y = x.to(device), y.to(device)
            out = model(x)
            loss_sum += crit(out, y).item() * y.size(0)
            _, idx = out.topk(min(3, nc), dim=1)
            top1 += (idx[:, 0] == y).sum().item()
            top3 += (idx == y[:, None]).any(dim=1).sum().item()
            for t, p in zip(y.cpu().numpy(), out.argmax(1).cpu().numpy()):
                cm[t, p] += 1
    n = max(len(loader.dataset), 1)
    return {"loss": loss_sum / n, "top1": top1 / n, "top3": top3 / n, "cm": cm}


def per_class_report(cm, names):
    rows = []
    for i, name in enumerate(names):
        tp = cm[i, i]
        fp = cm[:, i].sum() - tp
        fn = cm[i, :].sum() - tp
        prec = tp / max(tp + fp, 1)
        rec = tp / max(tp + fn, 1)
        f1 = 2 * prec * rec / max(prec + rec, 1e-9)
        rows.append({"idx": i, "name": name, "support": int(cm[i].sum()),
                     "precision": round(float(prec), 4), "recall": round(float(rec), 4),
                     "f1": round(float(f1), 4)})
    return rows


def plot_confusion(cm, names, out_path, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    matplotlib.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
    matplotlib.rcParams["axes.unicode_minus"] = False
    n = len(names)
    fig, ax = plt.subplots(figsize=(max(10, n * 0.30), max(8, n * 0.30)))
    norm = cm / np.maximum(cm.sum(1, keepdims=True), 1)
    im = ax.imshow(norm, cmap="Blues", vmin=0, vmax=1)
    ax.set_xticks(range(n))
    ax.set_yticks(range(n))
    ax.set_xticklabels(names, rotation=90, fontsize=6)
    ax.set_yticklabels(names, fontsize=6)
    ax.set_xlabel("predicted")
    ax.set_ylabel("true")
    ax.set_title(title)
    fig.colorbar(im, ax=ax, fraction=0.046)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--domain", choices=["mahjong", "poker"], required=True)
    ap.add_argument("--ckpt", default=None)
    ap.add_argument("--batch", type=int, default=256)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--device", default="cuda")
    args = ap.parse_args()

    device = torch.device(args.device if torch.cuda.is_available() else "cpu")
    out_dir = MODELS / f"classifier_{args.domain}"
    ckpt_path = Path(args.ckpt) if args.ckpt else out_dir / "best.pt"
    ckpt = torch.load(ckpt_path, map_location=device, weights_only=False)

    maps = json.loads((ROOT / "datasets" / "meta" / "class_maps.json").read_text(encoding="utf-8"))
    class_defs = maps[args.domain]["classes"]
    names = [c["name"] for c in class_defs]
    n_classes = maps[args.domain]["num_classes"]

    model = CardNet(n_classes, width=ckpt.get("width", 1.0)).to(device)
    model.load_state_dict(ckpt["model"])
    n_params = sum(p.numel() for p in model.parameters())

    tf = val_tf()
    test_ds = ImageFolder(CLS / args.domain / "test", transform=tf)
    test_loader = DataLoader(test_ds, batch_size=args.batch, shuffle=False, num_workers=args.workers, pin_memory=True)
    print(f"[{args.domain}] test samples={len(test_ds)} classes={len(test_ds.classes)} ckpt={ckpt_path}")

    m = evaluate(model, test_loader, device)
    report = per_class_report(m["cm"], names)
    plot_confusion(m["cm"], names, out_dir / "confusion_test.png",
                   f"{args.domain} test top1={m['top1']:.3f}")

    metrics = {
        "domain": args.domain, "num_classes": n_classes, "input_size": INPUT, "params": n_params,
        "best_epoch": ckpt.get("epoch"), "best_val_top1": ckpt.get("val_top1"),
        "test_top1": round(m["top1"], 4), "test_top3": round(m["top3"], 4),
        "test_loss": round(m["loss"], 4), "test_samples": len(test_ds), "per_class": report,
    }
    (out_dir / "classes.json").write_text(json.dumps({
        "domain": args.domain, "num_classes": n_classes, "input_size": INPUT,
        "mean": MEAN, "std": STD, "classes": class_defs}, ensure_ascii=False, indent=2), encoding="utf-8")

    model.eval()
    dummy = torch.randn(1, 3, INPUT, INPUT, device=device)
    onnx_path = out_dir / "classifier.onnx"
    torch.onnx.export(model, dummy, str(onnx_path), input_names=["input"], output_names=["logits"],
                      opset_version=12, do_constant_folding=True)
    try:
        import onnxruntime as ort
        sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
        o = sess.run(None, {"input": dummy.cpu().numpy()})[0]
        same = int((o.argmax(1) == model(dummy).argmax(1).detach().cpu().numpy()).sum())
        metrics["onnx_check"] = {"argmax_match": same, "out_shape": list(o.shape)}
    except Exception as e:
        metrics["onnx_check"] = {"error": str(e)}

    (out_dir / "metrics.json").write_text(json.dumps(metrics, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"[{args.domain}] test top1={m['top1']:.4f} top3={m['top3']:.4f}")
    print("worst classes:")
    for r in sorted(report, key=lambda r: r["recall"])[:10]:
        print(f"  {r['name']:6s} recall={r['recall']:.3f} n={r['support']}")


if __name__ == "__main__":
    main()
