# ESP-IDF 5.4.4 helper for this project.
# Usage:
#   .\build_43c.ps1 --version
#   .\build_43c.ps1 fullclean
#   .\build_43c.ps1 build
#   .\build_43c.ps1 -p COM8 flash monitor

$ErrorActionPreference = "Continue"

$env:IDF_PATH = "e:\esp\v5.4.4\esp-idf"
$env:IDF_TOOLS_PATH = "e:\.espressif"
$env:IDF_PYTHON_ENV_PATH = "e:\.espressif\python_env\idf5.4_py3.11_env"

# Add the toolchain/ninja/cmake that this IDF installation uses.
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
