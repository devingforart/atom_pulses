param(
    [Parameter(Mandatory = $true)] [string]$InputPath,
    [Parameter(Mandatory = $true)] [string]$OutputPath
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
    foreach ($char in $letters.ToCharArray()) { $number = $number * 26 + ([int]$char - [int][char]'A' + 1) }
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
    if (-not $cols) {
        $cols = $xml.CreateElement('cols', $mainNs)
        $format = $xml.SelectSingleNode('//s:sheetFormatPr', $ns)
        [void]$xml.DocumentElement.InsertAfter($cols, $format)
    }
    $col = $xml.CreateElement('col', $mainNs)
    $col.SetAttribute('min', [string]$column); $col.SetAttribute('max', [string]$column)
    $col.SetAttribute('width', $width.ToString([Globalization.CultureInfo]::InvariantCulture)); $col.SetAttribute('customWidth','1')
    [void]$cols.AppendChild($col)
}
function Reset-Sheet($xml, $ns) {
    $sheetData = $xml.SelectSingleNode('//s:sheetData', $ns)
    foreach ($row in @($sheetData.SelectNodes('s:row', $ns))) { [void]$sheetData.RemoveChild($row) }
    $cols = $xml.SelectSingleNode('//s:cols', $ns)
    if ($cols) { foreach ($col in @($cols.SelectNodes('s:col', $ns))) { [void]$cols.RemoveChild($col) } }
    foreach ($name in @('mergeCells','autoFilter','conditionalFormatting','dataValidations','hyperlinks','tableParts')) {
        foreach ($node in @($xml.SelectNodes("//s:$name", $ns))) { [void]$node.ParentNode.RemoveChild($node) }
    }
}

