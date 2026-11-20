param(
    [Parameter(Mandatory=$true)][ValidatePattern('^COM[0-9]+$')][string]$Port,
    [ValidateSet(115200,460800)][int]$Baud = 460800,
    [string]$OutputDirectory
)
# Windows convenience wrapper. The Python implementation also supports Linux/macOS.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Use-Idf.ps1"
$BcotiArgs = @('--port', $Port, '--baud', "$Baud")
if ($OutputDirectory) { $BcotiArgs += @('--output', $OutputDirectory) }
& $BcotiPython "$PSScriptRoot/backup_device.py" @BcotiArgs
if ($LASTEXITCODE -ne 0) { throw 'Backup failed or layout differed. Do not flash; preserve partial backup.' }
