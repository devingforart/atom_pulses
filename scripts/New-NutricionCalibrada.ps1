param(
    [string]$SourcePath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'NUTRICION.xlsx'),
    [string]$OutputPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'NUTRICION - calibrada 1.70.xlsx'),
    [double]$Factor = 1.7,
    [double]$TankLiters = 50
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if ($Factor -le 0) { throw 'El factor debe ser mayor que cero.' }
if ($TankLiters -le 0) { throw 'Los litros reales deben ser mayores que cero.' }
if (-not (Test-Path -LiteralPath $SourcePath)) { throw "No existe $SourcePath" }
if (Test-Path -LiteralPath $OutputPath) { throw "El archivo de salida ya existe: $OutputPath" }

Copy-Item -LiteralPath $SourcePath -Destination $OutputPath
$zip = [System.IO.Compression.ZipFile]::Open($OutputPath, [System.IO.Compression.ZipArchiveMode]::Update)
try {
    $entryName = 'xl\worksheets\sheet1.xml'
    $entry = $zip.GetEntry($entryName)
    if (-not $entry) { throw "Falta $entryName" }
    $reader = [System.IO.StreamReader]::new($entry.Open())
    try { $xmlText = $reader.ReadToEnd() } finally { $reader.Dispose() }

    $xml = [System.Xml.XmlDocument]::new()
    $xml.PreserveWhitespace = $true
    $xml.LoadXml($xmlText)
    $nsUri = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
    $ns = [System.Xml.XmlNamespaceManager]::new($xml.NameTable)
    $ns.AddNamespace('s', $nsUri)
    $row10 = $xml.SelectSingleNode('//s:sheetData/s:row[@r="10"]', $ns)
    if (-not $row10) { throw 'Falta la fila de parametros.' }
    $tankCell = $xml.SelectSingleNode('//s:sheetData/s:row[@r="3"]/s:c[@r="B3"]/s:v', $ns)
    if (-not $tankCell) { throw 'Falta el volumen real del tanque en B3.' }
    $tankCell.InnerText = $TankLiters.ToString('0.###', [System.Globalization.CultureInfo]::InvariantCulture)

    function Add-CellCopy([string]$sourceRef, [string]$targetRef) {
        $source = $row10.SelectSingleNode("s:c[@r='$sourceRef']", $ns)
        if (-not $source) { throw "Falta la celda $sourceRef" }
        $copy = $source.CloneNode($true)
        $copy.SetAttribute('r', $targetRef)
        while ($copy.HasChildNodes) { [void]$copy.RemoveChild($copy.FirstChild) }
        [void]$row10.AppendChild($copy)
        return $copy
    }

    function Set-InlineText([System.Xml.XmlElement]$cell, [string]$value) {
        $cell.SetAttribute('t', 'inlineStr')
        $inline = $xml.CreateElement('is', $nsUri)
        $text = $xml.CreateElement('t', $nsUri)
        $text.InnerText = $value
        [void]$inline.AppendChild($text)
        [void]$cell.AppendChild($inline)
    }

    Set-InlineText (Add-CellCopy 'A10' 'E10') 'Factor sales principales'
    $factorCell = Add-CellCopy 'B10' 'F10'
    $factorCell.SetAttribute('t', 'n')
    $factorValue = $xml.CreateElement('v', $nsUri)
    $factorValue.InnerText = $Factor.ToString('0.###', [System.Globalization.CultureInfo]::InvariantCulture)
    [void]$factorCell.AppendChild($factorValue)
    Set-InlineText (Add-CellCopy 'D10' 'G10') '1,70 = sales para 85 L en 50 L reales. Micro C no se escala. Validar EC y drenaje; la EC no confirma cada ppm.'

    foreach ($row in 12..20) {
        foreach ($column in @('E', 'F', 'G', 'H', 'I')) {
            $formula = $xml.SelectSingleNode("//s:c[@r='$column$row']/s:f", $ns)
            if (-not $formula) { throw "Falta la formula en $column$row" }
            $formula.InnerText = "($($formula.InnerText))*`$F`$10"
        }
    }

    $entry.Delete()
    $newEntry = $zip.CreateEntry($entryName, [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = [System.IO.StreamWriter]::new($newEntry.Open(), [System.Text.UTF8Encoding]::new($false))
    try { $xml.Save($writer) } finally { $writer.Dispose() }

    $workbookName = 'xl\workbook.xml'
    $workbookEntry = $zip.GetEntry($workbookName)
    if (-not $workbookEntry) { throw "Falta $workbookName" }
    $reader = [System.IO.StreamReader]::new($workbookEntry.Open())
    try { $workbookText = $reader.ReadToEnd() } finally { $reader.Dispose() }
    $workbook = [System.Xml.XmlDocument]::new()
    $workbook.PreserveWhitespace = $true
    $workbook.LoadXml($workbookText)
    $workbookNs = [System.Xml.XmlNamespaceManager]::new($workbook.NameTable)
    $workbookNs.AddNamespace('s', $nsUri)
    $calc = $workbook.SelectSingleNode('/s:workbook/s:calcPr', $workbookNs)
    if (-not $calc) {
        $calc = $workbook.CreateElement('calcPr', $nsUri)
        [void]$workbook.DocumentElement.AppendChild($calc)
    }
    $calc.SetAttribute('fullCalcOnLoad', '1')
    $calc.SetAttribute('forceFullCalc', '1')
    $workbookEntry.Delete()
    $newWorkbookEntry = $zip.CreateEntry($workbookName, [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = [System.IO.StreamWriter]::new($newWorkbookEntry.Open(), [System.Text.UTF8Encoding]::new($false))
    try { $workbook.Save($writer) } finally { $writer.Dispose() }
} finally {
    $zip.Dispose()
}

Write-Output $OutputPath
