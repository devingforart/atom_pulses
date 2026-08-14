param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$version = '0.30.0'
$packageName = "PULSO-$version-windows-x64"
$distRoot = Join-Path $projectRoot 'dist'
$stage = Join-Path $distRoot $packageName
$archive = Join-Path $distRoot "$packageName.zip"
$vstSource = Join-Path $projectRoot "build/$preset/Pulso_artefacts/$Configuration/VST3/PULSO.vst3"
$appSource = Join-Path $projectRoot "build/$preset/Pulso_artefacts/$Configuration/Standalone/PULSO.exe"

foreach ($required in @($vstSource, $appSource)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing build artifact: $required" }
}

if (-not (Test-Path -LiteralPath $distRoot)) {
    New-Item -ItemType Directory -Path $distRoot | Out-Null
}

if (Test-Path -LiteralPath $stage) {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
    $resolvedDist = (Resolve-Path -LiteralPath $distRoot).Path
    if (-not $resolvedStage.StartsWith($resolvedDist, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected staging path: $resolvedStage"
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }

New-Item -ItemType Directory -Path (Join-Path $stage 'VST3') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'Standalone') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'Documentation') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'AbletonBridge') -Force | Out-Null
Copy-Item -LiteralPath $vstSource -Destination (Join-Path $stage 'VST3\PULSO.vst3') -Recurse
Copy-Item -LiteralPath $appSource -Destination (Join-Path $stage 'Standalone\PULSO.exe')
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE.md') -Destination (Join-Path $stage 'LICENSE.md')
Copy-Item -Path (Join-Path $projectRoot 'docs\*.md') -Destination (Join-Path $stage 'Documentation')
Copy-Item -LiteralPath (Join-Path $projectRoot 'ableton\PulsoDeployRemote') -Destination (Join-Path $stage 'AbletonBridge\PulsoDeployRemote') -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts\install-ableton-bridge.ps1') -Destination (Join-Path $stage 'AbletonBridge\install-ableton-bridge.ps1')

Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$hash = Get-FileHash -LiteralPath $archive -Algorithm SHA256
$hashLine = "$($hash.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($archive))`n"
[System.IO.File]::WriteAllText("$archive.sha256", $hashLine)
Write-Host "Created $archive" -ForegroundColor Green
Write-Host "SHA256 $($hash.Hash)"
