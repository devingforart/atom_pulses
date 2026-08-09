param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Destination = "$env:LOCALAPPDATA\Programs\Common\VST3"
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$source = Join-Path $projectRoot "build/$preset/Pulso_artefacts/$Configuration/VST3/PULSO.vst3"

if (-not (Test-Path -LiteralPath $source)) {
    throw "Plugin not found at $source. Run scripts/build.ps1 first."
}
if (-not (Test-Path -LiteralPath $Destination)) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
}

$target = Join-Path $Destination 'PULSO.vst3'
if (Test-Path -LiteralPath $target) {
    $resolvedTarget = (Resolve-Path -LiteralPath $target).Path
    $resolvedDestination = (Resolve-Path -LiteralPath $Destination).Path
    $expectedTarget = Join-Path $resolvedDestination 'PULSO.vst3'
    if (-not $resolvedTarget.Equals($expectedTarget, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace unexpected path: $resolvedTarget"
    }
    Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}
Copy-Item -LiteralPath $source -Destination $target -Recurse -Force
Write-Host "Installed PULSO at $target" -ForegroundColor Green
