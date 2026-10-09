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
    foreach ($char in $letters.ToCharArray()) {
        $number = $number * 26 + ([int]$char - [int][char]'A' + 1)
    }
    return $number
}

function Row($xml, $ns, [int]$number) {
    $sheetData = $xml.SelectSingleNode('//s:sheetData', $ns)
    $row = $sheetData.SelectSingleNode("s:row[@r='$number']", $ns)
    if ($row) { return $row }
    $row = $xml.CreateElement('row', $mainNs)
    $row.SetAttribute('r', [string]$number)
    $before = $null
    foreach ($candidate in $sheetData.SelectNodes('s:row', $ns)) {
        if ([int]$candidate.r -gt $number) { $before = $candidate; break }
    }
    if ($before) { [void]$sheetData.InsertBefore($row, $before) }
    else { [void]$sheetData.AppendChild($row) }
    return $row
}

function Cell($xml, $ns, [string]$reference) {
    $rowNumber = [int]([regex]::Match($reference, '\d+').Value)
    $row = Row $xml $ns $rowNumber
    $cell = $row.SelectSingleNode("s:c[@r='$reference']", $ns)
    if ($cell) { return $cell }
    $cell = $xml.CreateElement('c', $mainNs)
    $cell.SetAttribute('r', $reference)
    $column = Column-Number $reference
    $before = $null
    foreach ($candidate in $row.SelectNodes('s:c', $ns)) {
        if ((Column-Number $candidate.r) -gt $column) { $before = $candidate; break }
    }
    if ($before) { [void]$row.InsertBefore($cell, $before) }
    else { [void]$row.AppendChild($cell) }
    return $cell
}

function Reset-Cell($cell) {
    while ($cell.HasChildNodes) { [void]$cell.RemoveChild($cell.FirstChild) }
    [void]$cell.RemoveAttribute('t')
}

function Set-Text($xml, $ns, [string]$reference, [string]$value, [string]$style = '') {
    $cell = Cell $xml $ns $reference
    Reset-Cell $cell
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
    Reset-Cell $cell
    if ($style) { $cell.SetAttribute('s', $style) }
    $node = $xml.CreateElement('v', $mainNs)
    $node.InnerText = $value.ToString([Globalization.CultureInfo]::InvariantCulture)
    [void]$cell.AppendChild($node)
}

function Set-Formula($xml, $ns, [string]$reference, [string]$formula, [string]$style = '') {
    $cell = Cell $xml $ns $reference
    Reset-Cell $cell
    if ($style) { $cell.SetAttribute('s', $style) }
    $node = $xml.CreateElement('f', $mainNs)
    $node.InnerText = $formula
    [void]$cell.AppendChild($node)
    $cached = $xml.CreateElement('v', $mainNs)
    [void]$cell.AppendChild($cached)
}

function Clear-Row($xml, $ns, [int]$number) {
    $row = Row $xml $ns $number
    foreach ($cell in @($row.SelectNodes('s:c', $ns))) { [void]$row.RemoveChild($cell) }
}

function Set-ColumnWidth($xml, $ns, [int]$column, [double]$width) {
    $cols = $xml.SelectSingleNode('//s:cols', $ns)
    if (-not $cols) {
        $cols = $xml.CreateElement('cols', $mainNs)
        $sheetFormat = $xml.SelectSingleNode('//s:sheetFormatPr', $ns)
        [void]$xml.DocumentElement.InsertAfter($cols, $sheetFormat)
    }
    foreach ($old in @($cols.SelectNodes("s:col[@min='$column' and @max='$column']", $ns))) {
        [void]$cols.RemoveChild($old)
    }
    $col = $xml.CreateElement('col', $mainNs)
    $col.SetAttribute('min', [string]$column)
    $col.SetAttribute('max', [string]$column)
    $col.SetAttribute('width', $width.ToString([Globalization.CultureInfo]::InvariantCulture))
    $col.SetAttribute('customWidth', '1')
    [void]$cols.AppendChild($col)
}

