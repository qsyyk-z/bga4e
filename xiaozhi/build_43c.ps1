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

# Build/flash with IDF first. After a successful flash, also write the app to
# the other OTA slot: this board's OTA data can point at ota_1, so flashing
# only ota_0 would leave the old UI running.
function Write-OtherOtaSlot {
    param([string[]]$IdfArgs)

    $pIndex = [Array]::IndexOf($IdfArgs, "-p")
    if ($pIndex -lt 0) { $pIndex = [Array]::IndexOf($IdfArgs, "--port") }
    if ($pIndex -lt 0 -or $pIndex + 1 -ge $IdfArgs.Count) {
        Write-Host "WARN: no -p/--port given; skipped ota_1 mirror write"
        return
    }

    $port = $IdfArgs[$pIndex + 1]
    $app = Join-Path $PSScriptRoot "build\\AiXiaoZhi_Hiwonder.bin"
    $esptool = Join-Path $env:IDF_PATH "components\\esptool_py\\esptool\\esptool.py"
    if (-not (Test-Path $app)) {
        Write-Host "WARN: app image not found: $app"
        return
    }

    Write-Host "Writing app to ota_1 (0x840000) so both OTA slots match..."
    & $python $esptool -p $port -b 460800 --before default_reset --after hard_reset write_flash 0x840000 $app
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: ota_1 mirror write failed"
        exit $LASTEXITCODE
    }
}

& $python $idfPy @args
$idfExit = $LASTEXITCODE
if ($idfExit -eq 0 -and ($args -contains "flash")) {
    Write-OtherOtaSlot -IdfArgs $args
}
exit $idfExit
