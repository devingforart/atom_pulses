param(
    [string]$SourcePath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'NUTRICION.xlsx'),
    [string]$OutputPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'NUTRICION Emergen C - propuesta 9 semanas.xlsx')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not (Test-Path -LiteralPath $SourcePath)) { throw "No existe $SourcePath" }
if (Test-Path -LiteralPath $OutputPath) { throw "El archivo de salida ya existe: $OutputPath" }

Copy-Item -LiteralPath $SourcePath -Destination $OutputPath
$zip = [System.IO.Compression.ZipFile]::Open($OutputPath, [System.IO.Compression.ZipArchiveMode]::Update)
try {
    $sheetPath = 'xl\worksheets\sheet1.xml'
    $entry = $zip.GetEntry($sheetPath)
    if (-not $entry) { throw "Falta $sheetPath" }
    $reader = [System.IO.StreamReader]::new($entry.Open())
    try { $sourceXml = $reader.ReadToEnd() } finally { $reader.Dispose() }

    $xml = [System.Xml.XmlDocument]::new()
    $xml.PreserveWhitespace = $true
    $xml.LoadXml($sourceXml)
    $uri = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
    $ns = [System.Xml.XmlNamespaceManager]::new($xml.NameTable)
    $ns.AddNamespace('s', $uri)
    $data = $xml.SelectSingleNode('/s:worksheet/s:sheetData', $ns)
    if (-not $data) { throw 'No se encontro sheetData' }

    function Set-Text([System.Xml.XmlElement]$cell, [string]$value) {
        while ($cell.HasChildNodes) { [void]$cell.RemoveChild($cell.FirstChild) }
        $cell.SetAttribute('t', 'inlineStr')
        $inline = $xml.CreateElement('is', $uri)
        $textNode = $xml.CreateElement('t', $uri)
        $textNode.InnerText = $value
        [void]$inline.AppendChild($textNode)
        [void]$cell.AppendChild($inline)
    }

    function Set-Number([System.Xml.XmlElement]$cell, [int]$value) {
        while ($cell.HasChildNodes) { [void]$cell.RemoveChild($cell.FirstChild) }
        $cell.SetAttribute('t', 'n')
        $v = $xml.CreateElement('v', $uri)
        $v.InnerText = [string]$value
        [void]$cell.AppendChild($v)
    }

    function Set-Formula([System.Xml.XmlElement]$cell, [string]$value) {
        while ($cell.HasChildNodes) { [void]$cell.RemoveChild($cell.FirstChild) }
        [void]$cell.RemoveAttribute('t')
        $formula = $xml.CreateElement('f', $uri)
        $formula.InnerText = $value
        [void]$cell.AppendChild($formula)
    }

    function Move-Row([System.Xml.XmlElement]$row, [int]$from, [int]$to) {
        $row.SetAttribute('r', [string]$to)
        foreach ($cell in $row.SelectNodes('s:c', $ns)) {
            $ref = $cell.GetAttribute('r')
            if ($ref -notmatch '^([A-Z]+)[0-9]+$') { throw "Referencia invalida: $ref" }
            $cell.SetAttribute('r', "$($Matches[1])$to")
            $formula = $cell.SelectSingleNode('s:f', $ns)
            if ($formula) {
                $formula.InnerText = [regex]::Replace(
                    $formula.InnerText,
                    "(?<![A-Z0-9])([`$]?[A-Z]{1,3}[`$]?)$from(?![0-9])",
                    { param($match) $match.Groups[1].Value + $to }
                )
            }
        }
    }

    $originalRows = @{}
    foreach ($row in $data.SelectNodes('s:row', $ns)) {
        $originalRows[[int]$row.GetAttribute('r')] = $row
    }
    foreach ($number in 12..20) {
        if (-not $originalRows.ContainsKey($number)) { throw "Falta la semana en fila $number" }
    }
    Set-Number ($originalRows[3].SelectSingleNode("s:c[@r='B3']", $ns)) 50

    foreach ($number in @($originalRows.Keys | Where-Object { $_ -ge 19 } | Sort-Object -Descending)) {
        $target = $number + 3
        Move-Row $originalRows[$number] $number $target
    }

    foreach ($target in 19..21) {
        $copy = $originalRows[18].CloneNode($true)
        Move-Row $copy 18 $target
        $week = $copy.SelectSingleNode("s:c[@r='A$target']", $ns)
        $phase = $copy.SelectSingleNode("s:c[@r='B$target']", $ns)
        $rule = $copy.SelectSingleNode("s:c[@r='W$target']", $ns)
        if (-not $week -or -not $phase -or -not $rule) { throw "Faltan celdas de control en fila $target" }
        Set-Number $week ($target - 11)
        Set-Text $phase 'Floracion media (ajuste gradual)'
        Set-Text $rule 'Mantener si hay crecimiento floral; revisar consumo, planta y drenaje antes de reducir.'
        $nextRow = $originalRows[19]
        [void]$data.InsertBefore($copy, $nextRow)
    }

    foreach ($pair in @(@(22,11), @(23,12))) {
        $row = $originalRows[$pair[0] - 3]
        $cell = $row.SelectSingleNode("s:c[@r='A$($pair[0])']", $ns)
        Set-Number $cell $pair[1]
    }
    $profiles = @(
        @{ Row=18; Rates=@(65.8,31.4,39.5,5.7,17.6); Phase='Floracion media 4'; Ec='1.5 - 1.9' },
        @{ Row=19; Rates=@(63.2,30.7,39.0,5.6,16.7); Phase='Floracion media 5'; Ec='1.5 - 1.9' },
        @{ Row=20; Rates=@(60.5,30.1,38.0,5.5,15.9); Phase='Floracion media 6'; Ec='1.5 - 1.9' },
        @{ Row=21; Rates=@(57.9,29.4,37.0,5.4,15.4); Phase='Floracion avanzada 7'; Ec='1.4 - 1.9' },
        @{ Row=22; Rates=@(55.3,25.0,36.0,7.7,14.1); Phase='Maduracion 8 (condicional)'; Ec='1.4 - 1.8' },
        @{ Row=23; Rates=@(52.6,20.7,35.5,9.8,13.2); Phase='Maduracion 9 (condicional)'; Ec='1.3 - 1.8' }
    )
    $columns = @('E','F','G','H','I')
    foreach ($profile in $profiles) {
        $row = $data.SelectSingleNode("s:row[@r='$($profile.Row)']", $ns)
        for ($i = 0; $i -lt $columns.Count; $i++) {
            $cell = $row.SelectSingleNode("s:c[@r='$($columns[$i])$($profile.Row)']", $ns)
            $rate = $profile.Rates[$i].ToString([System.Globalization.CultureInfo]::InvariantCulture)
            Set-Formula $cell ('$B$3*({0}/100)' -f $rate)
        }
        Set-Text ($row.SelectSingleNode("s:c[@r='B$($profile.Row)']", $ns)) $profile.Phase
        Set-Text ($row.SelectSingleNode("s:c[@r='L$($profile.Row)']", $ns)) $profile.Ec
    }
    Set-Text ($originalRows[19].SelectSingleNode("s:c[@r='W22']", $ns)) 'Si sigue creciendo con vigor, sostener perfil anterior. No reducir por calendario.'
    Set-Text ($originalRows[19].SelectSingleNode("s:c[@r='N22']", $ns)) '750-1000'
    Set-Text ($originalRows[20].SelectSingleNode("s:c[@r='C23']", $ns)) 'Maduracion, sin lavado fijo'
    Set-Text ($originalRows[20].SelectSingleNode("s:c[@r='W23']", $ns)) 'No es lavado: mantener nutrientes si sigue engordando; cosechar por madurez, no por semana.'
    Set-Text ($originalRows[20].SelectSingleNode("s:c[@r='N23']", $ns)) '750-1000'
    Set-Text ($originalRows[16].SelectSingleNode("s:c[@r='L16']", $ns)) '1.5 - 1.9'
    Set-Text ($originalRows[17].SelectSingleNode("s:c[@r='L17']", $ns)) '1.5 - 1.9'

    # La fila 21 original es el espacio entre la tabla y las reglas.
    $noteRow = $originalRows[21]
    if (-not $noteRow) {
        $noteRow = $xml.CreateElement('row', $uri)
        $noteRow.SetAttribute('r', '24')
        $next = $originalRows[22]
        [void]$data.InsertBefore($noteRow, $next)
    }
    foreach ($item in @(@('A24','Duracion de floracion'), @('B24','Semana 4 total = floracion 1. Semanas florales 8-9 son condicionales: si sigue engordando, mantener el perfil anterior; cosechar por madurez real. No hay lavado obligatorio. Las dosis son una hipotesis agronomica, no un ensayo de Emergen C.'))) {
        $cell = $noteRow.SelectSingleNode("s:c[@r='$($item[0])']", $ns)
        if (-not $cell) {
            $cell = $xml.CreateElement('c', $uri)
            $cell.SetAttribute('r', $item[0])
            [void]$noteRow.AppendChild($cell)
        }
        Set-Text $cell $item[1]
    }

    $ecNote = $originalRows[10].SelectSingleNode("s:c[@r='D10']", $ns)
    Set-Text $ecNote 'EC referencial, no predicha. Semana 5 medida: 1,526 mS/cm en 1,10 L; ~1,65 mS/cm a 1 L es estimacion. No subir todas las sales para perseguir EC. Ppm de sales no incluyen minerales del agua.'

    $dimension = $xml.SelectSingleNode('/s:worksheet/s:dimension', $ns)
    if ($dimension) { $dimension.SetAttribute('ref', 'A1:AI42') }
    $entry.Delete()
    $newEntry = $zip.CreateEntry($sheetPath, [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = [System.IO.StreamWriter]::new($newEntry.Open(), [System.Text.UTF8Encoding]::new($false))
    try { $xml.Save($writer) } finally { $writer.Dispose() }

    $workbookPath = 'xl\workbook.xml'
    $workbookEntry = $zip.GetEntry($workbookPath)
    if (-not $workbookEntry) { throw "Falta $workbookPath" }
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
    $calc.SetAttribute('fullCalcOnLoad', '1')
    $calc.SetAttribute('forceFullCalc', '1')
    $workbookEntry.Delete()
    $newWorkbookEntry = $zip.CreateEntry($workbookPath, [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = [System.IO.StreamWriter]::new($newWorkbookEntry.Open(), [System.Text.UTF8Encoding]::new($false))
    try { $workbook.Save($writer) } finally { $writer.Dispose() }
} finally {
    $zip.Dispose()
}

Write-Output $OutputPath
