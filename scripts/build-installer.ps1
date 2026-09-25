param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$CertificatePath = '',
    [string]$CertificatePassword = '',
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
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

$signTool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if ($CertificatePath) {
    if (-not $signTool) { throw 'signtool.exe is required for Authenticode signing.' }
    if (-not (Test-Path -LiteralPath $CertificatePath)) { throw "Certificate not found: $CertificatePath" }
    foreach ($file in @($pluginBinary, $standalone)) {
        & $signTool sign /fd SHA256 /td SHA256 /tr $TimestampUrl /f $CertificatePath /p $CertificatePassword $file
        if ($LASTEXITCODE -ne 0) { throw "Signing failed: $file" }
        & $signTool verify /pa /v $file
        if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $file" }
    }
}

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe', 'C:\Program Files\Inno Setup 6\ISCC.exe') |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 is not installed. Install it with: winget install JRSoftware.InnoSetup' }
$dist = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
& $iscc "/DAppVersion=$version" "/DSourceRoot=$projectRoot" "/DOutputRoot=$dist" (Join-Path $projectRoot 'installer\PULSO.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$installer = Join-Path $dist "PULSO-$version-windows-x64-setup.exe"
if ($CertificatePath) {
    & $signTool sign /fd SHA256 /td SHA256 /tr $TimestampUrl /f $CertificatePath /p $CertificatePassword $installer
    if ($LASTEXITCODE -ne 0) { throw 'Installer signing failed.' }
}
$hash = Get-FileHash -LiteralPath $installer -Algorithm SHA256
[IO.File]::WriteAllText("$installer.sha256", "$($hash.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($installer))`n", [Text.UTF8Encoding]::new($false))
Write-Host "Created $installer" -ForegroundColor Green
