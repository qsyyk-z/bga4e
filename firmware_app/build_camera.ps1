# Build/flash the DNESP32S3 OV5640 camera firmware with ESP-IDF 5.4.4.
# Usage:
#   .\build_camera.ps1 build
#   .\build_camera.ps1 -p COM10 flash
#   .\build_camera.ps1 -p COM10 flash monitor
#
# If COM10 says "Write timeout", put the camera board into download mode
# manually before flashing: unplug USB, hold BOOT, plug USB, release BOOT.

$ErrorActionPreference = "Continue"

$env:IDF_PATH = "e:\esp\v5.4.4\esp-idf"
$env:IDF_TOOLS_PATH = "e:\.espressif"
$env:IDF_PYTHON_ENV_PATH = "e:\.espressif\python_env\idf5.4_py3.11_env"

$toolPaths = @(
    "e:\.espressif\tools\ninja\1.12.1",
    "e:\.espressif\tools\cmake\3.30.2\bin",
    "e:\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
)
$env:PATH = ($toolPaths -join ";") + ";" + $env:PATH

Set-Location $PSScriptRoot

$idfPy = Join-Path $env:IDF_PATH "tools\idf.py"
$python = Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe"

if (-not (Test-Path $python)) {
    Write-Host "ERROR: ESP-IDF Python not found: $python"
    exit 1
}
if (-not (Test-Path $idfPy)) {
    Write-Host "ERROR: idf.py not found: $idfPy"
    exit 1
}

& $python $idfPy @args
exit $LASTEXITCODE
