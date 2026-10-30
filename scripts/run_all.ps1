# 一键训练流程（PowerShell，在项目根目录执行）
# 路径含中文时建议先建 ASCII 目录联接：
#   cmd /c mklink /J D:\cardrecog "D:\大学\制造工程体验"
#   cd D:\cardrecog\M08\bga4e

$py   = ".\.venv\Scripts\python.exe"
$py37 = ".\.venv37\Scripts\python.exe"

# 0. 环境自检
& $py scripts\00_check_env.py

# 1. 数据清洗 + 扑克类别重映射 + 裁剪（约 3-5 分钟）
& $py scripts\10_prepare_dataset.py --workers 8

# 2. 训练 2 类检测器（RTX 4060 约 20 分钟）
& $py scripts\12_train_detector.py --epochs 40 --imgsz 640 --batch 16 --workers 8 --cache ram --project runs\det

# 3. 训练两个分类器
& $py scripts\13_train_classifier.py --domain mahjong --epochs 35
& $py scripts\13_train_classifier.py --domain poker   --epochs 45

# 3b. 如果训练中断但 best.pt 可用：评估 + 导出
# & $py scripts\19_eval_export.py --domain mahjong

# 4. 模型辅助补标注（生成 datasets/det_auto）
& $py scripts\14_autolabel.py

# 5. 端到端评估
& $py scripts\15_eval_pipeline.py --domains mahjong,poker --conf 0.35

# 6. int8 量化（ONNX Runtime，PC 验证）
& $py scripts\16_quantize_onnx.py --domain mahjong
& $py scripts\16_quantize_onnx.py --domain poker

# 7. ESP-DL 量化（Python 3.7；导出 ESP32-S3 系数）
& $py37 scripts\21_quantize_espdl.py --domain mahjong
& $py37 scripts\21_quantize_espdl.py --domain poker

# 8. 汇总指标 / 导出部署包
& $py scripts\18_summary.py
& $py scripts\20_export_espdl_package.py --domain mahjong
& $py scripts\20_export_espdl_package.py --domain poker
