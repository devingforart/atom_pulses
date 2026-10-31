param(
    [ValidateRange(1, 65535)][int]$Port = 4177,
    [ValidateSet('Recovered', 'BestRated', 'Experimental')][string]$Engine = 'Recovered'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$anchorSource = Join-Path $projectRoot 'build\anchor-harmonic-review-source'
$anchorWorker = Join-Path $projectRoot 'build\anchor-harmonic-review-build\Release\pulso_cloud_worker.exe'
if ($Engine -eq 'Recovered') {
    $serverPath = Join-Path $projectRoot 'web\local-studio\server.mjs'
    $workerPath = Join-Path $projectRoot 'build\recovered-club-controls-build\Release\pulso_cloud_worker.exe'
    $env:PULSO_LOCAL_EXPERIMENTAL = '0'
    $receiptPath = Join-Path $projectRoot 'build\recovered-club-controls-build\worker-receipt.json'
    if (-not (Test-Path -LiteralPath $receiptPath)) { throw 'Falta el comprobante del worker integrado.' }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath $workerPath -Algorithm SHA256).Hash -ne $receipt.sha256) {
        throw 'El worker integrado no coincide con el binario verificado.'
    }
} elseif ($Engine -eq 'BestRated') {
    & (Join-Path $PSScriptRoot 'prepare-best-rated-engine.ps1') | Out-Host
    $serverPath = Join-Path $anchorSource 'web\local-studio\server.mjs'
    $workerPath = $anchorWorker
    $env:PULSO_LOCAL_EXPERIMENTAL = '1'
    $env:PULSO_AI_JOINT_ONE_GROUP = '1'
} else {
    $serverPath = Join-Path $projectRoot 'web\local-studio\server.mjs'
    $defaultWorkerPath = Join-Path $projectRoot 'build-cloud\Release\pulso_cloud_worker.exe'
    $workerPath = if ($env:PULSO_LOCAL_WORKER_PATH) { $env:PULSO_LOCAL_WORKER_PATH } else { $defaultWorkerPath }
}

if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
    throw 'No se encontró web/local-studio/server.mjs.'
}
if (-not (Test-Path -LiteralPath $workerPath -PathType Leaf)) {
    throw 'No se encontró el worker existente. Esta herramienta no compila ni ejecuta CMake.'
}
$configuredUserKey = [Environment]::GetEnvironmentVariable('OPENAI_API_KEY', 'User')
if ($configuredUserKey) { $env:OPENAI_API_KEY = $configuredUserKey }
if (-not $env:OPENAI_API_KEY) {
    Write-Warning 'OPENAI_API_KEY no está configurada. La página abrirá, pero no podrá componer.'
}

$env:PULSO_LOCAL_PORT = [string]$Port
$env:PULSO_LOCAL_WORKER_PATH = $workerPath
$env:PULSO_LOCAL_DATA_DIR = Join-Path $projectRoot 'build\local-studio-data'
node $serverPath