if (-not (Test-Path -LiteralPath $InputPath)) { throw "No existe $InputPath" }
Copy-Item -LiteralPath $InputPath -Destination $OutputPath -Force

$zip = [IO.Compression.ZipFile]::Open($OutputPath, [IO.Compression.ZipArchiveMode]::Update)
try {
    $sheet = Read-Entry $zip 'xl\worksheets\sheet1.xml'
    $products = Read-Entry $zip 'xl\worksheets\sheet2.xml'
    $workbook = Read-Entry $zip 'xl\workbook.xml'
    $ns = Ns $sheet
    $nsProducts = Ns $products

    # Preserve the irrigation settings and schedules, but replace the entire
    # nutrient block with the user's Advanced Nutrients products.
    Set-Text $sheet $ns 'A1' 'NUTRICION COCO - SENSI PROFESSIONAL' '1'
    Set-Number $sheet $ns 'B10' 0 '1'
    Set-Text $sheet $ns 'D10' 'EC medida antes de fertilizar. Configurada en 0,0 mS/cm para agua de osmosis; la EC objetivo es la lectura final del tanque.'
    Set-Text $sheet $ns 'E10' 'Modo aditivo floracion'
    Set-Text $sheet $ns 'F10' 'FACTOR X' '1'
    Set-Text $sheet $ns 'G10' 'Escribir CANDY o FACTOR X. La opcion no elegida queda en cero.'
    Set-Text $sheet $ns 'H10' 'EC objetivo RO + Cal Mag'
    Set-Number $sheet $ns 'I10' 0.3 '1'
    Set-Text $sheet $ns 'J10' 'Ajustar Cal Mag hasta esta EC antes de agregar A/B; las dosis de la columna I son puntos de partida.'

    $headers = [ordered]@{
        D='Sensi Grow A Pro (g)'; E='Sensi Grow B Pro (g)'; F='Sensi Bloom A Pro (g)'; G='Sensi Bloom B Pro (g)'
        H='Rhino Skin (ml)'; I='Cal Mag Xtra (ml)'; J='Big Bud LIQUIDO (ml)'; K='Bud Candy (ml) - tanque regular'
        L='Bud Factor X (ml) - tanque X, REEMPLAZA Candy'; M='EC final objetivo (mS/cm)'; N='pH final objetivo'
        O='PPFD Maximo'; P='Riegos al Dia'; Q='% Vol por Pulso'; R='Volumen Pulso (ml)'; S='Tiempo por Riego (seg)'
        T='Horarios Exactos de Riego'; U='Total diario (ml/planta)'; V='Runoff objetivo'; W='Dryback noche objetivo'
        X='Regla de ajuste'; Y='Nota de preparacion'; Z='N total calculado (ppm)'; AA='P elemental calculado (ppm)'
        AB='K elemental calculado (ppm)'; AC='Ca calculado (ppm)'; AD='Mg calculado (ppm)'; AE='S calculado (ppm)'
        AF='SiO2 declarado aprox. (ppm)'; AG='Modo de tanque'; AH='Uso de Cal Mag'; AI='Compatibilidad critica'
        AJ='Base activa por parte (g/L)'
    }
    foreach ($column in $headers.Keys) { Set-Text $sheet $ns "$column`11" $headers[$column] '10' }

    $phases = @(
        @('Enraizamiento / Veg','Vegetativo suave'), @('Vegetativo Medio','Vegetativo'), @('Vegetativo Final','Vegetativo'),
        @('Transicion (Stretch)','Generativo controlado'), @('Desarrollo Flores','Generativo'), @('Engorde (Bulking)','Balanceado'),
        @('Engorde Avanzado','Balanceado'), @('Maduracion','Generativo suave'), @('Final controlado','Baja EC')
    )
    $basePerL = @(0.35,0.46,0.60,0.65,0.65,0.68,0.68,0.60,0.45)
    $ecTargets = @('1.0 - 1.3','1.2 - 1.5','1.5 - 1.8','1.7 - 2.0','1.9 - 2.2','2.0 - 2.2','2.0 - 2.2','1.8 - 2.1','1.3 - 1.6')
    $phTargets = @('5.7 - 5.9','5.8 - 6.0','5.8 - 6.0','5.8 - 6.0','5.8 - 6.1','5.8 - 6.1','5.9 - 6.1','5.9 - 6.2','5.9 - 6.2')
    $ppfd = @('300-400','400-500','500-650','650-800','800-900','900-1000','850-1000','750-900','600-750')
    $bigBudPerL = @(0,0,0,0,2,2,2,2,0)
    $rhinoPerL = @(2,2,2,2,2,2,2,2,0)
    # RO water has no Ca/Mg reserve. These conservative doses supplement the
    # complete Sensi Pro base without imposing the full 2 ml/L correction dose.
    $calMagPerL = @(1,1,0.5,0.5,0,0,0,0,0)
    $candyPerL = @(0,0,0,2,2,2,2,2,2)
    $factorPerL = @(0,0,0,2,2,2,2,2,2)
    $runoff = @('5-10%','5-15%','10-15%','10-20%','15-25%','15-25%','15-25%','15-25%','20-30%')
    $dryback = @('10-18%','12-22%','15-25%','18-28%','18-30%','15-25%','15-25%','18-28%','15-25%')
    $rules = @(
        'Si no seca, quitar ultimo pulso','Subir pulsos solo con buen dryback','Si runoff EC sube, aumentar drenaje',
        'No dejar runoff EC > entrada +0,5','Priorizar runoff antes que subir EC','Mas frecuencia si seca antes de mitad del dia',
        'Reducir EC si hay puntas quemadas','No elevar EC para forzar maduracion','No lavar coco con agua vacia'
    )

    for ($i = 0; $i -lt 9; $i++) {
        $row = 12 + $i
        $originalScheduleNode = $sheet.SelectSingleNode("//s:c[@r='S$row']/s:f", $ns)
        $originalScheduleText = if ($originalScheduleNode) { $originalScheduleNode.InnerText } else { '' }
        $dose = $basePerL[$i].ToString([Globalization.CultureInfo]::InvariantCulture)
        Set-Number $sheet $ns "A$row" ($i + 1)
        Set-Text $sheet $ns "B$row" $phases[$i][0]
        Set-Text $sheet $ns "C$row" $phases[$i][1]
        if ($i -le 2) {
            Set-Formula $sheet $ns "D$row" "`$B`$3*$dose" '1'; Set-Formula $sheet $ns "E$row" "`$B`$3*$dose" '1'
            Set-Number $sheet $ns "F$row" 0 '1'; Set-Number $sheet $ns "G$row" 0 '1'
        } else {
            Set-Number $sheet $ns "D$row" 0 '1'; Set-Number $sheet $ns "E$row" 0 '1'
            Set-Formula $sheet $ns "F$row" "`$B`$3*$dose" '1'; Set-Formula $sheet $ns "G$row" "`$B`$3*$dose" '1'
        }
        Set-Formula $sheet $ns "H$row" "`$B`$3*$($rhinoPerL[$i])" '1'
        Set-Formula $sheet $ns "I$row" "`$B`$3*$($calMagPerL[$i])" '1'
        Set-Formula $sheet $ns "J$row" "`$B`$3*$($bigBudPerL[$i])" '1'
        if ($i -le 2) {
            Set-Number $sheet $ns "K$row" 0 '1'; Set-Number $sheet $ns "L$row" 0 '1'
        } else {
            Set-Formula $sheet $ns "K$row" "IF(`$F`$10=`"CANDY`",`$B`$3*$($candyPerL[$i]),0)" '1'
            Set-Formula $sheet $ns "L$row" "IF(`$F`$10=`"FACTOR X`",`$B`$3*$($factorPerL[$i]),0)" '1'
        }
        Set-Text $sheet $ns "M$row" $ecTargets[$i]
        Set-Text $sheet $ns "N$row" $phTargets[$i]
        Set-Text $sheet $ns "O$row" $ppfd[$i]

        # Irrigation values stay identical to the audited NUTRICION workbook.
        $oldRiegos = @(5,7,8,5,5,8,8,5,4)[$i]
        $oldPulse = @(0.018,0.020,0.022,0.040,0.045,0.030,0.028,0.040,0.050)[$i]
        Set-Number $sheet $ns "P$row" $oldRiegos
        Set-Number $sheet $ns "Q$row" $oldPulse
        Set-Formula $sheet $ns "R$row" "`$B`$2*1000*Q$row"
        Set-Formula $sheet $ns "S$row" "(R$row/(`$B`$4*`$B`$5))*60"

        if ($originalScheduleText) { $schedule = $originalScheduleText.Replace("O$row", "P$row") }
        else { $schedule = '"REVISAR HORARIO"' }
        Set-Formula $sheet $ns "T$row" $schedule
        Set-Formula $sheet $ns "U$row" "P$row*R$row"
        Set-Text $sheet $ns "V$row" $runoff[$i]
        Set-Text $sheet $ns "W$row" $dryback[$i]
        Set-Text $sheet $ns "X$row" $rules[$i]
        Set-Text $sheet $ns "Y$row" 'Agregar A y B en partes iguales. Tras todos los productos, ajustar ambas bases por igual hasta alcanzar la EC final.'

        # Label-based elemental estimates. Liquids are approximated at 1 g/ml.
        Set-Formula $sheet $ns "Z$row" "(D$row*0.09+E$row*0.15+F$row*0.10+G$row*0.17+I$row*0.04+K$row*0.008)*1000/`$B`$3"
        Set-Formula $sheet $ns "AA$row" "(D$row*0.10+F$row*0.14+J$row*0.01)*0.4364*1000/`$B`$3"
        Set-Formula $sheet $ns "AB$row" "(D$row*0.28+F$row*0.26+G$row*0.06+H$row*0.004+J$row*0.03)*0.8301*1000/`$B`$3"
        Set-Formula $sheet $ns "AC$row" "(E$row*0.185+G$row*0.14+I$row*0.032)*1000/`$B`$3"
        Set-Formula $sheet $ns "AD$row" "(D$row*0.03+F$row*0.0285+I$row*0.011+K$row*0.005+L$row*0.005)*1000/`$B`$3"
        Set-Formula $sheet $ns "AE$row" "(D$row*0.048+F$row*0.0378)*1000/`$B`$3"
        Set-Formula $sheet $ns "AF$row" "H$row*0.0015*1000/`$B`$3"
        if ($i -le 2) { Set-Text $sheet $ns "AG$row" 'Un solo tanque; sin Candy ni Factor X' }
        else { Set-Formula $sheet $ns "AG$row" 'IF($F$10="FACTOR X","Tanque X: Factor X; Candy = 0","Tanque regular: Candy; Factor X = 0")' }
        Set-Text $sheet $ns "AH$row" 'Con RO, ajustar I hasta la EC acondicionada indicada en I10 antes de A/B. Si hay exceso de N/Ca o bloqueo de K/Mg, reducir.'
        Set-Text $sheet $ns "AI$row" 'Bud Candy y Bud Factor X nunca juntos en este plan.'
        Set-Formula $sheet $ns "AJ$row" "MAX(D$row,F$row)/`$B`$3"
    }

    Set-Text $sheet $ns 'A22' 'REGLAS DE PREPARACION - COCO Y FERTIRRIEGO' '1'
    $rulesTable = @(
        @('Orden de mezcla','Agua 70-80% > Rhino Skin > mezclar 5 min > Cal Mag si corresponde > base A prediluida > base B prediluida > Big Bud > Candy O Factor X > completar > EC > pH.'),
        @('A y B','Usar siempre A y B de la misma fase y en igual peso. Nunca unir polvos ni concentrados sin diluir.'),
        @('Ajuste por EC','Las cantidades son punto de partida. Con todos los productos dentro, sumar A y B por igual en pasos de 0,05 g/L hasta entrar en el rango; si se excede, diluir.'),
        @('Cal Mag Xtra','Con RO se usa 1 ml/L al inicio, 0,5 ml/L en vegetativo final/transicion y luego cero porque Sensi Pro y Factor X ya aportan Ca/Mg. Ajustar por respuesta y runoff.'),
        @('EC de agua acondicionada','I10 es una referencia previa a las bases. Si la dosis de I12:I20 no alcanza I10, subir poco a poco; si la supera, reducir. La EC final de la tabla incluye todos los productos.'),
        @('Candy vs Factor X','Preparar tanques separados. En el tanque X, omitir completamente Bud Candy.'),
        @('Bud Candy en lineas','No dejar la solucion azucarada estancada varios dias. Mantener tanque fresco, filtro limpio y purgar lineas para limitar biofilm.'),
        @('Big Bud','Esta tabla presupone Big Bud liquido 0-1-3. No sirve para Big Bud Coco ni polvo sin recalcular.'),
        @('Producto faltante','Fundamental: tener Grow A+B y Bloom A+B. No hace falta Micro C. Overdrive es opcional para floracion tardia, no esencial para completar nutrientes.'),
        @('Control','Registrar EC/pH de entrada y EC/pH de runoff. Si runoff supera entrada +0,5, bajar EC 10-15% y aumentar drenaje temporalmente.')
    )
    for ($i = 0; $i -lt $rulesTable.Count; $i++) {
        $row = 23 + $i
        Set-Text $sheet $ns "A$row" $rulesTable[$i][0]
        Set-Text $sheet $ns "B$row" $rulesTable[$i][1]
    }

    $dimension = $sheet.SelectSingleNode('//s:dimension', $ns)
    if ($dimension) { $dimension.SetAttribute('ref', 'A1:AJ32') }
    foreach ($column in 4..12) { Set-ColumnWidth $sheet $ns $column 18 }
    Set-ColumnWidth $sheet $ns 20 58
    foreach ($column in 24..25) { Set-ColumnWidth $sheet $ns $column 34 }
    foreach ($column in 33..35) { Set-ColumnWidth $sheet $ns $column 32 }

    # Replace the former additives worksheet with a compact, useful reference.
    foreach ($row in @($products.SelectNodes('//s:sheetData/s:row', $nsProducts))) {
        [void]$row.ParentNode.RemoveChild($row)
    }
    Set-Text $products $nsProducts 'A1' 'PRODUCTO' '10'
    Set-Text $products $nsProducts 'B1' 'COMPOSICION DECLARADA / INVESTIGADA' '10'
    Set-Text $products $nsProducts 'C1' 'FUNCION EN ESTA RECETA' '10'
    Set-Text $products $nsProducts 'D1' 'REGLA DE USO' '10'
    Set-Text $products $nsProducts 'E1' 'FUENTE' '10'
    $reference = @(
        @('Sensi Grow A Pro','9-10-28; Mg 3%; S 4,8%; B 0,04%; Cu 0,002%; Fe 0,2%; Mn 0,2%; Mo 0,0005%; Zn 0,03%','Macros, Mg, S y todos los micros de vegetativo','Solo con Grow B, mismo peso','https://www.advancednutrients.com/feeding/'),
        @('Sensi Grow B Pro','15-0-0; Ca 18,5%','Nitrato y calcio de vegetativo','Solo con Grow A, mismo peso','https://www.advancednutrients.com/feeding/'),
        @('Sensi Bloom A Pro','10-14-26; Mg 2,85%; S 3,78%; micros quelatados','Macros, Mg, S y micros de floracion','Solo con Bloom B, mismo peso','https://www.advancednutrients.com/feeding/'),
        @('Sensi Bloom B Pro','17-0-6; Ca 14%','Nitrogeno, potasio y calcio de floracion','Solo con Bloom A, mismo peso','https://www.advancednutrients.com/feeding/'),
        @('Rhino Skin','0-0-0,4; silicato de potasio; SiO2 depende de la etiqueta regional','Silicio estructural','Primero en agua; mezclar 5 minutos','https://www.advancednutrients.com/products/rhino-skin/'),
        @('Sensi Cal Mag Xtra','4-0-0; Ca 3,2%; Mg 1,1%; Fe 0,09%; Mn 0,05%; Zn 0,05%','Corrector, no base diaria','2 ml/L solo ante necesidad; no preventivo fijo','https://www.advancednutrients.com/products/sensi-cal-mag-xtra/'),
        @('Big Bud liquido','0-1-3; fosfato monopotasico, sulfato de potasio e hidrolizado','PK moderado en mitad de floracion','2 ml/L semanas de floracion 2 a 5','https://www.advancednutrients.com/products/big-bud/'),
        @('Bud Candy','0-0-0 nominal; carbohidratos y Mg; algunas etiquetas regionales declaran 0,8% N','Carbohidratos/Mg en tanque regular','2 ml/L; nunca junto con Factor X en esta receta','https://www.advancednutrients.com/products/bud-candy/'),
        @('Bud Factor X','0-0-0; Mg soluble y extractos bioactivos','Biostimulante de floracion','2 ml/L en tanque X sustituyendo Bud Candy','https://www.advancednutrients.com/products/bud-factor-x/')
    )
    for ($i = 0; $i -lt $reference.Count; $i++) {
        $row = 2 + $i
        Set-Text $products $nsProducts "A$row" $reference[$i][0]
        Set-Text $products $nsProducts "B$row" $reference[$i][1]
        Set-Text $products $nsProducts "C$row" $reference[$i][2]
        Set-Text $products $nsProducts "D$row" $reference[$i][3]
        Set-Text $products $nsProducts "E$row" $reference[$i][4]
    }
    Set-Text $products $nsProducts 'A13' 'SUPUESTO CRITICO' '10'
    Set-Text $products $nsProducts 'B13' 'La receta presupone cuatro bolsas Sensi Pro (Grow A+B y Bloom A+B), Big Bud liquido comun y Sensi Cal Mag Xtra. Verificar las etiquetas antes del primer tanque.'
    Set-Text $products $nsProducts 'A14' 'LIMITACION DEL CALCULO' '10'
    Set-Text $products $nsProducts 'B14' 'Los ppm de liquidos usan densidad aproximada 1 g/ml y analisis de etiqueta. La EC medida manda sobre la estimacion teorica.'
    $productDimension = $products.SelectSingleNode('//s:dimension', $nsProducts)
    if ($productDimension) { $productDimension.SetAttribute('ref', 'A1:E14') }
    foreach ($column in 1..5) { Set-ColumnWidth $products $nsProducts $column @(24,58,34,42,60)[$column-1] }

    $sheetName = $workbook.SelectSingleNode('//*[local-name()="sheet"][@name="Sheet1"]')
    if ($sheetName) { $sheetName.SetAttribute('name', 'Nutricion coco') }
    $additiveName = $workbook.SelectSingleNode('//*[local-name()="sheet"][@name="Aditivos"]')
    if ($additiveName) { $additiveName.SetAttribute('name', 'Composicion') }
    $definedNames = $workbook.SelectSingleNode('//*[local-name()="definedNames"]')
    if ($definedNames) { [void]$definedNames.ParentNode.RemoveChild($definedNames) }
    $calc = $workbook.SelectSingleNode('//*[local-name()="calcPr"]')
    if (-not $calc) {
        $calc = $workbook.CreateElement('calcPr', $mainNs)
        [void]$workbook.DocumentElement.AppendChild($calc)
    }
    $calc.SetAttribute('calcMode', 'auto')
    $calc.SetAttribute('fullCalcOnLoad', '1')
    $calc.SetAttribute('forceFullCalc', '1')

    Write-Entry $zip 'xl\worksheets\sheet1.xml' $sheet
    Write-Entry $zip 'xl\worksheets\sheet2.xml' $products
    Write-Entry $zip 'xl\workbook.xml' $workbook
}
finally { $zip.Dispose() }

Get-Item -LiteralPath $OutputPath | Select-Object FullName, Length, LastWriteTime
