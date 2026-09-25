param([string]$OutputDirectory = "$env:USERPROFILE\Desktop")

$ErrorActionPreference = 'Stop'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) "pulso-support-$stamp"
$archive = Join-Path $OutputDirectory "PULSO-support-$stamp.zip"
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
try {
    $abletonEditions = @('Suite', 'Standard', 'Intro', 'Lite')
    $bridgeInstalled = $abletonEditions | Where-Object {
        Test-Path (Join-Path $env:ProgramData "Ableton\Live 12 $_\Resources\MIDI Remote Scripts\PulsoDeployRemote")
    }
    $system = [ordered]@{
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        windows = [Environment]::OSVersion.VersionString
        architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        culture = [Globalization.CultureInfo]::CurrentCulture.Name
        vst3Installed = Test-Path "$env:CommonProgramFiles\VST3\PULSO.vst3"
        abletonBridgeInstalled = [bool]$bridgeInstalled
        abletonEditions = @($abletonEditions | Where-Object {
            Test-Path (Join-Path $env:ProgramData "Ableton\Live 12 $_\Resources\MIDI Remote Scripts")
        })
    }
    $system | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $tempRoot 'system.json') -Encoding utf8
    $journalRoot = Join-Path $env:LOCALAPPDATA 'PULSO'
    if (Test-Path -LiteralPath $journalRoot) {
        Get-ChildItem -LiteralPath $journalRoot -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -in '.log','.txt','.json' -and $_.Name -notmatch 'key|token|credential|secret' } |
            ForEach-Object {
                $content = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
                $sanitized = $content -replace 'sk-[A-Za-z0-9_-]{12,}', '[REDACTED_API_KEY]' -replace '(?i)(bearer\s+)[A-Za-z0-9._~-]+', '$1[REDACTED]'
                [IO.File]::WriteAllText((Join-Path $tempRoot $_.Name), $sanitized, [Text.UTF8Encoding]::new($false))
            }
    }
    Compress-Archive -LiteralPath $tempRoot -DestinationPath $archive -CompressionLevel Optimal
    Write-Host "Support bundle created: $archive" -ForegroundColor Green
    Write-Host 'Review the ZIP before sharing it. API keys and tokens are intentionally excluded.'
} finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}
