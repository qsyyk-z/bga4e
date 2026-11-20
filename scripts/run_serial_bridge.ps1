param(
    [string]$Port = "COM9",
    [int]$Player = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $root "backend")

$python = Join-Path (Get-Location) ".venv\Scripts\python.exe"
if (-not (Test-Path $python)) {
    throw "backend/.venv 不存在，先运行 scripts\run_backend.ps1 或手动创建虚拟环境。"
}

& $python -u tools\serial_game_bridge.py --port $Port --player $Player --echo
