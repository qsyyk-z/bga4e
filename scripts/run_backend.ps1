param(
    [int]$Port = 8000
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $root "backend")

$python = Join-Path (Get-Location) ".venv\Scripts\python.exe"
if (-not (Test-Path $python)) {
    Write-Host "创建 backend/.venv ..."
    python -m venv .venv
    & $python -m pip install -r requirements.txt
}

Write-Host "后端 http://127.0.0.1:$Port"
& $python -m uvicorn app.main:app --host 0.0.0.0 --port $Port
