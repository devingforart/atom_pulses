param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration

$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$cli = Join-Path $projectRoot "build/$preset/$Configuration/pulso_cli.exe"
foreach ($role in @('bass', 'drums', 'counter')) {
    Write-Host "--- $role ---"
    & $cli $role 42 0
    if ($LASTEXITCODE -ne 0) { throw "CLI smoke test failed for $role" }
}

