param(
    [string]$Port = "COM9"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $root "xiaozhi")

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "找不到 idf.py。请先在 ESP-IDF 5.4.4 终端里运行 export.ps1 (或 export.bat)。"
}

idf.py -p $Port build flash
