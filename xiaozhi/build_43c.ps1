# 在 PowerShell 中直接运行 ESP-IDF 的辅助脚本。
# 用法：
#   .\build_43c.ps1 --version
#   .\build_43c.ps1 fullclean
#   .\build_43c.ps1 build
#   .\build_43c.ps1 -p COM8 flash monitor

$ErrorActionPreference = "Stop"

# 本机 ESP-IDF 5.4.4 安装位置；换机器时改这三行
$env:IDF_PATH = "e:\esp\v5.4.4\esp-idf"
$env:IDF_TOOLS_PATH = "e:\.espressif"
$env:IDF_PYTHON_ENV_PATH = "e:\.espressif\python_env\idf5.4_py3.11_env"

Set-Location $PSScriptRoot

$idfPy = Join-Path $env:IDF_PATH "tools\idf.py"
$python = Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe"

if (-not (Test-Path $python)) {
    Write-Error "找不到 ESP-IDF Python：$python"
}
if (-not (Test-Path $idfPy)) {
    Write-Error "找不到 idf.py：$idfPy"
}

& $python $idfPy @args
exit $LASTEXITCODE
