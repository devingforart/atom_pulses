param([ValidateRange(1,65535)][int]$Port = 4313)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$worker = Join-Path $projectRoot 'build\expanded-ensemble-bin\pulso_cloud_worker.exe'
if (!(Test-Path -LiteralPath $worker)) { throw 'Primero compila el worker de reparto ampliado.' }
if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
    throw "El puerto $Port ya esta ocupado; no se interrumpio ningun estudio."
}
$env:PULSO_LOCAL_PORT = [string]$Port
$env:PULSO_LOCAL_WORKER_PATH = $worker
$env:PULSO_LOCAL_DATA_DIR = Join-Path $projectRoot 'build\expanded-ensemble-studio-data'
$env:PULSO_LOCAL_EXPERIMENTAL = '0'
$env:PULSO_AI_CLUB_ENSEMBLE_SCENES = '1'
$configuredKey = [Environment]::GetEnvironmentVariable('OPENAI_API_KEY', 'User')
if ($configuredKey) { $env:OPENAI_API_KEY = $configuredKey }
node (Join-Path $projectRoot 'web\local-studio\server.mjs')
