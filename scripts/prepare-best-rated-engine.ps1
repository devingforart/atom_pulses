param([switch]$Rebuild)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$tag = 'candidate-club-2026-10-24'
$source = Join-Path $projectRoot 'build\anchor-best-rated-source'
$build = Join-Path $projectRoot 'build\anchor-best-rated-build'
$worker = Join-Path $build 'Release\pulso_cloud_worker.exe'
$juce = Join-Path $projectRoot 'build\cloud-harmonic-phrase\_deps\juce-src'
$expected = (& git -C $projectRoot rev-parse "$tag^{}" 2>&1).Trim()
if ($LASTEXITCODE -ne 0 -or $expected -notmatch '^[0-9a-f]{40}$') {
    throw "No se encontro el anclaje Git $tag."
}
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    & git -C $projectRoot worktree add --detach $source $tag
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo recuperar el anclaje sin tocar el arbol de trabajo actual.' }
}
$actual = (& git -C $source rev-parse HEAD 2>&1).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) {
    throw "El codigo aislado no coincide con $tag. No se modifico ni borro nada."
}
if ($Rebuild -or -not (Test-Path -LiteralPath $worker -PathType Leaf)) {
    if (-not (Test-Path -LiteralPath $juce -PathType Container)) {
        throw 'Falta la dependencia JUCE local. Configurala antes de compilar el motor.'
    }
    & cmake -S $source -B $build -G 'Visual Studio 17 2022' -A x64 `
        -DPULSO_BUILD_PLUGIN=OFF -DPULSO_BUILD_CLI=OFF -DPULSO_BUILD_TESTS=OFF `
        -DPULSO_BUILD_CLOUD_WORKER=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=$($juce.Replace('\','/'))"
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo configurar el motor recuperado.' }
    & cmake --build $build --config Release --target pulso_cloud_worker --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar el motor recuperado.' }
}
Write-Output "Motor principal local listo: $actual"
Write-Output "Worker: $worker"