if (-not (Test-Path -LiteralPath $InputPath)) { throw "No existe $InputPath" }
Copy-Item -LiteralPath $InputPath -Destination $OutputPath -Force
$zip = [IO.Compression.ZipFile]::Open($OutputPath, [IO.Compression.ZipArchiveMode]::Update)
try {
    $sheet = Read-Entry $zip 'xl\worksheets\sheet1.xml'
    $micro = Read-Entry $zip 'xl\worksheets\sheet2.xml'
    $workbook = Read-Entry $zip 'xl\workbook.xml'
    $ns = Ns $sheet; $microNs = Ns $micro
    Reset-Sheet $sheet $ns; Reset-Sheet $micro $microNs

    Set-Text $sheet $ns 'A1' 'CULTIVO NAHUEL - TIERRA / REGADERA' '1'
    Set-Text $sheet $ns 'A2' 'PARAMETRO CONFIGURABLE' '10'; Set-Text $sheet $ns 'B2' 'VALOR' '10'; Set-Text $sheet $ns 'C2' 'UNIDAD' '10'; Set-Text $sheet $ns 'D2' 'NOTAS' '10'
    $parameters = @(
        @('Litros de regadera',10,'L','Todas las cantidades se recalculan por este volumen.'),
        @('Litros por maceta',1,'L','Volumen orientativo por planta; regar lento y parar antes de saturar.'),
        @('EC del agua base',0.3,'mS/cm','Medir antes de fertilizar. Si usas osmosis, ingresar 0,0.'),
        @('Multiplicador de receta',0.5,'x','0,50 para tierra comercial/preabonada. 1,00 solo para suelo mineral/inertizado sin carga.'),
        @('Riegos de agua entre fertirriego',1,'riego(s)','1 significa: un riego nutritivo y luego un riego solo agua. Subir a 2 si el suelo viene cargado.'),
        @('pH de referencia',6.4,'pH','Objetivo general en tierra: 6,2-6,6. Ajustar segun la mezcla y respuesta.')
    )
    for ($i=0; $i -lt $parameters.Count; $i++) {
        $row = 3 + $i
        Set-Text $sheet $ns "A$row" $parameters[$i][0]
        Set-Number $sheet $ns "B$row" $parameters[$i][1] '1'
        Set-Text $sheet $ns "C$row" $parameters[$i][2]
        Set-Text $sheet $ns "D$row" $parameters[$i][3]
    }
    Set-Text $sheet $ns 'F3' 'USO' '10'
    Set-Text $sheet $ns 'F4' 'Esta receta es para riego manual en tierra. No es un programa de pulsos ni de runoff diario como coco.'
    Set-Text $sheet $ns 'F5' 'Primero preparar una sola regadera y medir EC/pH. No fertilizar si la maceta aun esta pesada.'
    Set-Text $sheet $ns 'F6' 'Con suelo prefertilizado, empezar siempre con multiplicador 0,50 y observar 7-10 dias antes de subir.'
    Set-Text $sheet $ns 'F7' 'Micro C: la formula madre no cambia. Esta tabla usa menos ml/L que coco porque la tierra retiene micros.'
    Set-Text $sheet $ns 'F8' 'Las EC son objetivos de la solucion terminada; no son una suma de valores ni reemplazan la medicion real.'

    $headers = [ordered]@{
        A='Semana'; B='Fase'; C='EC objetivo (mS/cm)'; D='pH objetivo'; E='PPFD maximo'; F='Calcinit / nitrato Ca (g)'
        G='Nitrato de potasio (g)'; H='Epsom / sulfato Mg (g)'; I='Sulfato de potasio (g)'; J='MKP 0-52-34 (g)'
        K='Micro C (ml)'; L='Frecuencia'; M='N total (ppm)'; N='P elemental (ppm)'; O='K elemental (ppm)'
        P='Ca (ppm)'; Q='Mg (ppm)'; R='S (ppm)'; S='Accion / observacion'
    }
    foreach ($column in $headers.Keys) { Set-Text $sheet $ns "$column`11" $headers[$column] '10' }

    # Rates are g/L at 1.00x. 0.50x is the safe default for pre-fertilized soil.
    $phases = @('Enraizamiento / veg inicial','Vegetativo medio','Vegetativo final','Transicion / stretch','Floracion temprana','Engorde','Engorde avanzado','Maduracion','Final suave')
    $ec = @('0.5 - 0.7','0.6 - 0.8','0.7 - 0.9','0.8 - 1.0','0.9 - 1.1','0.9 - 1.1','0.8 - 1.0','0.6 - 0.8','0.3 - 0.5')
    $ph = @('6.2 - 6.5','6.2 - 6.5','6.2 - 6.5','6.2 - 6.5','6.3 - 6.6','6.3 - 6.6','6.3 - 6.6','6.3 - 6.6','6.3 - 6.6')
    $ppfd = @('300-400','400-500','500-650','650-800','800-900','850-950','800-900','700-850','600-700')
    $calcinit = @(0.22,0.30,0.34,0.34,0.34,0.30,0.25,0.18,0.06)
    $kno3 = @(0.08,0.11,0.13,0.13,0.13,0.12,0.10,0.05,0.00)
    $epsom = @(0.12,0.16,0.18,0.18,0.19,0.18,0.16,0.13,0.06)
    $k2so4 = @(0.03,0.04,0.05,0.05,0.05,0.05,0.05,0.05,0.02)
    $mkp = @(0.05,0.07,0.08,0.09,0.10,0.09,0.07,0.05,0.01)
    $microMlPerL = @(0.10,0.15,0.20,0.20,0.20,0.20,0.20,0.15,0.10)
    $actions = @(
        'Regar lento; no buscar drenaje. Si la tierra esta cargada, usar 0,25x.',
        'Fertilizar y luego respetar los riegos de agua configurados arriba.',
        'No subir EC si hay puntas quemadas o maceta permanece pesada.',
        'Mantener N moderado; observar estiramiento y color de hoja.',
        'Controlar acumulacion: medir EC de drenaje solo de forma ocasional.',
        'No aumentar PK por encima de la EC objetivo para forzar flores.',
        'Bajar multiplicador si runoff EC supera entrada +0,5 mS/cm.',
        'Reducir intensidad; mantener riego por peso, no por calendario fijo.',
        'Solo solucion suave o agua ajustada, segun EC de drenaje y estado de planta.'
    )
    for ($i=0; $i -lt 9; $i++) {
        $row=12+$i
        Set-Number $sheet $ns "A$row" ($i+1); Set-Text $sheet $ns "B$row" $phases[$i]; Set-Text $sheet $ns "C$row" $ec[$i]; Set-Text $sheet $ns "D$row" $ph[$i]; Set-Text $sheet $ns "E$row" $ppfd[$i]
        foreach ($entry in @(@('F',$calcinit[$i]),@('G',$kno3[$i]),@('H',$epsom[$i]),@('I',$k2so4[$i]),@('J',$mkp[$i]),@('K',$microMlPerL[$i]))) {
            $dose = $entry[1].ToString([Globalization.CultureInfo]::InvariantCulture)
            Set-Formula $sheet $ns ("{0}{1}" -f $entry[0],$row) ("`$B`$3*$dose*`$B`$6") '1'
        }
        Set-Formula $sheet $ns "L$row" 'IF($B$7=0,"Todos los riegos: nutrientes",IF($B$7=1,"1 nutritivo / 1 agua","1 nutritivo / "&$B$7&" aguas"))'
        Set-Formula $sheet $ns "M$row" "(F$row*0.155+G$row*0.137)*1000/`$B`$3"
        Set-Formula $sheet $ns "N$row" "J$row*0.52*0.4364*1000/`$B`$3"
        Set-Formula $sheet $ns "O$row" "(G$row*0.461*0.8301+I$row*0.444+J$row*0.34*0.8301)*1000/`$B`$3"
        Set-Formula $sheet $ns "P$row" "F$row*0.19*1000/`$B`$3"
        Set-Formula $sheet $ns "Q$row" "H$row*0.0986*1000/`$B`$3"
        Set-Formula $sheet $ns "R$row" "(H$row*0.13+I$row*0.182)*1000/`$B`$3"
        Set-Text $sheet $ns "S$row" $actions[$i]
    }

    Set-Text $sheet $ns 'A23' 'ORDEN PARA UNA REGADERA' '1'
    $steps = @(
        @('1','Poner 70-80% del agua. Medir y anotar EC base.'),
        @('2','Agregar Micro C en la dosis indicada; no se usa para subir EC.'),
        @('3','Disolver Calcinit por separado y agregar. Nunca mezclarlo concentrado con sulfatos o MKP.'),
        @('4','Disolver por separado nitrato de potasio, Epsom, sulfato de potasio y MKP; agregar uno por uno.'),
        @('5','Completar al volumen final. Mezclar bien, medir EC y ajustar pH al final.'),
        @('6','Regar lento. Detenerse si aparece drenaje temprano o la maceta ya tiene peso excesivo.'),
        @('7','En el proximo riego usar solo agua ajustada, salvo que B7 indique otro intervalo.')
    )
    for ($i=0; $i -lt $steps.Count; $i++) { $row=24+$i; Set-Text $sheet $ns "A$row" $steps[$i][0]; Set-Text $sheet $ns "B$row" $steps[$i][1] }
    Set-Text $sheet $ns 'A32' 'NOTA CLAVE' '1'
    Set-Text $sheet $ns 'B32' 'La formula madre de Micro C se mantiene igual. Lo que cambia para tierra es la dosis por litro, la EC final y la frecuencia de aplicacion.'
    Set-Text $sheet $ns 'A33' 'LIMITACION' '1'
    Set-Text $sheet $ns 'B33' 'Sin analisis de sustrato no existe una receta universal. Si la tierra contiene compost, humus o fertilizante de liberacion lenta, mantener 0,50x o menos y decidir por EC/pH de drenaje y respuesta de la planta.'

    $dimension = $sheet.SelectSingleNode('//s:dimension', $ns); if ($dimension) { $dimension.SetAttribute('ref','A1:S33') }
    $widths=@(10,30,18,14,15,22,22,21,23,18,16,23,16,19,19,15,15,15,58)
    for($i=0;$i-lt$widths.Count;$i++){Set-ColumnWidth $sheet $ns ($i+1) $widths[$i]}

    Set-Text $micro $microNs 'A1' 'MICRO C - USO EN CULTIVO NAHUEL' '1'
    Set-Text $micro $microNs 'A2' 'La solucion madre mantiene la receta de 2 L. Para tierra se reduce su dosis en la regadera; no se modifica la formula madre.'
    Set-Text $micro $microNs 'A4' 'Ingrediente para 2 L finales' '10';Set-Text $micro $microNs 'B4' 'Cantidad' '10';Set-Text $micro $microNs 'C4' 'Especificacion' '10'
    $microRecipe=@(
        @('Afital hierro EDTA liquido',210,'g - Fe 4% p/p'),@('Sulfato de manganeso',7.74,'g - MnSO4.H2O, 31% Mn'),@('Sulfato de zinc',2.11,'g - ZnSO4.7H2O'),@('Acido borico',8.91,'g - 17,5% B'),@('Sulfato de cobre',0.48,'g - CuSO4.5H2O'),@('Molibdato de sodio',0.2,'g - Na2MoO4.2H2O'),@('Agua destilada/osmosis','c.s.p. 2,00','L finales')
    )
    for($i=0;$i-lt$microRecipe.Count;$i++){$row=5+$i;Set-Text $micro $microNs "A$row" $microRecipe[$i][0];if($microRecipe[$i][1] -is [double] -or $microRecipe[$i][1] -is [int]){Set-Number $micro $microNs "B$row" $microRecipe[$i][1] '1'}else{Set-Text $micro $microNs "B$row" [string]$microRecipe[$i][1]};Set-Text $micro $microNs "C$row" $microRecipe[$i][2]}
    Set-Text $micro $microNs 'A14' 'DOSIS EN TIERRA' '1'
    Set-Text $micro $microNs 'A15' 'Dosis de esta tabla'; Set-Text $micro $microNs 'B15' '0,10-0,20 ml/L a 1,00x, solo en riego nutritivo. El multiplicador de Tierra tambien reduce esta dosis.'
    Set-Text $micro $microNs 'A16' 'Regadera de 10 L'; Set-Text $micro $microNs 'B16' 'A 0,50x: 0,5-1 ml. A 1,00x: 1-2 ml. La cantidad exacta se calcula en la hoja Tierra.'
    Set-Text $micro $microNs 'A17' 'Comparacion con coco'; Set-Text $micro $microNs 'B17' 'Coco usaba 0,50 ml/L. Tierra usa menos porque retiene micronutrientes y se fertiliza con menor frecuencia.'
    Set-Text $micro $microNs 'A18' 'Mejora futura'; Set-Text $micro $microNs 'B18' 'Para tierra con pH 6,2-6,6, Fe-DTPA es mas estable que Fe-EDTA. No sustituir gramos sin recalcular concentracion de hierro.'
    Set-Text $micro $microNs 'A19' 'PREPARACION' '1'
    Set-Text $micro $microNs 'A20' 'Disolver las sales por separado en aprox. 1,5 L de agua. Agregar boro, Mn, Zn, Cu y Mo; despues el hierro. Completar a 2 L finales, guardar opaco y agitar antes de usar.'
    $microDimension=$micro.SelectSingleNode('//s:dimension',$microNs);if($microDimension){$microDimension.SetAttribute('ref','A1:C20')}
    foreach($spec in @(@(1,34),@(2,58),@(3,42))){Set-ColumnWidth $micro $microNs $spec[0] $spec[1]}

    $sheetNode=$workbook.SelectSingleNode('//*[local-name()="sheet"][@name="Sheet1"]');if($sheetNode){$sheetNode.SetAttribute('name','Tierra')}
    $additiveNode=$workbook.SelectSingleNode('//*[local-name()="sheet"][@name="Aditivos"]');if($additiveNode){$additiveNode.SetAttribute('name','Micro C')}
    $names=$workbook.SelectSingleNode('//*[local-name()="definedNames"]');if($names){[void]$names.ParentNode.RemoveChild($names)}
    $calc=$workbook.SelectSingleNode('//*[local-name()="calcPr"]');if(-not $calc){$calc=$workbook.CreateElement('calcPr',$mainNs);[void]$workbook.DocumentElement.AppendChild($calc)}
    $calc.SetAttribute('calcMode','auto');$calc.SetAttribute('fullCalcOnLoad','1');$calc.SetAttribute('forceFullCalc','1')
    Write-Entry $zip 'xl\worksheets\sheet1.xml' $sheet; Write-Entry $zip 'xl\worksheets\sheet2.xml' $micro; Write-Entry $zip 'xl\workbook.xml' $workbook
}
finally { $zip.Dispose() }
Get-Item -LiteralPath $OutputPath | Select-Object FullName,Length,LastWriteTime
