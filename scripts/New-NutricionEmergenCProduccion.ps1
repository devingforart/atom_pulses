param(
    [string]$SourcePath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'NUTRICION Emergen C - propuesta 9 semanas.xlsx'),
    [string]$OutputPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'NUTRICION Emergen C - produccion y terpenos.xlsx')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not (Test-Path -LiteralPath $SourcePath)) { throw "No existe $SourcePath" }
if (Test-Path -LiteralPath $OutputPath) { throw "El archivo de salida ya existe: $OutputPath" }

Copy-Item -LiteralPath $SourcePath -Destination $OutputPath
$zip = [System.IO.Compression.ZipFile]::Open($OutputPath, [System.IO.Compression.ZipArchiveMode]::Update)
try {
    $uri = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
    $entry = $zip.GetEntry('xl\worksheets\sheet1.xml')
    if (-not $entry) { throw 'Falta sheet1.xml' }
    $reader = [System.IO.StreamReader]::new($entry.Open())
    try { $sourceText = $reader.ReadToEnd() } finally { $reader.Dispose() }
    $xml = [System.Xml.XmlDocument]::new()
    $xml.PreserveWhitespace = $true
    $xml.LoadXml($sourceText)
    $ns = [System.Xml.XmlNamespaceManager]::new($xml.NameTable)
    $ns.AddNamespace('s', $uri)

    function Get-Cell([string]$reference) {
        $cell = $xml.SelectSingleNode("//s:c[@r='$reference']", $ns)
        if (-not $cell) { throw "Falta la celda $reference" }
        return $cell
    }

    function Clear-Cell([System.Xml.XmlElement]$cell) {
        while ($cell.HasChildNodes) { [void]$cell.RemoveChild($cell.FirstChild) }
        [void]$cell.RemoveAttribute('t')
    }

    function Set-Text([string]$reference, [string]$value) {
        $cell = Get-Cell $reference
        Clear-Cell $cell
        $cell.SetAttribute('t', 'inlineStr')
        $inline = $xml.CreateElement('is', $uri)
        $textNode = $xml.CreateElement('t', $uri)
        $textNode.InnerText = $value
        [void]$inline.AppendChild($textNode)
        [void]$cell.AppendChild($inline)
    }

    function Set-Number([string]$reference, [string]$value) {
        $cell = Get-Cell $reference
        Clear-Cell $cell
        $node = $xml.CreateElement('v', $uri)
        $node.InnerText = $value
        [void]$cell.AppendChild($node)
    }

    function Set-Formula([string]$reference, [string]$formula) {
        $cell = Get-Cell $reference
        Clear-Cell $cell
        $node = $xml.CreateElement('f', $uri)
        $node.InnerText = $formula
        [void]$cell.AppendChild($node)
    }

    # Dosis en gramos por 100 L: Calcinit, KNO3, Epsom, K2SO4, MKP.
    # Es una hipotesis moderada para equilibrar biomasa y calidad, no una curva
    # validada experimentalmente para Emergen C en coco.
    $profiles = @(
        @{ Row=15; Rates=@(76.9,29.8,39.0,9.2,20.1) },
        @{ Row=16; Rates=@(77.6,29.1,39.0,8.4,22.3) },
        @{ Row=17; Rates=@(74.9,28.4,38.5,8.1,20.1) },
        @{ Row=18; Rates=@(69.0,31.4,38.0,5.7,17.6) },
        @{ Row=19; Rates=@(66.4,30.7,37.5,5.6,16.7) },
        @{ Row=20; Rates=@(63.7,30.1,37.0,5.5,15.9) },
        @{ Row=21; Rates=@(61.1,29.4,36.5,5.4,15.4) },
        @{ Row=22; Rates=@(58.5,25.0,35.5,7.7,14.1) },
        @{ Row=23; Rates=@(55.8,20.7,35.5,9.8,13.2) }
    )
    $columns = @('E','F','G','H','I')
    foreach ($profile in $profiles) {
        for ($i = 0; $i -lt $columns.Count; $i++) {
            $rate = $profile.Rates[$i].ToString([System.Globalization.CultureInfo]::InvariantCulture)
            Set-Formula "$($columns[$i])$($profile.Row)" ('$B$3*({0}/100)' -f $rate)
        }
    }

    Set-Number 'B10' '0.25'
    Set-Text 'L11' 'EC medida al preparar (mS/cm)'
    foreach ($row in 12..23) { Clear-Cell (Get-Cell "L$row") }
    Set-Text 'D10' 'Agua real: 0,25 mS/cm. Anotar en L la EC medida tras preparar cada receta; no hay EC predicha. La receta anterior de semana 5 dio 1,35 mS/cm. No subir sales solo para llegar a 2,0.'
    Set-Text 'A24' 'Criterio nutricional'
    Set-Text 'B24' 'Produccion + calidad: N moderado y descendente, P/K sin picos, Mg sostenido. Hipotesis para Emergen C en coco; ajustar con planta, consumo y drenaje. Semanas florales 8-9 condicionales; sin lavado obligatorio. El sabor no se garantiza con fertilizacion.'
    Set-Text 'W16' 'Registrar EC real; no aumentar todas las sales para perseguir 2,0.'
    Set-Text 'W22' 'Si sigue engordando, sostener perfil anterior; no reducir por calendario.'
    Set-Text 'W23' 'No es lavado: continuar nutrientes si sigue creciendo; cosechar por madurez.'

    $entry.Delete()
    $newEntry = $zip.CreateEntry('xl\worksheets\sheet1.xml', [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = [System.IO.StreamWriter]::new($newEntry.Open(), [System.Text.UTF8Encoding]::new($false))
    try { $xml.Save($writer) } finally { $writer.Dispose() }

    $workbookEntry = $zip.GetEntry('xl\workbook.xml')
    if (-not $workbookEntry) { throw 'Falta workbook.xml' }
    $reader = [System.IO.StreamReader]::new($workbookEntry.Open())
    try { $workbookText = $reader.ReadToEnd() } finally { $reader.Dispose() }
    $workbook = [System.Xml.XmlDocument]::new()
    $workbook.PreserveWhitespace = $true
    $workbook.LoadXml($workbookText)
    $wbNs = [System.Xml.XmlNamespaceManager]::new($workbook.NameTable)
    $wbNs.AddNamespace('s', $uri)
    $calc = $workbook.SelectSingleNode('/s:workbook/s:calcPr', $wbNs)
    if (-not $calc) {
        $calc = $workbook.CreateElement('calcPr', $uri)
        [void]$workbook.DocumentElement.AppendChild($calc)
    }
    $calc.SetAttribute('calcMode', 'auto')
    $calc.SetAttribute('fullCalcOnLoad', '1')
    $calc.SetAttribute('forceFullCalc', '1')
    $workbookEntry.Delete()
    $newWorkbookEntry = $zip.CreateEntry('xl\workbook.xml', [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = [System.IO.StreamWriter]::new($newWorkbookEntry.Open(), [System.Text.UTF8Encoding]::new($false))
    try { $workbook.Save($writer) } finally { $writer.Dispose() }
} finally {
    $zip.Dispose()
}

Write-Output $OutputPath
