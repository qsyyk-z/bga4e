# 指标汇总

## 检测器

- mAP50: **0.9866**
- mAP50-95: **0.7931**
- Precision: 0.9934
- Recall: 0.9756
- 每类: {"mahjong_tile": {"mAP50": 0.9781996318771172, "mAP50-95": 0.7562349530307645}, "poker_card": {"mAP50": 0.994981884057971, "mAP50-95": 0.8299825029463499}}

## 麻将分类器

- val top-1: 0.9689
- test top-1: **0.9753**
- test top-3: 0.9962
- 参数量: 0.329 M
- int8: {'int8_top1': 0.9745, 'fp32_top1_onnx': 0.9753, 'int8_size_kb': 360.9, 'fp32_size_kb': 1282.2}

## 扑克分类器

- val top-1: 0.8002
- test top-1: **0.7866**
- test top-3: 0.9638
- 参数量: 0.334 M
- int8: {'int8_top1': 0.7794, 'fp32_top1_onnx': 0.7866, 'int8_size_kb': 366.0, 'fp32_size_kb': 1302.3}

## 端到端

```json
{
  "gt": 2903,
  "e2e_correct": 2650,
  "e2e_acc": 0.9128,
  "det_recall": 0.9666
}
```

## 自动补标注

```json
{
  "mahjong_recovered_images": 457,
  "mahjong_gt_matched": 44638,
  "mahjong_gt": 45330,
  "mahjong_gt_missed": 692,
  "mahjong_added": 823,
  "poker_gt_matched": 4978,
  "poker_gt": 4998,
  "poker_gt_missed": 20,
  "poker_added": 2
}
```
