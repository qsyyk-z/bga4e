#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
18_summary.py
=============
汇总所有训练/评估结果，生成 report/metrics_summary.md 与 report/metrics_summary.json。

用法：
    python scripts/18_summary.py
"""
import json
import sys
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

ROOT = Path(__file__).absolute().parents[1]
MODELS = ROOT / "models"
REPORT = ROOT / "report"


def load(p, default=None):
    p = Path(p)
    if p.exists():
        return json.loads(p.read_text(encoding="utf-8"))
    return default


def main():
    det = load(MODELS / "detector" / "metrics.json", {})
    mj = load(MODELS / "classifier_mahjong" / "metrics.json", {})
    pk = load(MODELS / "classifier_poker" / "metrics.json", {})
    pipe = load(REPORT / "pipeline_metrics.json", {})
    auto = load(REPORT / "autolabel.json", {})

    out = {"detector": det, "classifier_mahjong": mj, "classifier_poker": pk,
           "pipeline": pipe, "autolabel": auto.get("stats") if auto else {}}
    (REPORT / "metrics_summary.json").write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")

    lines = ["# 指标汇总", ""]
    if det:
        m = det.get("metrics", {})
        lines += ["## 检测器", "",
                  f"- mAP50: **{m.get('metrics/mAP50(B)', 0):.4f}**",
                  f"- mAP50-95: **{m.get('metrics/mAP50-95(B)', 0):.4f}**",
                  f"- Precision: {m.get('metrics/precision(B)', 0):.4f}",
                  f"- Recall: {m.get('metrics/recall(B)', 0):.4f}",
                  f"- 每类: {json.dumps(det.get('per_class', {}), ensure_ascii=False)}", ""]
    for name, d in (("麻将", mj), ("扑克", pk)):
        if d:
            lines += [f"## {name}分类器", "",
                      f"- val top-1: {d.get('best_val_top1', 0):.4f}",
                      f"- test top-1: **{d.get('test_top1', 0):.4f}**",
                      f"- test top-3: {d.get('test_top3', 0):.4f}",
                      f"- 参数量: {d.get('params', 0)/1e6:.3f} M",
                      f"- int8: {d.get('quantization', {})}", ""]
    if pipe:
        lines += ["## 端到端", "", "```json", json.dumps(pipe.get("overall", {}), ensure_ascii=False, indent=2), "```", ""]
    if out["autolabel"]:
        lines += ["## 自动补标注", "", "```json",
                  json.dumps(out["autolabel"], ensure_ascii=False, indent=2), "```", ""]

    text = "\n".join(lines)
    (REPORT / "metrics_summary.md").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
