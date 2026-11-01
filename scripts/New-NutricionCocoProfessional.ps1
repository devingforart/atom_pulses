param(
    [Parameter(Mandatory = $true)] [string]$InputPath,
    [Parameter(Mandatory = $true)] [string]$OutputPath,
    [switch]$Fer
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

function Column-Letters([int]$number) {
    $letters = ''
    while ($number -gt 0) {
        $number--
        $letters = ([string][char](65 + ($number % 26))) + $letters
        $number = [int][math]::Floor($number / 26)
    }
    return $letters
}

function Shift-FormulaColumns([string]$formula, [int]$start, [int]$offset) {
    return [regex]::Replace($formula, '(\$?)([A-Z]{1,3})(\$?)(\d+)', {
        param($match)
        $column = Column-Number $match.Groups[2].Value
        if ($column -lt $start) { return $match.Value }
        return $match.Groups[1].Value + (Column-Letters ($column + $offset)) + $match.Groups[3].Value + $match.Groups[4].Value
    })
}

function Shift-SheetColumns($xml, $ns, [int]$start, [int]$offset) {
    foreach ($cell in @($xml.SelectNodes('//s:sheetData/s:row/s:c', $ns))) {
        $column = Column-Number $cell.r
        if ($column -ge $start) {
            $cell.SetAttribute('r', (Column-Letters ($column + $offset)) + ([regex]::Match($cell.r, '\d+').Value))
        }
        $formula = $cell.SelectSingleNode('s:f', $ns)
        if ($formula) { $formula.InnerText = Shift-FormulaColumns $formula.InnerText $start $offset }
    }
    foreach ($col in @($xml.SelectNodes('//s:cols/s:col', $ns))) {
        if ([int]$col.min -ge $start) { $col.SetAttribute('min', [string]([int]$col.min + $offset)) }
        if ([int]$col.max -ge $start) { $col.SetAttribute('max', [string]([int]$col.max + $offset)) }
    }
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
    Set-Formula $sheet $ns 'B3' 'Composicion!$B$2' '1'
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
        AJ='Base activa por parte (g/L)'; AK='N min (ppm)'; AL='N max (ppm)'; AM='P min (ppm)'; AN='P max (ppm)'
        AO='K min (ppm)'; AP='K max (ppm)'; AQ='Ca min (ppm)'; AR='Ca max (ppm)'; AS='Mg min (ppm)'
        AT='Mg max (ppm)'; AU='S min (ppm)'; AV='S max (ppm)'; AW='Control de bandas'; AX='SiO2: objetivo'
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
    # Bandas orientativas de la solucion de entrada (mg/L = ppm elemental).
    # N/P/Mg tienen apoyo experimental; K/Ca/S son bandas operativas, no umbrales de toxicidad.
    $bands = @(
        @(100,145,12,35,75,160,70,125,20,50,12,80),
        @(130,175,15,40,95,185,85,145,30,60,15,85),
        @(145,185,20,45,110,205,95,155,35,70,20,90),
        @(165,200,30,70,120,240,90,155,30,65,20,95),
        @(160,200,35,80,140,260,85,155,30,65,20,95),
        @(165,200,35,80,150,270,85,160,30,65,20,95),
        @(165,200,35,80,150,270,85,160,30,65,20,95),
        @(140,190,25,70,110,240,75,145,25,60,18,90),
        @(90,155,15,60,70,205,55,125,20,55,15,85)
    )
    $bandColumns = @('AK','AL','AM','AN','AO','AP','AQ','AR','AS','AT','AU','AV')

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
        for ($j = 0; $j -lt $bandColumns.Count; $j++) {
            Set-Number $sheet $ns "$($bandColumns[$j])$row" $bands[$i][$j] '1'
        }
        $checks = @(
            "IF(OR(Z$row<AK$row,Z$row>AL$row),`"N `",`"`")",
            "IF(OR(AA$row<AM$row,AA$row>AN$row),`"P `",`"`")",
            "IF(OR(AB$row<AO$row,AB$row>AP$row),`"K `",`"`")",
            "IF(OR(AC$row<AQ$row,AC$row>AR$row),`"Ca `",`"`")",
            "IF(OR(AD$row<AS$row,AD$row>AT$row),`"Mg `",`"`")",
            "IF(OR(AE$row<AU$row,AE$row>AV$row),`"S `",`"`")"
        )
        Set-Formula $sheet $ns "AW$row" "IF(LEN(TRIM($($checks -join '&')))=0,`"EN BANDA`",`"REVISAR: `"&TRIM($($checks -join '&')))"
        Set-Text $sheet $ns "AX$row" 'Sin rango validado; confirmar SiO2 en la etiqueta regional.'
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
    Set-Text $sheet $ns 'A34' 'LECTURA DE LOS RANGOS PPM' '1'
    Set-Text $sheet $ns 'B35' 'AK:AV son bandas orientativas de ppm elementales en la solucion final, no limites de toxicidad ni un objetivo de EC. AW compara las dosis calculadas con esas bandas.'
    Set-Text $sheet $ns 'B36' 'N y P: ensayos en vegetativo y floracion sin suelo. Mg: ensayo vegetativo 35-70 ppm; extrapolacion prudente a floracion. K, Ca y S: bandas operativas, no optimos demostrados para este cultivar.'
    Set-Text $sheet $ns 'B37' 'El calculo supone liquidos con densidad 1 g/ml y composicion de etiqueta. Si ajustas Cal Mag o A/B para alcanzar EC medida, modifica su cantidad en la fila para actualizar ppm y alertas.'
    Set-Text $sheet $ns 'B38' 'SiO2: la etiqueta regional de Rhino Skin varia; no se fija un rango elemental confiable. No aumentar productos solo para borrar una alerta sin comprobar etiqueta, agua y drenaje.'
    Set-Text $sheet $ns 'A40' 'FUENTES DE LAS BANDAS' '1'
    Set-Text $sheet $ns 'B41' 'N vegetativo: https://www.frontiersin.org/journals/plant-science/articles/10.3389/fpls.2020.572293/full'
    Set-Text $sheet $ns 'B42' 'N/P floracion: https://www.frontiersin.org/journals/plant-science/articles/10.3389/fpls.2021.764103/full'
    Set-Text $sheet $ns 'B43' 'Mg vegetativo: https://www.mdpi.com/2223-7747/12/14/2676'
    if ($dimension) { $dimension.SetAttribute('ref', 'A1:AX43') }
    foreach ($column in 4..12) { Set-ColumnWidth $sheet $ns $column 18 }
    Set-ColumnWidth $sheet $ns 20 58
    foreach ($column in 24..25) { Set-ColumnWidth $sheet $ns $column 34 }
    foreach ($column in 33..35) { Set-ColumnWidth $sheet $ns $column 32 }
    foreach ($column in 37..48) { Set-ColumnWidth $sheet $ns $column 14 }
    Set-ColumnWidth $sheet $ns 49 25
    Set-ColumnWidth $sheet $ns 50 48

    # Per-product elemental ppm calculator, using the same label assumptions as sheet 1.
    foreach ($row in @($products.SelectNodes('//s:sheetData/s:row', $nsProducts))) {
        [void]$row.ParentNode.RemoveChild($row)
    }
    foreach ($name in @('autoFilter','mergeCells','dataValidations')) {
        foreach ($node in @($products.SelectNodes("//s:$name", $nsProducts))) {
            [void]$node.ParentNode.RemoveChild($node)
        }
    }
    foreach ($pane in @($products.SelectNodes('//s:sheetViews/s:sheetView/s:pane', $nsProducts))) {
        [void]$pane.ParentNode.RemoveChild($pane)
    }
    Set-Text $products $nsProducts 'A1' 'COMPOSICION Y APORTE POR PRODUCTO (ppm = mg/L)' '10'
    Set-Text $products $nsProducts 'A2' 'Litros del tanque' '10'
    Set-Number $products $nsProducts 'B2' 40 '1'
    Set-Text $products $nsProducts 'D2' 'Semana (1-9)' '10'
    Set-Number $products $nsProducts 'E2' 6 '1'
    Set-Text $products $nsProducts 'A4' 'Cambiar B2 actualiza las cantidades de la receta. Si esas cantidades escalan, los ppm por litro no cambian.'
    Set-Text $products $nsProducts 'A5' 'La columna C permite probar una cantidad fija/manual sin modificar la receta principal; dejarla vacia para usar la receta.'
    $ppmHeaders = [ordered]@{
        A='Producto'; B='Receta tanque'; C='Cantidad manual'; D='Unidad'; E='Dosis usada/L'
        F='N ppm'; G='P ppm'; H='K ppm'; I='Ca ppm'; J='Mg ppm'; K='S ppm'; L='SiO2 ppm'; M='Nota'
    }
    foreach ($column in $ppmHeaders.Keys) { Set-Text $products $nsProducts "$column`6" $ppmHeaders[$column] '10' }
    $contributions = @(
        @('Sensi Grow A Pro','D','g','0.09','0.10*0.4364','0.28*0.8301','0','0.03','0.048','0','NPK en oxidos convertido a P/K elemental'),
        @('Sensi Grow B Pro','E','g','0.15','0','0','0.185','0','0','0',''),
        @('Sensi Bloom A Pro','F','g','0.10','0.14*0.4364','0.26*0.8301','0','0.0285','0.0378','0','NPK en oxidos convertido a P/K elemental'),
        @('Sensi Bloom B Pro','G','g','0.17','0','0.06*0.8301','0.14','0','0','0',''),
        @('Rhino Skin','H','ml','0','0','0.004*0.8301','0','0','0','0.0015','SiO2 regional aproximado'),
        @('Sensi Cal Mag Xtra','I','ml','0.04','0','0','0.032','0.011','0','0','Densidad supuesta 1 g/ml'),
        @('Big Bud liquido','J','ml','0','0.01*0.4364','0.03*0.8301','0','0','0','0','Densidad supuesta 1 g/ml'),
        @('Bud Candy','K','ml','0.008','0','0','0','0.005','0','0','N/Mg segun etiqueta regional; comprobar'),
        @('Bud Factor X','L','ml','0','0','0','0','0.005','0','0','Mg segun etiqueta regional; comprobar')
    )
    for ($i = 0; $i -lt $contributions.Count; $i++) {
        $row = 7 + $i
        $item = $contributions[$i]
        Set-Text $products $nsProducts "A$row" $item[0]
        $mainColumn = $item[1]
        Set-Formula $products $nsProducts "B$row" "IFERROR(INDEX('Nutricion coco'!`$$mainColumn`$12:`$$mainColumn`$20,`$E`$2),0)" '1'
        Set-Text $products $nsProducts "D$row" $item[2]
        Set-Formula $products $nsProducts "E$row" "IFERROR(IF(C$row=`"`",B$row,C$row)/`$B`$2,0)" '1'
        for ($j = 0; $j -lt 7; $j++) {
            $outputColumn = @('F','G','H','I','J','K','L')[$j]
            Set-Formula $products $nsProducts "$outputColumn$row" "E$row*($($item[3+$j]))*1000" '1'
        }
        if ($item[10]) { Set-Text $products $nsProducts "M$row" $item[10] }
    }
    Set-Text $products $nsProducts 'A16' 'TOTAL ppm' '10'
    foreach ($column in @('F','G','H','I','J','K','L')) {
        Set-Formula $products $nsProducts "$column`16" "SUM($column`7:$column`15)" '1'
    }
    Set-Text $products $nsProducts 'A18' 'ppm = mg del elemento por litro de solucion; P y K son elementales, no P2O5/K2O.'
    Set-Text $products $nsProducts 'A19' 'Liquidos calculados con densidad aproximada de 1 g/ml. Los micronutrientes no se cuantifican sin etiquetas regionales completas.'
    Set-Text $products $nsProducts 'A20' 'Una cantidad manual solo cambia este calculador. La hoja Nutricion coco conserva la receta original.'
    Set-Text $products $nsProducts 'A22' 'PRODUCTO' '10'
    Set-Text $products $nsProducts 'B22' 'COMPOSICION DECLARADA / INVESTIGADA' '10'
    Set-Text $products $nsProducts 'C22' 'FUNCION EN ESTA RECETA' '10'
    Set-Text $products $nsProducts 'D22' 'REGLA DE USO' '10'
    Set-Text $products $nsProducts 'E22' 'FUENTE' '10'
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
        $row = 23 + $i
        Set-Text $products $nsProducts "A$row" $reference[$i][0]
        Set-Text $products $nsProducts "B$row" $reference[$i][1]
        Set-Text $products $nsProducts "C$row" $reference[$i][2]
        Set-Text $products $nsProducts "D$row" $reference[$i][3]
        Set-Text $products $nsProducts "E$row" $reference[$i][4]
    }
    Set-Text $products $nsProducts 'A34' 'SUPUESTO CRITICO' '10'
    Set-Text $products $nsProducts 'B34' 'La receta presupone cuatro bolsas Sensi Pro (Grow A+B y Bloom A+B), Big Bud liquido comun y Sensi Cal Mag Xtra. Verificar las etiquetas antes del primer tanque.'
    Set-Text $products $nsProducts 'A35' 'LIMITACION DEL CALCULO' '10'
    Set-Text $products $nsProducts 'B35' 'La EC medida manda sobre la estimacion teorica; los ppm no equivalen a la lectura EC.'
    $productDimension = $products.SelectSingleNode('//s:dimension', $nsProducts)
    if ($productDimension) { $productDimension.SetAttribute('ref', 'A1:M35') }
    foreach ($column in 1..13) { Set-ColumnWidth $products $nsProducts $column @(25,58,34,42,60,15,15,15,15,15,15,16,43)[$column-1] }

    if ($Fer) {
        Shift-SheetColumns $sheet $ns 13 2
        Set-Text $sheet $ns 'A1' 'CULTIVO FER - COCO / RO / SENSI PROFESSIONAL' '1'
        Set-Text $sheet $ns 'E6' 'Hora OFF vegetativo' '10'
        Set-Number $sheet $ns 'F6' (11.0 / 24.0) '5'
        Set-Formula $sheet $ns 'B7' 'ROUND(MOD(F6-B6,1)*24,4)' '4'
        Set-Text $sheet $ns 'D7' 'Horas calculadas desde ON/OFF vegetativo; editar B6 y F6.'
        Set-Text $sheet $ns 'E8' 'Hora OFF floracion' '10'
        Set-Number $sheet $ns 'F8' 0 '8'
        Set-Formula $sheet $ns 'B9' 'ROUND(MOD(F8-B8,1)*24,4)' '7'
        Set-Text $sheet $ns 'D9' 'Horas calculadas desde ON/OFF floracion; editar B8 y F8.'
        Set-Text $sheet $ns 'M11' 'Epsom MgSO4 7H2O (g)' '10'
        Set-Text $sheet $ns 'N11' 'Factor A+B (editable)' '10'
        Set-Text $sheet $ns 'H10' 'EC RO + Cal Mag MEDIDA'
        Set-Number $sheet $ns 'I10' 0 '1'
        Set-Text $sheet $ns 'J10' 'Registrar lectura real. No subir Cal Mag solo para llegar a una EC previa fija; medir EC final tras la mezcla.'
        $epsomPerL = @(0,0.11,0.12,0,0.07,0.06,0.06,0,0)
        for ($i = 0; $i -lt 9; $i++) {
            $row = 12 + $i
            $dose = $basePerL[$i].ToString([Globalization.CultureInfo]::InvariantCulture)
            $epsom = $epsomPerL[$i].ToString([Globalization.CultureInfo]::InvariantCulture)
            Set-Number $sheet $ns "N$row" 1 '1'
            Set-Formula $sheet $ns "M$row" "`$B`$3*$epsom" '1'
            if ($i -le 2) {
                Set-Formula $sheet $ns "D$row" "`$B`$3*$dose*N$row" '1'
                Set-Formula $sheet $ns "E$row" "`$B`$3*$dose*N$row" '1'
            } else {
                Set-Formula $sheet $ns "F$row" "`$B`$3*$dose*N$row" '1'
                Set-Formula $sheet $ns "G$row" "`$B`$3*$dose*N$row" '1'
            }
            Set-Formula $sheet $ns "AF$row" "(D$row*0.03+F$row*0.0285+I$row*0.011+K$row*0.005+L$row*0.005+M$row*0.0986)*1000/`$B`$3"
            Set-Formula $sheet $ns "AG$row" "(D$row*0.048+F$row*0.0378+M$row*0.1300)*1000/`$B`$3"
            $scheduleNode = $sheet.SelectSingleNode("//s:c[@r='V$row']/s:f", $ns)
            if ($scheduleNode) {
                $minimumHours = if ($i -le 2) { 4 } elseif ($i -eq 8) { 8 } else { 6 }
                $durationCell = if ($i -le 2) { '$B$7' } else { '$B$9' }
                Set-Formula $sheet $ns "V$row" "IF($durationCell<=$minimumHours,`"REVISAR ON/OFF`",$($scheduleNode.InnerText))" '1'
            }
            Set-Text $sheet $ns "AJ$row" 'Cal Mag es una dosis de partida, no una meta de EC previa. Registrar lo agregado realmente; el Mg adicional viene de Epsom.'
            if ($i -gt 2) {
                Set-Formula $sheet $ns "AI$row" 'IF(AND($F$10<>"CANDY",$F$10<>"FACTOR X"),"MODO INVALIDO",IF($F$10="FACTOR X","Tanque X: Factor X; Candy = 0","Tanque regular: Candy; Factor X = 0"))'
            }
            Set-Text $sheet $ns "BA$row" ''
            Set-Text $sheet $ns "BB$row" ''
            Set-Text $sheet $ns "BC$row" ''
            Set-Text $sheet $ns "BD$row" ''
            $minEc = [double](($ecTargets[$i] -split ' - ')[0]); $maxEc = [double](($ecTargets[$i] -split ' - ')[1])
            $minPh = [double](($phTargets[$i] -split ' - ')[0]); $maxPh = [double](($phTargets[$i] -split ' - ')[1])
            $a = $minEc.ToString([Globalization.CultureInfo]::InvariantCulture)
            $b = $maxEc.ToString([Globalization.CultureInfo]::InvariantCulture)
            $c = $minPh.ToString([Globalization.CultureInfo]::InvariantCulture)
            $d = $maxPh.ToString([Globalization.CultureInfo]::InvariantCulture)
            Set-Formula $sheet $ns "BE$row" "IF(OR(`$B`$3<=0,N$row<=0),`"REVISAR LITROS/FACTOR`",IF(AND(A$row>=4,`$F`$10<>`"CANDY`",`$F`$10<>`"FACTOR X`"),`"MODO INVALIDO`",IF(OR(BA$row=`"`",BB$row=`"`"),`"MEDIR EC/pH`",IF(AND(BA$row>=$a,BA$row<=$b,BB$row>=$c,BB$row<=$d),`"EC/pH EN RANGO`",`"REVISAR EC/pH`"))))"
        }
        Set-Text $sheet $ns 'BA11' 'EC final medida' '10'
        Set-Text $sheet $ns 'BB11' 'pH final medido' '10'
        Set-Text $sheet $ns 'BC11' 'EC drenaje medida' '10'
        Set-Text $sheet $ns 'BD11' 'Runoff real (%)' '10'
        Set-Text $sheet $ns 'BE11' 'Control EC/pH real' '10'
        Set-Text $sheet $ns 'B23' 'Agua RO 70-80% > Rhino Skin > mezclar > Cal Mag segun dosis > Grow/Bloom A prediluida > B prediluida > Epsom disuelto aparte > Big Bud > Candy O Factor X > completar volumen > medir EC > ajustar pH.'
        Set-Text $sheet $ns 'B25' 'Si EC final no coincide, cambiar el Factor A+B de la semana (columna N), volver a mezclar y medir. No modificar una sola parte. Revisar ppm y alertas despues del cambio.'
        Set-Text $sheet $ns 'B26' 'La dosis de Cal Mag es punto de partida. No perseguir una EC previa de 0,3: si se cambia la cantidad, ingresar el valor real en columna I para actualizar los ppm.'
        Set-Text $sheet $ns 'B27' 'I10 registra la EC observada tras Cal Mag, no es un objetivo. BA/BB registran EC/pH finales; BC/BD son mediciones reales de drenaje.'
        Set-Text $sheet $ns 'B35' 'AM:AX son bandas orientativas de ppm elementales, no limites de toxicidad. AY compara las dosis calculadas con esas bandas.'
        Set-Text $sheet $ns 'B37' 'Los ppm de liquidos suponen densidad 1 g/ml. Si cambias dosis de Cal Mag, Big Bud o aditivos, ingresa la cantidad real en su celda de la semana.'
        Set-Text $sheet $ns 'A45' 'GUIA DE USO - LEER ANTES DE PREPARAR EL TANQUE' '10'
        $guide = @(
            '1. En Composicion!B2 poner los litros FINALES del tanque; B3 de esta hoja queda enlazada. No dejar cero.',
            '2. Elegir la semana por estado real de la planta. Las semanas 4-9 son de floracion; no usar la tabla como calendario rigido de cosecha.',
            '3. Revisar el producto exacto: Grow/Bloom PROFESSIONAL en polvo, Cal Mag Xtra y Big Bud LIQUIDO. Una variante distinta requiere recalcular.',
            '4. Usar A y B de la misma fase en igual peso. N de la semana es el factor editable; iniciar en 1. No mezclar concentrados entre si.',
            '5. M es Epsom / sulfato de magnesio HEPTAHIDRATADO: 9,86% Mg y 13,0% S. No usar otra hidratacion sin corregir formulas.',
            '6. Preparar en el orden de B23, agitar entre productos y completar a los litros finales antes de evaluar la EC.',
            '7. Anotar en I10 la EC despues de Cal Mag, BA la EC final y BB el pH final. Ajustar N en pasos pequenos si la EC difiere; volver a medir.',
            '8. Si la dosis REAL de un liquido o Epsom difiere, sobrescribir su celda de esa semana: los ppm cambiaran, pero la formula de escalado quedara sustituida.',
            '9. Preparar Candy O Factor X con F10. Al usar Factor X, Candy queda en cero. Evitar estancamiento de organicos en tanque/lineas.',
            '10. BC/BD son drenaje REAL. Runoff y dryback objetivos son guias; observar volumen, peso de maceta, EC/pH del drenaje y planta.',
            '11. En Composicion, E2 elige la semana y C7:C16 permite simular cantidades manuales sin modificar la receta principal.',
            '12. Los ppm son estimados por etiqueta y densidad. Ni EC ni sintomas identifican por si solos un mineral; confirmar etiqueta regional y medir.'
        )
        for ($i = 0; $i -lt $guide.Count; $i++) { Set-Text $sheet $ns "B$($i+46)" $guide[$i] }
        Set-Text $sheet $ns 'B58' '13. Horarios: editar ON vegetativo B6 y OFF F6; ON floracion B8 y OFF F8. B7/B9 calculan horas incluso cruzando medianoche. Primer pulso ON+2 h; ultimo OFF-2 h en vege, OFF-4 h en flora y OFF-6 h al final. Todos los pulsos intermedios se redistribuyen.'
        $dimension.SetAttribute('ref', 'A1:BE58')
        Set-ColumnWidth $sheet $ns 13 22
        Set-ColumnWidth $sheet $ns 14 21
        foreach ($column in 53..57) { Set-ColumnWidth $sheet $ns $column 21 }

        Set-Text $products $nsProducts 'A16' 'Epsom / MgSO4 7H2O'
        Set-Formula $products $nsProducts 'A3' 'IF(OR(B2<=0,E2<1,E2>9),"REVISAR LITROS/SEMANA","")'
        Set-Formula $products $nsProducts 'B16' 'IFERROR(INDEX(''Nutricion coco''!$M$12:$M$20,$E$2),0)' '1'
        Set-Text $products $nsProducts 'D16' 'g'
        Set-Formula $products $nsProducts 'E16' 'IFERROR(IF(C16="",B16,C16)/$B$2,0)' '1'
        foreach ($column in @('F','G','H','I','L')) { Set-Number $products $nsProducts "$column`16" 0 '1' }
        Set-Formula $products $nsProducts 'J16' 'E16*0.0986*1000' '1'
        Set-Formula $products $nsProducts 'K16' 'E16*0.1300*1000' '1'
        Set-Text $products $nsProducts 'M16' 'Solo heptahidratado; aporte de Mg y S'
        Set-Text $products $nsProducts 'A17' 'TOTAL ppm' '10'
        foreach ($column in @('F','G','H','I','J','K','L')) {
            Set-Formula $products $nsProducts "$column`17" "SUM($column`7:$column`16)" '1'
        }
        Set-Text $products $nsProducts 'A36' 'Epsom / MgSO4 7H2O' '10'
        Set-Text $products $nsProducts 'B36' 'Mg 9,86%; S 13,0%; confirmar que el envase diga heptahidratado.'
        Set-Text $products $nsProducts 'C36' 'Correccion independiente de Mg sin subir N/Ca.'
        Set-Text $products $nsProducts 'D36' 'Disolver aparte en agua y agregar despues de A+B diluidas.'
        $productDimension.SetAttribute('ref', 'A1:M36')
    }

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
