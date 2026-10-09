param(
    [Parameter(Mandatory = $true)] [string]$Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$mainNs = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'

function Read-Entry($zip, [string]$name) {
    $entry = $zip.GetEntry($name)
    if (-not $entry) { throw "No se encontro $name" }
    $reader = [IO.StreamReader]::new($entry.Open())
    try { return [xml]$reader.ReadToEnd() } finally { $reader.Dispose() }
}

function Write-Entry($zip, [string]$name, [xml]$xml) {
    $old = $zip.GetEntry($name)
    if ($old) { $old.Delete() }
    $entry = $zip.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
    $settings = [Xml.XmlWriterSettings]::new()
    $settings.Encoding = [Text.UTF8Encoding]::new($false)
    $settings.OmitXmlDeclaration = $false
    $writer = [Xml.XmlWriter]::Create($entry.Open(), $settings)
    try { $xml.Save($writer) } finally { $writer.Dispose() }
}

function Ns([xml]$xml) {
    $manager = [Xml.XmlNamespaceManager]::new($xml.NameTable)
    $manager.AddNamespace('s', $mainNs)
    return ,$manager
}

function Column-Number([string]$reference) {
    $letters = ([regex]::Match($reference, '^[A-Z]+')).Value
    $number = 0
    foreach ($char in $letters.ToCharArray()) {
        $number = $number * 26 + ([int]$char - [int][char]'A' + 1)
    }
    return $number
}

function Row($xml, $ns, [int]$number) {
    $sheetData = $xml.SelectSingleNode('//s:sheetData', $ns)
    $row = $xml.CreateElement('row', $mainNs)
    $row.SetAttribute('r', [string]$number)
    [void]$sheetData.AppendChild($row)
    return $row
}

function Cell($xml, $ns, [string]$reference) {
    $rowNumber = [int]([regex]::Match($reference, '\d+').Value)
    $row = $xml.SelectSingleNode("//s:sheetData/s:row[@r='$rowNumber']", $ns)
    if (-not $row) { $row = Row $xml $ns $rowNumber }
    $cell = $xml.CreateElement('c', $mainNs)
    $cell.SetAttribute('r', $reference)
    $column = Column-Number $reference
    $before = $null
    foreach ($candidate in $row.SelectNodes('s:c', $ns)) {
        if ((Column-Number $candidate.r) -gt $column) { $before = $candidate; break }
    }
    if ($before) { [void]$row.InsertBefore($cell, $before) } else { [void]$row.AppendChild($cell) }
    return $cell
}

function Set-Text($xml, $ns, [string]$reference, [string]$value, [string]$style = '') {
    $cell = Cell $xml $ns $reference
    $cell.SetAttribute('t', 'inlineStr')
    if ($style) { $cell.SetAttribute('s', $style) }
    $inline = $xml.CreateElement('is', $mainNs)
    $text = $xml.CreateElement('t', $mainNs)
    $text.InnerText = $value
    [void]$inline.AppendChild($text)
    [void]$cell.AppendChild($inline)
}

function Set-Number($xml, $ns, [string]$reference, [double]$value, [string]$style = '') {
    $cell = Cell $xml $ns $reference
    if ($style) { $cell.SetAttribute('s', $style) }
    $node = $xml.CreateElement('v', $mainNs)
    $node.InnerText = $value.ToString([Globalization.CultureInfo]::InvariantCulture)
    [void]$cell.AppendChild($node)
}

function Set-Formula($xml, $ns, [string]$reference, [string]$formula, [string]$style = '') {
    $cell = Cell $xml $ns $reference
    if ($style) { $cell.SetAttribute('s', $style) }
    $node = $xml.CreateElement('f', $mainNs)
    $node.InnerText = $formula
    [void]$cell.AppendChild($node)
    [void]$cell.AppendChild($xml.CreateElement('v', $mainNs))
}

function Set-ColumnWidth($xml, $ns, [int]$column, [double]$width) {
    $cols = $xml.SelectSingleNode('//s:cols', $ns)
    $col = $xml.CreateElement('col', $mainNs)
    $col.SetAttribute('min', [string]$column)
    $col.SetAttribute('max', [string]$column)
    $col.SetAttribute('width', $width.ToString([Globalization.CultureInfo]::InvariantCulture))
    $col.SetAttribute('customWidth', '1')
    [void]$cols.AppendChild($col)
}

if (-not (Test-Path -LiteralPath $Path)) { throw "No existe $Path" }

$zip = [IO.Compression.ZipFile]::Open($Path, [IO.Compression.ZipArchiveMode]::Update)
try {
    $sheet = Read-Entry $zip 'xl\worksheets\sheet1.xml'
    $ns = Ns $sheet

    $sheetData = $sheet.SelectSingleNode('//s:sheetData', $ns)
    foreach ($row in @($sheetData.SelectNodes('s:row', $ns))) { [void]$sheetData.RemoveChild($row) }

    $cols = $sheet.SelectSingleNode('//s:cols', $ns)
    if (-not $cols) {
        $cols = $sheet.CreateElement('cols', $mainNs)
        $sheetFormat = $sheet.SelectSingleNode('//s:sheetFormatPr', $ns)
        [void]$sheet.DocumentElement.InsertAfter($cols, $sheetFormat)
    }
    foreach ($col in @($cols.SelectNodes('s:col', $ns))) { [void]$cols.RemoveChild($col) }

    foreach ($name in @('mergeCells','autoFilter','conditionalFormatting','dataValidations','hyperlinks','tableParts')) {
        foreach ($node in @($sheet.SelectNodes("//s:$name", $ns))) { [void]$node.ParentNode.RemoveChild($node) }
    }

    Set-Text $sheet $ns 'A1' 'NUTRICION PARA COCO - SENSI PROFESSIONAL' '1'
    Set-Text $sheet $ns 'A2' 'Litros de agua del tanque'
    Set-Number $sheet $ns 'B2' 40 '1'
    Set-Text $sheet $ns 'D2' 'EC del agua base (mS/cm)'
    Set-Number $sheet $ns 'E2' 0.3 '1'
    Set-Text $sheet $ns 'G2' 'Aditivo de floracion'
    Set-Text $sheet $ns 'H2' 'FACTOR X' '1'
    Set-Text $sheet $ns 'I2' 'Escribir CANDY o FACTOR X; nunca ambos en el mismo tanque.'
    Set-Text $sheet $ns 'A4' 'Las dosis calculadas son el punto de partida para el volumen indicado. Tras mezclar todo, la EC medida manda: ajustar las bases A y B siempre por igual.'

    $headers = [ordered]@{
        A='Semana'; B='Fase'; C='EC final objetivo'; D='pH final'; E='Grow A Pro (g)'; F='Grow B Pro (g)'
        G='Bloom A Pro (g)'; H='Bloom B Pro (g)'; I='Rhino Skin (ml)'; J='Cal Mag Xtra (ml)'
        K='Big Bud liquido (ml)'; L='Bud Candy (ml)'; M='Bud Factor X (ml)'; N='N total (ppm)'
        O='P elemental (ppm)'; P='K elemental (ppm)'; Q='Ca (ppm)'; R='Mg (ppm)'; S='S (ppm)'
        T='SiO2 aprox. (ppm)'; U='Accion al cerrar el tanque'; V='Observacion'
    }
    foreach ($column in $headers.Keys) { Set-Text $sheet $ns "$column`6" $headers[$column] '10' }

    $phases = @(
        'Enraizamiento / vegetativo inicial','Vegetativo medio','Vegetativo final','Transicion / stretch',
        'Floracion temprana','Engorde','Engorde avanzado','Maduracion','Final controlado'
    )
    $basePerL = @(0.35,0.46,0.60,0.65,0.65,0.68,0.68,0.60,0.45)
    $ecTargets = @('1.0 - 1.3','1.2 - 1.5','1.5 - 1.8','1.7 - 2.0','1.9 - 2.2','2.0 - 2.2','2.0 - 2.2','1.8 - 2.1','1.3 - 1.6')
    $phTargets = @('5.7 - 5.9','5.8 - 6.0','5.8 - 6.0','5.8 - 6.0','5.8 - 6.1','5.8 - 6.1','5.9 - 6.1','5.9 - 6.2','5.9 - 6.2')
    $bigBudPerL = @(0,0,0,0,2,2,2,2,0)
    $rhinoPerL = @(2,2,2,2,2,2,2,2,0)

    for ($i = 0; $i -lt 9; $i++) {
        $row = 7 + $i
        $dose = $basePerL[$i].ToString([Globalization.CultureInfo]::InvariantCulture)
        Set-Number $sheet $ns "A$row" ($i + 1)
        Set-Text $sheet $ns "B$row" $phases[$i]
        Set-Text $sheet $ns "C$row" $ecTargets[$i]
        Set-Text $sheet $ns "D$row" $phTargets[$i]
        if ($i -le 2) {
            Set-Formula $sheet $ns "E$row" "`$B`$2*$dose" '1'; Set-Formula $sheet $ns "F$row" "`$B`$2*$dose" '1'
            Set-Number $sheet $ns "G$row" 0 '1'; Set-Number $sheet $ns "H$row" 0 '1'
        } else {
            Set-Number $sheet $ns "E$row" 0 '1'; Set-Number $sheet $ns "F$row" 0 '1'
            Set-Formula $sheet $ns "G$row" "`$B`$2*$dose" '1'; Set-Formula $sheet $ns "H$row" "`$B`$2*$dose" '1'
        }
        Set-Formula $sheet $ns "I$row" "`$B`$2*$($rhinoPerL[$i])" '1'
        Set-Number $sheet $ns "J$row" 0 '1'
        Set-Formula $sheet $ns "K$row" "`$B`$2*$($bigBudPerL[$i])" '1'
        if ($i -le 2) {
            Set-Number $sheet $ns "L$row" 0 '1'; Set-Number $sheet $ns "M$row" 0 '1'
        } else {
            Set-Formula $sheet $ns "L$row" "IF(`$H`$2=`"CANDY`",`$B`$2*2,0)" '1'
            Set-Formula $sheet $ns "M$row" "IF(`$H`$2=`"FACTOR X`",`$B`$2*2,0)" '1'
        }

        Set-Formula $sheet $ns "N$row" "(E$row*0.09+F$row*0.15+G$row*0.10+H$row*0.17+J$row*0.04+L$row*0.008)*1000/`$B`$2"
        Set-Formula $sheet $ns "O$row" "(E$row*0.10+G$row*0.14+K$row*0.01)*0.4364*1000/`$B`$2"
        Set-Formula $sheet $ns "P$row" "(E$row*0.28+G$row*0.26+H$row*0.06+I$row*0.004+K$row*0.03)*0.8301*1000/`$B`$2"
        Set-Formula $sheet $ns "Q$row" "(F$row*0.185+H$row*0.14+J$row*0.032)*1000/`$B`$2"
        Set-Formula $sheet $ns "R$row" "(E$row*0.03+G$row*0.0285+J$row*0.011+L$row*0.005+M$row*0.005)*1000/`$B`$2"
        Set-Formula $sheet $ns "S$row" "(E$row*0.048+G$row*0.0378)*1000/`$B`$2"
        Set-Formula $sheet $ns "T$row" "I$row*0.0015*1000/`$B`$2"
        Set-Text $sheet $ns "U$row" 'Medir EC. Si queda baja, sumar A y B de la fase por igual, en pasos de 0,05 g/L.'
        if ($i -le 2) { Set-Text $sheet $ns "V$row" 'Sin Candy ni Factor X.' }
        else { Set-Formula $sheet $ns "V$row" 'IF($H$2="FACTOR X","Usar Factor X; Candy queda en cero.","Usar Candy; Factor X queda en cero.")' }
    }

    Set-Text $sheet $ns 'A18' 'ORDEN DE PREPARACION' '1'
    $rules = @(
        @('1','Llenar el tanque con 70-80% del agua.'),
        @('2','Agregar Rhino Skin al agua sola y recircular 5 minutos.'),
        @('3','Agregar Cal Mag Xtra solo si existe una necesidad confirmada; no queda fijo en esta receta.'),
        @('4','Prediluir y agregar la base A de la fase. Mezclar completamente.'),
        @('5','Prediluir y agregar la base B de la misma fase. Nunca juntar concentrados A y B.'),
        @('6','Agregar Big Bud cuando la fila lo indique.'),
        @('7','Agregar Bud Candy o Bud Factor X segun H2. No usar ambos en el mismo tanque.'),
        @('8','Completar el volumen, medir EC, corregir A y B por igual y ajustar el pH al final.'),
        @('9','En fertirriego, registrar EC/pH de entrada y drenaje; limpiar tanque, filtro y lineas con regularidad.')
    )
    for ($i = 0; $i -lt $rules.Count; $i++) {
        $row = 19 + $i
        Set-Text $sheet $ns "A$row" $rules[$i][0]
        Set-Text $sheet $ns "B$row" $rules[$i][1]
    }

    Set-Text $sheet $ns 'A29' 'DECISIONES IMPORTANTES' '1'
    Set-Text $sheet $ns 'A30' 'Base completa'
    Set-Text $sheet $ns 'B30' 'Se necesitan las cuatro partes: Grow A+B para vegetativo y Bloom A+B para floracion. Si solo tienes un par A+B, el programa no esta completo.'
    Set-Text $sheet $ns 'A31' 'Micronutrientes'
    Set-Text $sheet $ns 'B31' 'No falta Micro C: las partes A de Sensi Professional ya aportan B, Cu, Fe, Mn, Mo y Zn.'
    Set-Text $sheet $ns 'A32' 'Cal Mag'
    Set-Text $sheet $ns 'B32' 'Sensi Professional ya aporta Ca y Mg. Con agua base EC 0,3 se deja en cero hasta confirmar carencia, agua muy blanda/RO o analisis que lo justifique.'
    Set-Text $sheet $ns 'A33' 'Supuestos de producto'
    Set-Text $sheet $ns 'B33' 'Calculado para Sensi Pro WSP, Sensi Cal Mag Xtra, Big Bud liquido 0-1-3 y las etiquetas regionales investigadas. Verificar tus envases antes de preparar.'
    Set-Text $sheet $ns 'A34' 'Limite del calculo'
    Set-Text $sheet $ns 'B34' 'Los ppm de liquidos usan densidad aproximada 1 g/ml. La EC real del tanque y la respuesta de la planta prevalecen sobre la estimacion.'
    Set-Text $sheet $ns 'A35' 'Disponibilidad futura'
    Set-Text $sheet $ns 'B35' 'Advanced Nutrients anuncio en 2026 la discontinuacion mundial de Sensi Professional WSP. Puedes terminar el stock; para una compra futura habra que recalcular con Cultivator Series o CS2.'

    $dimension = $sheet.SelectSingleNode('//s:dimension', $ns)
    if ($dimension) { $dimension.SetAttribute('ref', 'A1:V35') }

    $widths = @(10,30,17,13,18,18,18,18,18,20,21,18,20,16,18,18,14,14,14,18,58,42)
    for ($i = 0; $i -lt $widths.Count; $i++) { Set-ColumnWidth $sheet $ns ($i + 1) $widths[$i] }

    $pane = $sheet.SelectSingleNode('//s:sheetViews/s:sheetView/s:pane', $ns)
    if ($pane) {
        $pane.SetAttribute('ySplit','6'); $pane.SetAttribute('topLeftCell','A7')
        $pane.SetAttribute('activePane','bottomLeft'); $pane.SetAttribute('state','frozen')
    }

    Write-Entry $zip 'xl\worksheets\sheet1.xml' $sheet
}
finally { $zip.Dispose() }

Get-Item -LiteralPath $Path | Select-Object FullName, Length, LastWriteTime
