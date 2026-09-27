param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
$match = [regex]::Match($cmake, 'project\(Pulso\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $match.Success) { throw 'Unable to read product version.' }
$version = $match.Groups[1].Value
$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$artifactRoot = Join-Path $projectRoot "build\$preset\Pulso_artefacts\$Configuration"
$pluginBinary = Join-Path $artifactRoot 'VST3\PULSO.vst3\Contents\x86_64-win\PULSO.vst3'
$standalone = Join-Path $artifactRoot 'Standalone\PULSO.exe'
foreach ($file in @($pluginBinary, $standalone)) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing artifact: $file" }
}

$makensis = @(
    'C:\Program Files (x86)\NSIS\makensis.exe',
    'C:\Program Files\NSIS\makensis.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs\NSIS\makensis.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $makensis) { throw 'NSIS is required. Install it with: winget install NSIS.NSIS' }

$dist = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
& $makensis '/INPUTCHARSET' 'UTF8' "/DAPP_VERSION=$version" "/DSOURCE_ROOT=$projectRoot" "/DARTIFACT_ROOT=$artifactRoot" "/DOUTPUT_ROOT=$dist" (Join-Path $projectRoot 'installer\PULSO.nsi')
if ($LASTEXITCODE -ne 0) { throw 'NSIS compilation failed.' }
$installer = Join-Path $dist "PULSO-$version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installer)) { throw "NSIS did not create $installer" }
$hash = Get-FileHash -LiteralPath $installer -Algorithm SHA256
[IO.File]::WriteAllText("$installer.sha256", "$($hash.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($installer))`n", [Text.UTF8Encoding]::new($false))
Write-Host "Created unsigned beta installer $installer" -ForegroundColor Green
