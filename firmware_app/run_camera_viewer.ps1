# Start the local USB camera preview page (no board SoftAP).
# Usage: .\run_camera_viewer.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$python = "e:\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"
if (-not (Test-Path $python)) {
    $python = "python"
}
& $python (Join-Path $root "backend\tools\camera_usb_viewer.py") `
    --serial COM10 --http-port 8080 --open
