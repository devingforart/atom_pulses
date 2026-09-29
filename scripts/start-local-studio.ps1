param([ValidateRange(1, 65535)][int]$Port = 4177)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$serverPath = Join-Path $projectRoot 'web\local-studio\server.mjs'
$defaultWorkerPath = Join-Path $projectRoot 'build-cloud\Release\pulso_cloud_worker.exe'
$workerPath = if ($env:PULSO_LOCAL_WORKER_PATH) { $env:PULSO_LOCAL_WORKER_PATH } else { $defaultWorkerPath }

if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
    throw 'No se encontró web/local-studio/server.mjs.'
}
if (-not (Test-Path -LiteralPath $workerPath -PathType Leaf)) {
    throw 'No se encontró el worker existente. Esta herramienta no compila ni ejecuta CMake.'
}
if (-not $env:OPENAI_API_KEY) {
    $env:OPENAI_API_KEY = [Environment]::GetEnvironmentVariable('OPENAI_API_KEY', 'User')
}
if (-not $env:OPENAI_API_KEY) {
    Write-Warning 'OPENAI_API_KEY no está configurada. La página abrirá, pero no podrá componer.'
}

$env:PULSO_LOCAL_PORT = [string]$Port
node $serverPath
