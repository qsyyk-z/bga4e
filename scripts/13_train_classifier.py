#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
13_train_classifier.py
======================
训练牌面分类器（麻将 42 类 / 扑克 52 类），轻量深度可分离 CNN，便于 ESP-DL 量化部署。

模型只使用 ESP-DL 原生支持的算子：
    Conv2D(3x3/1x1) + BatchNorm(推理时折叠) + ReLU + GlobalAvgPool + FullyConnected

用法：
    python scripts/13_train_classifier.py --domain mahjong --epochs 35
    python scripts/13_train_classifier.py --domain poker   --epochs 45
"""
import argparse
import json
import random
import sys
import time
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

import numpy as np
import torch
import torch.nn as nn
from PIL import Image
from torch.utils.data import DataLoader, WeightedRandomSampler
from torchvision import transforms as T
from torchvision.datasets import ImageFolder

from cardnet import CardNet

ROOT = Path(__file__).absolute().parents[1]
CLS = ROOT / "datasets" / "cls"
MODELS = ROOT / "models"
RUNS = ROOT / "runs" / "cls"
INPUT = 96
MEAN = (0.5, 0.5, 0.5)
STD = (0.5, 0.5, 0.5)
FILL = (114, 114, 114)


class RandomRotate:
    """牌可能横放/倒放：麻将做 90° 倍数旋转，扑克做连续旋转。"""

    def __init__(self, mode="quad", p=0.5, fill=FILL):
        self.mode, self.p, self.fill = mode, p, fill

    def __call__(self, img):
        if random.random() > self.p:
            return img
        if self.mode == "full":
            ang = random.uniform(-180, 180)
        else:
            ang = random.choice([0, 90, 180, 270]) + random.uniform(-12, 12)
        return img.rotate(ang, resample=Image.BILINEAR, expand=False, fillcolor=self.fill)


def build_transforms(domain):
    rot = RandomRotate("full" if domain == "poker" else "quad",
                       p=0.75 if domain == "poker" else 0.55)
    train_tf = T.Compose([
        rot,
        T.RandomResizedCrop(INPUT, scale=(0.62, 1.0), ratio=(0.80, 1.25),
                            interpolation=T.InterpolationMode.BILINEAR),
        T.RandomPerspective(distortion_scale=0.12, p=0.30, fill=FILL),
        T.ColorJitter(brightness=0.35, contrast=0.35, saturation=0.30, hue=0.06),
        T.ToTensor(),
        T.Normalize(MEAN, STD),
        T.RandomErasing(p=0.15, scale=(0.02, 0.08), value="random"),
    ])
    val_tf = T.Compose([T.Resize(112), T.CenterCrop(INPUT), T.ToTensor(), T.Normalize(MEAN, STD)])
    return train_tf, val_tf


def evaluate(model, loader, device):
    model.eval()
    n = len(loader.dataset)
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
            k = min(3, nc)
            _, idx = out.topk(k, dim=1)
            top1 += (idx[:, 0] == y).sum().item()
            top3 += (idx == y[:, None]).any(dim=1).sum().item()
            for t, p in zip(y.cpu().numpy(), out.argmax(1).cpu().numpy()):
                cm[t, p] += 1
    return {"loss": loss_sum / max(n, 1), "top1": top1 / max(n, 1),
            "top3": top3 / max(n, 1), "cm": cm}


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
    ap.add_argument("--epochs", type=int, default=35)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--lr", type=float, default=3e-3)
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--width", type=float, default=1.0)
    ap.add_argument("--device", default="cuda")
    ap.add_argument("--fast", action="store_true")
    args = ap.parse_args()

    random.seed(42)
    np.random.seed(42)
    torch.manual_seed(42)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(42)
        torch.backends.cudnn.benchmark = True
    device = torch.device(args.device if torch.cuda.is_available() else "cpu")

    maps = json.loads((ROOT / "datasets" / "meta" / "class_maps.json").read_text(encoding="utf-8"))
    class_defs = maps[args.domain]["classes"]
    n_classes = maps[args.domain]["num_classes"]
    names = [c["name"] for c in class_defs]

    domain_dir = CLS / args.domain
    train_tf, val_tf = build_transforms(args.domain)
    train_ds = ImageFolder(domain_dir / "train", transform=train_tf)
    val_ds = ImageFolder(domain_dir / "valid", transform=val_tf)
    test_ds = ImageFolder(domain_dir / "test", transform=val_tf)
    assert len(train_ds.classes) == n_classes, (len(train_ds.classes), n_classes)
    assert len(val_ds.classes) == n_classes

    if args.fast:
        train_ds.samples = train_ds.samples[:2000]
        train_ds.targets = [s[1] for s in train_ds.samples]
        val_ds.samples = val_ds.samples[:500]
        val_ds.targets = [s[1] for s in val_ds.samples]

    labels = np.array([s[1] for s in train_ds.samples])
    counts = np.bincount(labels, minlength=n_classes).astype(np.float64)
    w = 1.0 / np.sqrt(np.maximum(counts, 1))
    sw = w[labels]
    sampler = WeightedRandomSampler(torch.from_numpy(sw).double(), num_samples=len(labels), replacement=True)

    train_loader = DataLoader(train_ds, batch_size=args.batch, sampler=sampler,
                              num_workers=args.workers, pin_memory=True, drop_last=True)
    val_loader = DataLoader(val_ds, batch_size=args.batch, shuffle=False,
                            num_workers=args.workers, pin_memory=True)
    test_loader = DataLoader(test_ds, batch_size=args.batch, shuffle=False,
                             num_workers=args.workers, pin_memory=True)

    model = CardNet(n_classes, width=args.width).to(device)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"[{args.domain}] train={len(train_ds)} val={len(val_ds)} test={len(test_ds)} "
          f"classes={n_classes} params={n_params/1e6:.2f}M device={device}")

    crit = nn.CrossEntropyLoss(label_smoothing=0.1)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=max(args.epochs, 1), eta_min=1e-5)
    scaler = torch.amp.GradScaler("cuda", enabled=device.type == "cuda")

    out_dir = MODELS / f"classifier_{args.domain}"
    out_dir.mkdir(parents=True, exist_ok=True)
    (RUNS / args.domain).mkdir(parents=True, exist_ok=True)

    best_acc, best_epoch, bad = 0.0, -1, 0
    history = []
    t0 = time.time()
    for epoch in range(args.epochs):
        model.train()
        loss_sum, seen, correct = 0.0, 0, 0
        for x, y in train_loader:
            x, y = x.to(device, non_blocking=True), y.to(device, non_blocking=True)
            opt.zero_grad(set_to_none=True)
            with torch.amp.autocast("cuda", enabled=device.type == "cuda"):
                out = model(x)
                loss = crit(out, y)
            scaler.scale(loss).backward()
            scaler.step(opt)
            scaler.update()
            loss_sum += loss.item() * y.size(0)
            correct += (out.argmax(1) == y).sum().item()
            seen += y.size(0)
        sched.step()

        vm = evaluate(model, val_loader, device)
        rec = {"epoch": epoch + 1, "train_loss": round(loss_sum / max(seen, 1), 4),
               "train_acc": round(correct / max(seen, 1), 4), "val_loss": round(vm["loss"], 4),
               "val_top1": round(vm["top1"], 4), "val_top3": round(vm["top3"], 4),
               "lr": round(opt.param_groups[0]["lr"], 6), "elapsed_s": round(time.time() - t0, 1)}
        history.append(rec)
        print(f"  ep{epoch+1:02d}/{args.epochs} train {rec['train_acc']:.3f} "
              f"val {rec['val_top1']:.3f} top3 {rec['val_top3']:.3f} loss {rec['val_loss']:.3f}")

        if vm["top1"] > best_acc:
            best_acc, best_epoch, bad = vm["top1"], epoch + 1, 0
            torch.save({"model": model.state_dict(), "n_classes": n_classes, "width": args.width,
                        "epoch": epoch + 1, "val_top1": vm["top1"]}, out_dir / "best.pt")
        else:
            bad += 1
            if bad >= 12:
                print("  early stop")
                break

    (out_dir / "history.json").write_text(json.dumps(history, ensure_ascii=False, indent=2), encoding="utf-8")

    ckpt = torch.load(out_dir / "best.pt", map_location=device, weights_only=False)
    model.load_state_dict(ckpt["model"])
    test_m = evaluate(model, test_loader, device)
    report = per_class_report(test_m["cm"], names)
    plot_confusion(test_m["cm"], names, out_dir / "confusion_test.png",
                   f"{args.domain} test top1={test_m['top1']:.3f}")

    metrics = {
        "domain": args.domain, "num_classes": n_classes, "input_size": INPUT, "params": n_params,
        "best_epoch": best_epoch, "best_val_top1": round(best_acc, 4),
        "test_top1": round(test_m["top1"], 4), "test_top3": round(test_m["top3"], 4),
        "test_loss": round(test_m["loss"], 4), "test_samples": len(test_ds),
        "per_class": report, "history": history,
    }
    (out_dir / "metrics.json").write_text(json.dumps(metrics, ensure_ascii=False, indent=2), encoding="utf-8")

    classes = {"domain": args.domain, "num_classes": n_classes, "input_size": INPUT,
               "mean": MEAN, "std": STD, "classes": class_defs}
    (out_dir / "classes.json").write_text(json.dumps(classes, ensure_ascii=False, indent=2), encoding="utf-8")

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

    print(f"[{args.domain}] test top1={test_m['top1']:.4f} top3={test_m['top3']:.4f} -> {out_dir}")
    print("worst classes:")
    for r in sorted(report, key=lambda r: r["recall"])[:8]:
        print(f"  {r['name']:6s} recall={r['recall']:.3f} n={r['support']}")


if __name__ == "__main__":
    main()
