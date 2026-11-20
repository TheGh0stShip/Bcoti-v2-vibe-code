$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Use-Idf.ps1"
$BcotiProject = Join-Path (Split-Path $PSScriptRoot -Parent) 'Esp32_Files/ESP_IDF_Project'
& $BcotiPython "$env:IDF_PATH/tools/idf.py" -C $BcotiProject build
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
Write-Output 'Build complete. Nothing was flashed. Read docs/FLASHING.md before writing a device.'
