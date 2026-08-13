param(
    [string]$LiveResources = "C:\ProgramData\Ableton\Live 12 Suite\Resources"
)

$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "..\ableton\PulsoDeployRemote"
$targetRoot = Join-Path $LiveResources "MIDI Remote Scripts"
$target = Join-Path $targetRoot "PulsoDeployRemote"

if (-not (Test-Path -LiteralPath $source)) { throw "Bridge source not found: $source" }
if (-not (Test-Path -LiteralPath $targetRoot)) { throw "Ableton Remote Scripts folder not found: $targetRoot" }

New-Item -ItemType Directory -Force -Path $target | Out-Null
Get-ChildItem -LiteralPath $source -Filter "*.py" -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $target -Force
}
Get-ChildItem -LiteralPath $target -File | Select-Object Name, Length, LastWriteTime

Write-Host "Installed PulsoDeployRemote. Restart Live, then choose it in Settings > Link, Tempo & MIDI > Control Surface."
