param(
    [string]$VstDestination = "$env:LOCALAPPDATA\Programs\Common\VST3",
    [string]$LiveResources = "C:\ProgramData\Ableton\Live 12 Suite\Resources"
)

$ErrorActionPreference = 'Stop'
$packageRoot = $PSScriptRoot
$vstSource = Join-Path $packageRoot 'VST3\PULSO.vst3'
$bridgeSource = Join-Path $packageRoot 'AbletonBridge\PulsoDeployRemote'
$vstTarget = Join-Path $VstDestination 'PULSO.vst3'
$remoteRoot = Join-Path $LiveResources 'MIDI Remote Scripts'
$bridgeTarget = Join-Path $remoteRoot 'PulsoDeployRemote'

if (-not (Test-Path -LiteralPath $vstSource)) { throw "Missing packaged VST3: $vstSource" }
if (-not (Test-Path -LiteralPath $bridgeSource)) { throw "Missing packaged control surface: $bridgeSource" }
if (-not (Test-Path -LiteralPath $remoteRoot)) {
    throw "Ableton Remote Scripts directory was not found: $remoteRoot"
}

New-Item -ItemType Directory -Force -Path $VstDestination | Out-Null
if (Test-Path -LiteralPath $vstTarget) {
    $installedBinary = Join-Path $vstTarget 'Contents\x86_64-win\PULSO.vst3'
    if (Test-Path -LiteralPath $installedBinary) {
        try {
            $handle = [System.IO.File]::Open($installedBinary,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None)
            $handle.Dispose()
        } catch {
            $cause = $_.Exception
            while ($null -ne $cause.InnerException) { $cause = $cause.InnerException }
            if ($cause -is [System.UnauthorizedAccessException]) {
                throw "The installer cannot modify the current PULSO DLL. Run it with permission to write to: $vstTarget"
            }
            if ($cause -is [System.IO.IOException]) {
                throw 'PULSO is loaded by Ableton or another host. Close it and run this installer again.'
            }
            throw "Unable to verify the current PULSO DLL: $($cause.Message)"
        }
    }
    Remove-Item -LiteralPath $vstTarget -Recurse -Force
}
Copy-Item -LiteralPath $vstSource -Destination $vstTarget -Recurse -Force

New-Item -ItemType Directory -Force -Path $bridgeTarget | Out-Null
Get-ChildItem -LiteralPath $bridgeSource -Filter '*.py' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $bridgeTarget -Force
}

Write-Host "Installed PULSO VST3 at $vstTarget" -ForegroundColor Green
Write-Host "Installed PulsoDeployRemote at $bridgeTarget" -ForegroundColor Green
Write-Host 'Restart Ableton Live, select PulsoDeployRemote, then use SET UP AI inside PULSO.'
