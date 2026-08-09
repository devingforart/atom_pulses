param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$CoreOnly,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$buildDirectory = Join-Path $projectRoot "build/$preset"

# Some automation environments inject both `Path` and `PATH`. MSBuild treats those
# as duplicate keys and fails before launching cl.exe, so normalize the process env.
$cleanPath = $env:Path
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
[System.Environment]::SetEnvironmentVariable('Path', $null, [System.EnvironmentVariableTarget]::Process)
[System.Environment]::SetEnvironmentVariable('Path', $cleanPath, [System.EnvironmentVariableTarget]::Process)

if ($Clean -and (Test-Path -LiteralPath $buildDirectory)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDirectory).Path
    $resolvedRoot = (Resolve-Path -LiteralPath $projectRoot).Path
    if (-not $resolvedBuild.StartsWith((Join-Path $resolvedRoot 'build'), [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected path: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmakePath) {
    $candidate = 'C:\Program Files\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $candidate) { $cmakePath = $candidate }
}
if (-not $cmakePath) { throw 'CMake was not found. Install it and open a new PowerShell session.' }
$ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'

Push-Location $projectRoot
try {
    if ($CoreOnly) {
        $coreBuild = Join-Path $projectRoot 'build/core'
        & $cmakePath -S $projectRoot -B $coreBuild -DPULSO_BUILD_PLUGIN=OFF
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
        & $cmakePath --build $coreBuild --config $Configuration
        if ($LASTEXITCODE -ne 0) { throw "Core build failed with exit code $LASTEXITCODE" }
        & $ctestPath --test-dir $coreBuild -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Core tests failed with exit code $LASTEXITCODE" }
    } else {
        & $cmakePath --preset $preset
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
        & $cmakePath --build --preset $preset --parallel
        if ($LASTEXITCODE -ne 0) { throw "Plugin build failed with exit code $LASTEXITCODE" }
        & $ctestPath --preset $preset
        if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE" }
    }
} finally {
    Pop-Location
}

Write-Host "PULSO $Configuration build completed successfully." -ForegroundColor Green
