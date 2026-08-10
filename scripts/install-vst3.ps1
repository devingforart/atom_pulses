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

    # Windows permite borrar parte del bundle antes de fallar sobre un DLL cargado.
    # Comprobamos el bloqueo primero para dejar siempre la instalación anterior intacta.
    $installedBinary = Join-Path $resolvedTarget 'Contents\x86_64-win\PULSO.vst3'
    if (Test-Path -LiteralPath $installedBinary) {
        try {
            $handle = [System.IO.File]::Open($installedBinary,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None)
            $handle.Dispose()
        } catch {
            throw "PULSO is loaded by Ableton or another host. Close the host and run the installer again."
        }
    }
    Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}
Copy-Item -LiteralPath $source -Destination $target -Recurse -Force
Write-Host "Installed PULSO at $target" -ForegroundColor Green
