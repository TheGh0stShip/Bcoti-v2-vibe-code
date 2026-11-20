# Run from an activated ESP-IDF 5.3.1 PowerShell, or supply IDF_PATH and IDF_PYTHON_ENV_PATH.
$ErrorActionPreference = 'Stop'
if (!$env:IDF_PATH -or !(Test-Path -LiteralPath "$env:IDF_PATH/tools/idf.py")) {
    throw 'Activate ESP-IDF v5.3.1 first. See docs/BUILD.md.'
}
$BcotiPython = if ($env:IDF_PYTHON_ENV_PATH -and (Test-Path -LiteralPath "$env:IDF_PYTHON_ENV_PATH/Scripts/python.exe")) {
    Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
} else { (Get-Command python -ErrorAction Stop).Source }
$BcotiVersion = & $BcotiPython "$env:IDF_PATH/tools/idf.py" --version
if ($LASTEXITCODE -ne 0 -or $BcotiVersion -notmatch 'v5\.3\.1(?:\s|$)') {
    throw "Expected clean ESP-IDF v5.3.1; found: $BcotiVersion"
}
