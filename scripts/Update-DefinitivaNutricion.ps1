param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$script:SpreadsheetNs = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'

function Read-ZipXml {
    param($Zip, [string]$EntryName)
    $entry = $Zip.Entries | Where-Object FullName -eq $EntryName
    if (-not $entry) { throw "No se encontro $EntryName" }
    $reader = [System.IO.StreamReader]::new($entry.Open())
    try { return [xml]$reader.ReadToEnd() } finally { $reader.Dispose() }
}

function Write-ZipXml {
    param($Zip, [string]$EntryName, [xml]$Document)
    $entry = $Zip.Entries | Where-Object FullName -eq $EntryName
    if ($entry) { $entry.Delete() }
    $newEntry = $Zip.CreateEntry($EntryName, [System.IO.Compression.CompressionLevel]::Optimal)
    $settings = [System.Xml.XmlWriterSettings]::new()
    $settings.Encoding = [System.Text.UTF8Encoding]::new($false)
    $settings.Indent = $false
    $settings.OmitXmlDeclaration = $false
    $writer = [System.Xml.XmlWriter]::Create($newEntry.Open(), $settings)
    try { $Document.Save($writer) } finally { $writer.Dispose() }
}

function Get-NamespaceManager {
    param([xml]$Document)
    $manager = [System.Xml.XmlNamespaceManager]::new($Document.NameTable)
    $manager.AddNamespace('s', $script:SpreadsheetNs)
    return ,$manager
}

function Get-ColumnIndex {
    param([string]$Reference)
    $letters = ($Reference -replace '[^A-Z]', '')
    $result = 0
    foreach ($char in $letters.ToCharArray()) {
        $result = ($result * 26) + ([int][char]$char - [int][char]'A' + 1)
    }
    return $result
}

function Get-OrCreateRow {
    param([xml]$Document, $NamespaceManager, [int]$RowNumber)
    $sheetData = $Document.SelectSingleNode('//s:sheetData', $NamespaceManager)
    $row = $sheetData.SelectSingleNode("s:row[@r='$RowNumber']", $NamespaceManager)
    if ($row) { return $row }

    $row = $Document.CreateElement('row', $script:SpreadsheetNs)
    $row.SetAttribute('r', [string]$RowNumber)
    $insertBefore = $null
    foreach ($candidate in $sheetData.SelectNodes('s:row', $NamespaceManager)) {
        if ([int]$candidate.r -gt $RowNumber) { $insertBefore = $candidate; break }
    }
    if ($insertBefore) { [void]$sheetData.InsertBefore($row, $insertBefore) }
    else { [void]$sheetData.AppendChild($row) }
    return $row
}

function Get-OrCreateCell {
    param([xml]$Document, $NamespaceManager, [string]$Reference)
    $rowNumber = [int]($Reference -replace '[^0-9]', '')
    $row = Get-OrCreateRow $Document $NamespaceManager $rowNumber
    $cell = $row.SelectSingleNode("s:c[@r='$Reference']", $NamespaceManager)
    if ($cell) { return $cell }

    $cell = $Document.CreateElement('c', $script:SpreadsheetNs)
    $cell.SetAttribute('r', $Reference)
    $columnIndex = Get-ColumnIndex $Reference
    $insertBefore = $null
    foreach ($candidate in $row.SelectNodes('s:c', $NamespaceManager)) {
        if ((Get-ColumnIndex $candidate.r) -gt $columnIndex) { $insertBefore = $candidate; break }
    }
    if ($insertBefore) { [void]$row.InsertBefore($cell, $insertBefore) }
    else { [void]$row.AppendChild($cell) }
    return $cell
}

function Reset-Cell {
    param($Cell)
    while ($Cell.HasChildNodes) { [void]$Cell.RemoveChild($Cell.FirstChild) }
    [void]$Cell.RemoveAttribute('t')
}

function Set-CellText {
    param([xml]$Document, $NamespaceManager, [string]$Reference, [string]$Value, [string]$Style = '')
    $cell = Get-OrCreateCell $Document $NamespaceManager $Reference
    Reset-Cell $cell
    $cell.SetAttribute('t', 'inlineStr')
    if ($Style) { $cell.SetAttribute('s', $Style) }
    $inline = $Document.CreateElement('is', $script:SpreadsheetNs)
    $text = $Document.CreateElement('t', $script:SpreadsheetNs)
    $text.InnerText = $Value
    [void]$inline.AppendChild($text)
    [void]$cell.AppendChild($inline)
}

function Set-CellNumber {
    param([xml]$Document, $NamespaceManager, [string]$Reference, [double]$Value, [string]$Style = '')
    $cell = Get-OrCreateCell $Document $NamespaceManager $Reference
    Reset-Cell $cell
    if ($Style) { $cell.SetAttribute('s', $Style) }
    $valueNode = $Document.CreateElement('v', $script:SpreadsheetNs)
    $valueNode.InnerText = $Value.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    [void]$cell.AppendChild($valueNode)
}

function Set-CellFormula {
    param([xml]$Document, $NamespaceManager, [string]$Reference, [string]$Formula, [string]$Style = '')
    $cell = Get-OrCreateCell $Document $NamespaceManager $Reference
    Reset-Cell $cell
    if ($Style) { $cell.SetAttribute('s', $Style) }
    $formulaNode = $Document.CreateElement('f', $script:SpreadsheetNs)
    $formulaNode.InnerText = $Formula
    [void]$cell.AppendChild($formulaNode)
}

function Clear-Row {
    param([xml]$Document, $NamespaceManager, [int]$RowNumber)
    $row = Get-OrCreateRow $Document $NamespaceManager $RowNumber
    foreach ($cell in @($row.SelectNodes('s:c', $NamespaceManager))) { [void]$row.RemoveChild($cell) }
}

function Get-ColumnNumber {
    param([string]$Reference)
    $letters = ([regex]::Match($Reference, '^[A-Z]+')).Value
    $number = 0
    foreach ($character in $letters.ToCharArray()) {
        $number = ($number * 26) + ([int]$character - [int][char]'A' + 1)
    }
    return $number
}

function Reorder-Sheet1PlanColumns {
    param([xml]$Document, $NamespaceManager)

    # D:K = insumos, L:X = objetivos/operacion, Y:AI = control quimico.
    $columnMap = [ordered]@{
        'K'='L'; 'L'='M'; 'M'='N'; 'N'='O'; 'O'='P'; 'P'='Q'; 'Q'='R'
        'R'='S'; 'S'='T'; 'T'='U'; 'U'='V'; 'V'='W'; 'W'='X'; 'X'='Y'
        'Y'='Z'; 'Z'='AA'; 'AA'='AB'; 'AB'='AC'; 'AC'='AD'; 'AD'='AE'
        'AE'='AF'; 'AF'='AG'; 'AG'='K'; 'AH'='AH'; 'AI'='AI'
    }

    foreach ($rowNumber in 11..20) {
        $row = Get-OrCreateRow $Document $NamespaceManager $rowNumber
        $copies = @()
        foreach ($sourceColumn in $columnMap.Keys) {
            $source = $row.SelectSingleNode("s:c[@r='$sourceColumn$rowNumber']", $NamespaceManager)
            if ($source) {
                $copy = $source.CloneNode($true)
                $copy.SetAttribute('r', "$($columnMap[$sourceColumn])$rowNumber")
                $copies += $copy
            }
        }
        foreach ($sourceColumn in $columnMap.Keys) {
            $source = $row.SelectSingleNode("s:c[@r='$sourceColumn$rowNumber']", $NamespaceManager)
            if ($source) { [void]$row.RemoveChild($source) }
        }
        foreach ($copy in $copies) { [void]$row.AppendChild($copy) }

        $orderedCells = @($row.SelectNodes('s:c', $NamespaceManager) | Sort-Object { Get-ColumnNumber $_.GetAttribute('r') })
        foreach ($cell in $orderedCells) { [void]$row.AppendChild($cell) }
    }

    $referencePattern = '(?<![A-Z0-9_])(?<colabs>\$?)(?<col>AG|AH|AI|AA|AB|AC|AD|AE|AF|[K-Z])(?<rowabs>\$?)(?<row>1[1-9]|20)(?!\d)'
    foreach ($formula in @($Document.SelectNodes('//s:f', $NamespaceManager))) {
        $formula.InnerText = [regex]::Replace($formula.InnerText, $referencePattern, {
            param($match)
            $mappedColumn = $columnMap[$match.Groups['col'].Value]
            return $match.Groups['colabs'].Value + $mappedColumn + $match.Groups['rowabs'].Value + $match.Groups['row'].Value
        })
    }
}

if (-not (Test-Path -LiteralPath $InputPath)) { throw "No existe el archivo de entrada: $InputPath" }
Copy-Item -LiteralPath $InputPath -Destination $OutputPath -Force

$zip = [System.IO.Compression.ZipFile]::Open($OutputPath, [System.IO.Compression.ZipArchiveMode]::Update)
try {
    $sheet1 = Read-ZipXml $zip 'xl\worksheets\sheet1.xml'
    $additives = Read-ZipXml $zip 'xl\worksheets\sheet2.xml'
    $workbook = Read-ZipXml $zip 'xl\workbook.xml'
    $ns1 = Get-NamespaceManager $sheet1
    $ns2 = Get-NamespaceManager $additives

    # Receta revisada: la base 20-20-20 queda en dosis baja para aportar trazas
    # sin elevar en exceso la fraccion de nitrogeno amoniacal. Las demas sales
    # completan los objetivos elementales de cada semana.
    Set-CellText $sheet1 $ns1 'F11' 'Nitrato K 99% (g) - 13,7% N / 46,1% K2O' '10'
    Set-CellText $sheet1 $ns1 'H11' 'Sulfato K 99% (g) - 44,4% K / 18,2% S' '10'
    Set-CellText $sheet1 $ns1 'W11' 'Perfil elemental calculado (ppm)' '10'
    Set-CellText $sheet1 $ns1 'AG11' 'Silicato de potasio Kraff (g)' '10'
    Set-CellText $sheet1 $ns1 'AH11' 'Si aportado (ppm)' '10'
    Set-CellText $sheet1 $ns1 'AI11' 'K del silicato (ppm)' '10'
    $weeklyRatesPer100L = @(
        @(5.0,52.6,20.7,29.9,6.9,11.3),
        @(5.0,68.4,24.8,35.0,9.6,15.7),
        @(5.0,73.7,29.8,40.1,8.4,17.9),
        @(5.0,73.7,29.8,40.1,9.2,20.1),
        @(5.0,71.1,29.1,42.1,8.4,22.3),
        @(5.0,68.4,28.4,42.1,8.1,20.1),
        @(5.0,60.5,26.4,38.0,8.2,15.7),
        @(5.0,44.7,15.1,31.9,11.7,11.3),
        @(3.0,21.1,4.6,20.0,7.2,5.5)
    )
    $nutrientColumns = @('D','E','F','G','H','I')
    $ecTargets = @('1.2 - 1.5','1.4 - 1.7','1.6 - 1.9','1.7 - 2.0','1.8 - 2.1','1.8 - 2.1','1.6 - 1.9','1.3 - 1.6','0.8 - 1.1')
    for ($i = 0; $i -lt $weeklyRatesPer100L.Count; $i++) {
        $row = 12 + $i
        for ($j = 0; $j -lt $nutrientColumns.Count; $j++) {
            $rate = ([double]$weeklyRatesPer100L[$i][$j]).ToString([System.Globalization.CultureInfo]::InvariantCulture)
            Set-CellFormula $sheet1 $ns1 "$($nutrientColumns[$j])$row" "`$B`$3*($rate/100)" '1'
        }
        Set-CellText $sheet1 $ns1 "K$row" $ecTargets[$i]
        if ($row -lt 20) {
            Set-CellFormula $sheet1 $ns1 "AG$row" "`$B`$3*'Aditivos'!`$H`$7/(('Aditivos'!`$K`$6/100)*0.46744*1000)" '1'
        }
        else {
            Set-CellNumber $sheet1 $ns1 "AG$row" 0 '1'
        }
        $baseK2SO4Rate = ([double]$weeklyRatesPer100L[$i][4]).ToString([System.Globalization.CultureInfo]::InvariantCulture)
        Set-CellFormula $sheet1 $ns1 "H$row" "MAX(0,`$B`$3*($baseK2SO4Rate/100)-AG$row*('Aditivos'!`$K`$7/100)*0.8301/('Aditivos'!`$H`$3/100))" '1'
        Set-CellFormula $sheet1 $ns1 "AH$row" "IFERROR(AG$row*('Aditivos'!`$K`$6/100)*0.46744*1000/`$B`$3,0)" '1'
        Set-CellFormula $sheet1 $ns1 "AI$row" "IFERROR(AG$row*('Aditivos'!`$K`$7/100)*0.8301*1000/`$B`$3,0)" '1'
        Set-CellFormula $sheet1 $ns1 "AC$row" "(((D$row*0.20+I$row*0.34+F$row*'Aditivos'!`$K`$4/100)*0.8301+H$row*'Aditivos'!`$H`$3/100)+AG$row*('Aditivos'!`$K`$7/100)*0.8301)*1000/`$B`$3"
        Set-CellFormula $sheet1 $ns1 "W$row" "ROUND(Y$row,0)&`" N / `"&ROUND(AB$row,0)&`" P / `"&ROUND(AC$row,0)&`" K / `"&ROUND(AD$row,0)&`" Ca / `"&ROUND(AE$row,0)&`" Mg / `"&ROUND(AF$row,0)&`" S / `"&ROUND(AH$row,1)&`" Si ppm | Silicato: `"&ROUND(AG$row,2)&`" g`""
    }
    Set-CellText $sheet1 $ns1 'B27' 'Orden: agua 70-80% > silicato de potasio Kraff prediluido 1:100 > 20-20-20 > Micro C > Calcinit prediluido > KNO3 > Epsom > K2SO4 > Bloom/MKP > completar > EC > pH.'
    Set-CellText $sheet1 $ns1 'B28' 'Silicato Kraff: objetivo 7 ppm Si en semanas 1-8; semana 9 sin silicio. Calculo basado en etiqueta: 19,5% SiO2 y 8,2% K2O. Dosificar por peso.'
    Set-CellText $sheet1 $ns1 'B30' 'La receta usa una dosis baja de 20-20-20 para trazas; Calcinit, KNO3, Epsom, K2SO4 y Bloom/MKP completan los macros. Micro C completa los micros.'

    Reorder-Sheet1PlanColumns $sheet1 $ns1

    $sheet1Dimension = $sheet1.SelectSingleNode('//s:dimension', $ns1)
    if ($sheet1Dimension) { $sheet1Dimension.SetAttribute('ref', 'A1:AI39') }

    Set-CellText $sheet1 $ns1 'J11' 'Solucion Micro C (ml)' '10'
    foreach ($row in 12..20) {
        Set-CellFormula $sheet1 $ns1 "J$row" "`$B`$3*'Aditivos'!`$B`$4/40" '1'
    }

    Set-CellText $additives $ns2 'A1' 'ADITIVOS Y MICRONUTRIENTES - NUTRICION DEFINITIVA' '1'
    Set-CellText $additives $ns2 'A2' 'Producto de referencia'
    Set-CellText $additives $ns2 'B2' 'Solucion madre Micro C personalizada'
    Set-CellText $additives $ns2 'A3' 'Litros de tanque'
    Set-CellFormula $additives $ns2 'B3' "'Sheet1'!`$B`$3"
    Set-CellText $additives $ns2 'C3' 'L'
    Set-CellText $additives $ns2 'A4' 'Dosis estandar Micro C'
    Set-CellNumber $additives $ns2 'B4' 20
    Set-CellText $additives $ns2 'C4' 'ml/40 L'
    Set-CellText $additives $ns2 'D4' 'Se escala automaticamente con el volumen real del tanque.'
    Set-CellText $additives $ns2 'A5' 'Volumen de solucion madre'
    Set-CellNumber $additives $ns2 'B5' 2
    Set-CellText $additives $ns2 'C5' 'L'
    Set-CellText $additives $ns2 'D5' 'Rinde 100 tanques de 40 L (4.000 L de solucion nutritiva).'

    foreach ($row in 7..14) { Clear-Row $additives $ns2 $row }
    $compositionHeaders = @('Elemento','Objetivo final (ppm)','Fuente requerida','Cantidad en stock 2 L (g)','Control de etiqueta')
    for ($i = 0; $i -lt $compositionHeaders.Count; $i++) {
        $col = [char]([int][char]'A' + $i)
        Set-CellText $additives $ns2 "$col`7" $compositionHeaders[$i] '9'
    }
    $composition = @(
        @('Fe',2.10,'Fe-EDTA solido 13% Fe',64.60,'No usar hierro liquido 4% ni EDDHA en esta receta.'),
        @('Mn',0.60,'Sulfato de manganeso 32% Mn',7.50,'Confirmar porcentaje de Mn en la etiqueta.'),
        @('Zn',0.12,'Sulfato de zinc monohidratado 34,5% Zn',1.39,'La publicacion debe coincidir con 34,5% Zn.'),
        @('B',0.39,'Acido borico 17,5% B',8.91,'Usar pureza declarada cercana a 99,9%.'),
        @('Cu',0.03,'Sulfato de cobre pentahidratado ~25% Cu',0.48,'Pesar con balanza de 0,001 g.'),
        @('Mo',0.02,'Molibdato de sodio dihidratado 39,6% Mo',0.20,'Confirmar Na2MoO4.2H2O y 39,6% Mo.')
    )
    for ($i = 0; $i -lt $composition.Count; $i++) {
        $row = 8 + $i
        Set-CellText $additives $ns2 "A$row" $composition[$i][0]
        Set-CellNumber $additives $ns2 "B$row" $composition[$i][1]
        Set-CellText $additives $ns2 "C$row" $composition[$i][2]
        Set-CellNumber $additives $ns2 "D$row" $composition[$i][3]
        Set-CellText $additives $ns2 "E$row" $composition[$i][4]
    }
    Set-CellText $additives $ns2 'A14' 'Resultado'
    Set-CellText $additives $ns2 'B14' '20 ml/40 L entrega Fe 2,10; Mn 0,60; Zn 0,12; B 0,39; Cu 0,03; Mo 0,02 ppm.'

    foreach ($row in 16..25) { Clear-Row $additives $ns2 $row }
    $auditHeaders = @('Semana','Fase','Micro C estandar (ml/40 L)','Micro C real (ml/tanque)','Fe ppm','Mn ppm','Zn ppm','B ppm','Cu ppm','Mo ppm','Control','Nota')
    for ($i = 0; $i -lt $auditHeaders.Count; $i++) {
        $col = [char]([int][char]'A' + $i)
        Set-CellText $additives $ns2 "$col`16" $auditHeaders[$i] '9'
    }
    foreach ($row in 17..25) {
        $sourceRow = $row - 5
        Set-CellFormula $additives $ns2 "A$row" "'Sheet1'!A$sourceRow"
        Set-CellFormula $additives $ns2 "B$row" "'Sheet1'!B$sourceRow"
        Set-CellFormula $additives $ns2 "C$row" '`$B`$4'
        Set-CellFormula $additives $ns2 "D$row" "'Sheet1'!J$sourceRow"
        foreach ($mapping in @(@('E',8),@('F',9),@('G',10),@('H',11),@('I',12),@('J',13))) {
            Set-CellFormula $additives $ns2 "$($mapping[0])$row" "`$B`$$($mapping[1])*D$row/(`$B`$4*'Sheet1'!`$B`$3/40)"
        }
        Set-CellFormula $additives $ns2 "K$row" "IF(AND(E$row>=1,E$row<=2.3,F$row>=0.4,F$row<=0.8,G$row>=0.1,G$row<=0.4,H$row>=0.2,H$row<=0.45,I$row>=0.02,I$row<=0.1,J$row>=0.01,J$row<=0.08),`"En rango`",`"Revisar`")"
        Set-CellText $additives $ns2 "L$row" 'La dosis acompana el tanque; no usar Micro C para perseguir EC.'
    }

    Set-CellText $additives $ns2 'B41' 'Agitar Micro C y medir la dosis indicada en Sheet1. Agregarla al tanque ya diluido, con circulacion; nunca mezclar concentrados entre si.'

    foreach ($row in 60..94) { Clear-Row $additives $ns2 $row }
    Set-CellText $additives $ns2 'A60' 'LISTA DE COMPRA - MICRO C' '1'
    $purchaseHeaders = @('Insumo','Especificacion obligatoria','Cantidad minima practica','Uso en stock','Referencia / enlace')
    for ($i = 0; $i -lt $purchaseHeaders.Count; $i++) {
        $col = [char]([int][char]'A' + $i)
        Set-CellText $additives $ns2 "$col`61" $purchaseHeaders[$i] '9'
    }
    $purchases = @(
        @('Hierro quelatado','Fe-EDTA solido 13% Fe','100 g','64,60 g','https://www.afital.com.ar/wp-content/uploads/2022/08/AFITAL-FERRO-EDTA-ficha-tecnica.docx.pdf'),
        @('Sulfato de manganeso','32% Mn','100-250 g','7,50 g','https://www.mercadolibre.com.ar/fertilizante-sulfato-de-manganeso-x-250-gr/up/MLAU3574467908'),
        @('Sulfato de zinc','Monohidratado, 34,5% Zn','100 g','1,39 g','https://www.mercadolibre.com.ar/sulfato-de-zinc-x-1-kg-heptahidratado/p/MLA2091360747'),
        @('Sulfato de cobre','Pentahidratado, aprox. 25% Cu','100-250 g','0,48 g','https://www.mercadolibre.com.ar/sulfato-de-cobre-x250gr-pentahidratado/up/MLAU3398410194'),
        @('Acido borico','17,5% B; pureza 99,9%','100 g','8,91 g','https://www.mercadolibre.com.ar/acido-borico-pureza-999-x-1kg/up/MLAU477272139'),
        @('Molibdato de sodio','Na2MoO4.2H2O; 39,6% Mo','100 g','0,20 g','https://www.mercadolibre.com.ar/fertilizante-molibdato-de-sodio-fertirriego-x-100-gr/up/MLAU4420796576'),
        @('Balanza de precision','Resolucion real 0,001 g','1 unidad','Pesaje de Cu y Mo','Buscar balanza de laboratorio 0,001 g'),
        @('Botella opaca HDPE','2 L, cierre hermetico','1 unidad','Conservar solucion madre','Rotular fecha, formula y dosis'),
        @('Agua destilada u osmosis','Baja EC','2 L','Vehiculo del stock','No preparar el stock con agua dura'),
        @('Elementos de seguridad','Guantes nitrilo, antiparras, embudo y jeringa 20 ml','1 juego','Preparacion y dosificacion','No inhalar polvos ni usar utensilios de cocina')
    )
    for ($i = 0; $i -lt $purchases.Count; $i++) {
        $row = 62 + $i
        for ($j = 0; $j -lt 5; $j++) {
            $col = [char]([int][char]'A' + $j)
            Set-CellText $additives $ns2 "$col$row" $purchases[$i][$j]
        }
    }

    Set-CellText $additives $ns2 'A73' 'PREPARACION DE 2 L DE SOLUCION MADRE MICRO C' '1'
    Set-CellText $additives $ns2 'A74' 'Paso' '9'
    Set-CellText $additives $ns2 'B74' 'Procedimiento' '9'
    $stockSteps = @(
        'Usar guantes, antiparras, recipientes limpios y una balanza de 0,001 g.',
        'Cargar aproximadamente 1,5 L de agua destilada u osmosis en el recipiente.',
        'Disolver por separado y agregar de a uno: acido borico, Mn, Zn, Cu y Mo.',
        'Agregar el Fe-EDTA al final y agitar hasta disolucion uniforme.',
        'Completar con agua destilada hasta un volumen final exacto de 2,00 L.',
        'Envasar opaco, rotular composicion, fecha y dosis 20 ml/40 L; guardar fresco y oscuro.',
        'Agitar antes de cada uso. Descartar si aparecen precipitados persistentes, olor o contaminacion.',
        'No agregar al stock: Calcinit, KNO3, Epsom, sulfato de potasio, 20-20-20 ni Bloom/MKP.'
    )
    for ($i = 0; $i -lt $stockSteps.Count; $i++) {
        $row = 75 + $i
        Set-CellNumber $additives $ns2 "A$row" ($i + 1)
        Set-CellText $additives $ns2 "B$row" $stockSteps[$i]
    }

    Set-CellText $additives $ns2 'A84' 'ORDEN DE PREPARADO DE LA SOPA - CADA TANQUE' '1'
    Set-CellText $additives $ns2 'A85' 'Paso' '9'
    Set-CellText $additives $ns2 'B85' 'Orden' '9'
    $tankSteps = @(
        'Medir y registrar EC y pH del agua base; cargar 70-80% del volumen y mantener circulacion.',
        'Si se usa silicato, agregarlo primero, diluido, y mezclar completamente.',
        'Disolver por separado el 20-20-20 y agregarlo al tanque.',
        'Agitar Micro C, medir los ml indicados en Sheet1 y agregar con circulacion.',
        'Prediluir Calcinit y agregar lentamente. Nunca tocar sulfatos o fosfatos concentrados.',
        'Disolver y agregar por separado KNO3, Epsom y sulfato de potasio, en ese orden.',
        'Disolver y agregar Bloom/MKP al final de las sales.',
        'Completar el volumen, recircular 10-15 min y medir EC. Ajustar sales solo segun plan y respuesta.',
        'Ajustar pH al final; registrar EC/pH de entrada, volumen aplicado y runoff.'
    )
    for ($i = 0; $i -lt $tankSteps.Count; $i++) {
        $row = 86 + $i
        Set-CellNumber $additives $ns2 "A$row" ($i + 1)
        Set-CellText $additives $ns2 "B$row" $tankSteps[$i]
    }

    # Rebuild Aditivos as a compact operational sheet. Sheet1 depends on
    # B4, H3, H4, K3 and K4, so those cells must keep their exact meaning.
    $sheetData = $additives.SelectSingleNode('//s:sheetData', $ns2)
    foreach ($rowNode in @($sheetData.SelectNodes('s:row', $ns2))) {
        [void]$sheetData.RemoveChild($rowNode)
    }

    Set-CellText $additives $ns2 'A1' 'ADITIVOS - USO PRACTICO' '1'

    Set-CellText $additives $ns2 'A3' 'DOSIS DE MICRO C' '1'
    Set-CellText $additives $ns2 'A4' 'Dosis base'
    Set-CellNumber $additives $ns2 'B4' 20
    Set-CellText $additives $ns2 'C4' 'ml por cada 40 L'
    Set-CellText $additives $ns2 'A5' 'Litros actuales del tanque'
    Set-CellFormula $additives $ns2 'B5' "'Sheet1'!`$B`$3"
    Set-CellText $additives $ns2 'C5' 'L'
    Set-CellText $additives $ns2 'A6' 'Dosis para el tanque actual'
    Set-CellFormula $additives $ns2 'B6' "'Sheet1'!J12"
    Set-CellText $additives $ns2 'C6' 'ml de Micro C'
    Set-CellText $additives $ns2 'A7' 'Uso'
    Set-CellText $additives $ns2 'B7' 'Agregar la dosis indicada en Sheet1. No usar Micro C para subir la EC.'

    Set-CellText $additives $ns2 'G2' 'PARAMETROS QUE USA SHEET1' '1'
    Set-CellText $additives $ns2 'G3' 'Sulfato de potasio: K (%)'
    Set-CellNumber $additives $ns2 'H3' 44.4
    Set-CellText $additives $ns2 'G4' 'Sulfato de potasio: S (%)'
    Set-CellNumber $additives $ns2 'H4' 18.2
    Set-CellText $additives $ns2 'J3' 'Nitrato de potasio: N (%)'
    Set-CellNumber $additives $ns2 'K3' 13.7
    Set-CellText $additives $ns2 'J4' 'Nitrato de potasio: K2O (%)'
    Set-CellNumber $additives $ns2 'K4' 46.1
    Set-CellText $additives $ns2 'G6' 'Materia prima de silicio'
    Set-CellText $additives $ns2 'H6' 'Silicato de potasio Kraff (tipo K-30)'
    Set-CellText $additives $ns2 'G7' 'Objetivo semanas 1-8'
    Set-CellNumber $additives $ns2 'H7' 7
    Set-CellText $additives $ns2 'I7' 'ppm Si'
    Set-CellText $additives $ns2 'J6' 'SiO2 nominal (%)'
    Set-CellNumber $additives $ns2 'K6' 19.5
    Set-CellText $additives $ns2 'J7' 'K2O nominal (%)'
    Set-CellNumber $additives $ns2 'K7' 8.2

    Set-CellText $additives $ns2 'A10' 'RECETA DE SOLUCION MADRE MICRO C - VOLUMEN FINAL 2 L' '1'
    Set-CellText $additives $ns2 'A11' 'Producto' '9'
    Set-CellText $additives $ns2 'B11' 'Cantidad' '9'
    Set-CellText $additives $ns2 'C11' 'Especificacion necesaria' '9'
    $simpleRecipe = @(
        @('Hierro quelatado liquido',210.00,'Afital Hierro EDTA liquido, 4% Fe p/p; pesar el producto'),
        @('Sulfato de manganeso',7.74,'Monohidratado, 31% Mn; Norte Insumos'),
        @('Sulfato de zinc',2.11,'Heptahidratado P.A. Salttech, ZnSO4.7H2O'),
        @('Acido borico',8.91,'17,5% B'),
        @('Sulfato de cobre',0.24,'Pentahidratado, aproximadamente 25% Cu; la base completa el resto'),
        @('Molibdato de sodio',0.20,'Dihidratado, 39,6% Mo')
    )
    for ($i = 0; $i -lt $simpleRecipe.Count; $i++) {
        $row = 12 + $i
        Set-CellText $additives $ns2 "A$row" $simpleRecipe[$i][0]
        Set-CellNumber $additives $ns2 "B$row" $simpleRecipe[$i][1]
        Set-CellText $additives $ns2 "C$row" 'g'
        Set-CellText $additives $ns2 "D$row" $simpleRecipe[$i][2]
    }
    Set-CellText $additives $ns2 'A18' 'Agua destilada u osmosis'
    Set-CellText $additives $ns2 'B18' 'Completar hasta 2,00 L finales'
    Set-CellText $additives $ns2 'A19' 'Resultado por dosis de 20 ml/40 L'
    Set-CellText $additives $ns2 'B19' 'Micro C: Fe 2,10 | Mn 0,60 | Zn 0,12 | B 0,39 | Cu 0,015 | Mo 0,02 ppm. La base lleva el Cu total a aprox. 0,03 ppm.'

    Set-CellText $additives $ns2 'A22' 'COMO PREPARAR MICRO C UNA SOLA VEZ' '1'
    $microSteps = @(
        'Colocar aproximadamente 1,5 L de agua destilada u osmosis.',
        'Disolver cada producto por separado y agregar: acido borico, Mn, Zn, Cu y Mo.',
        'Agregar el Fe-EDTA al final y mezclar hasta que quede uniforme.',
        'Completar con agua hasta un volumen final exacto de 2,00 L.',
        'Guardar en botella opaca, rotulada y en lugar fresco. Agitar antes de usar.'
    )
    for ($i = 0; $i -lt $microSteps.Count; $i++) {
        $row = 23 + $i
        Set-CellNumber $additives $ns2 "A$row" ($i + 1)
        Set-CellText $additives $ns2 "B$row" $microSteps[$i]
    }

    Set-CellText $additives $ns2 'A30' 'ORDEN DE PREPARACION DE CADA TANQUE' '1'
    $mixSteps = @(
        'Cargar 70-80% del agua y encender la circulacion.',
        'Pesar el silicato Kraff indicado, prediluirlo al menos 1:100 y agregarlo primero; comprobar que no aparezca turbidez.',
        'Disolver por separado y agregar el 20-20-20.',
        'Agitar Micro C y agregar los ml indicados en Sheet1.',
        'Prediluir Calcinit y agregar lentamente.',
        'Disolver y agregar por separado nitrato de potasio.',
        'Disolver y agregar por separado Epsom.',
        'Disolver y agregar por separado sulfato de potasio.',
        'Disolver y agregar Bloom/MKP.',
        'Completar el volumen y recircular 10-15 minutos.',
        'Medir EC y ajustar solamente con el plan; ajustar pH al final.'
    )
    for ($i = 0; $i -lt $mixSteps.Count; $i++) {
        $row = 31 + $i
        Set-CellNumber $additives $ns2 "A$row" ($i + 1)
        Set-CellText $additives $ns2 "B$row" $mixSteps[$i]
    }

    Set-CellText $additives $ns2 'A43' 'REGLAS IMPORTANTES' '1'
    Set-CellText $additives $ns2 'A44' '1'
    Set-CellText $additives $ns2 'B44' 'Nunca mezclar sales concentradas entre si; cada una se disuelve por separado.'
    Set-CellText $additives $ns2 'A45' '2'
    Set-CellText $additives $ns2 'B45' 'Micro C no contiene Calcinit, KNO3, Epsom, sulfato de potasio, 20-20-20 ni Bloom.'
    Set-CellText $additives $ns2 'A46' '3'
    Set-CellText $additives $ns2 'B46' 'Si la EC final no coincide, no compensar agregando Micro C.'
    Set-CellText $additives $ns2 'A47' '4'
    Set-CellText $additives $ns2 'B47' 'Registrar EC y pH de entrada y de runoff antes de corregir la receta.'
    Set-CellText $additives $ns2 'A48' '5'
    Set-CellText $additives $ns2 'B48' 'El silicato Kraff no entra en Micro C. Agregar primero y suspender si deja turbidez, gel o sedimento; no acidificar un concentrado.'

    Set-CellText $additives $ns2 'A50' 'GUIA DE ELEMENTOS DE TU NUTRICION' '1'
    Set-CellText $additives $ns2 'A51' 'Tipo' '9'
    Set-CellText $additives $ns2 'B51' 'Simbolo' '9'
    Set-CellText $additives $ns2 'C51' 'Nombre' '9'
    Set-CellText $additives $ns2 'D51' 'Funcion principal' '9'
    Set-CellText $additives $ns2 'E51' 'De donde viene en tu plan' '9'
    $elementGuide = @(
        @('Macro primario','N','Nitrogeno','Crecimiento, proteinas y clorofila','20-20-20, Calcinit y nitrato de potasio'),
        @('Macro primario','P','Fosforo','Energia, raices y desarrollo floral','20-20-20 y Bloom/MKP'),
        @('Macro primario','K','Potasio','Regulacion del agua, enzimas y floracion','20-20-20, nitrato y sulfato de potasio, Bloom'),
        @('Macro secundario','Ca','Calcio','Paredes celulares, brotes y raices nuevas','Calcinit'),
        @('Macro secundario','Mg','Magnesio','Atomo central de la clorofila','Epsom'),
        @('Macro secundario','S','Azufre','Aminoacidos, proteinas y enzimas','Epsom y sulfato de potasio'),
        @('Micronutriente','Fe','Hierro','Formacion de clorofila y transporte de electrones','20-20-20 y Micro C'),
        @('Micronutriente','Mn','Manganeso','Fotosintesis y activacion enzimatica','20-20-20 y Micro C'),
        @('Micronutriente','Zn','Zinc','Crecimiento y regulacion hormonal','20-20-20 y Micro C'),
        @('Micronutriente','B','Boro','Tejidos nuevos, paredes celulares y floracion','20-20-20 y Micro C'),
        @('Micronutriente','Cu','Cobre','Enzimas y formacion de tejidos','20-20-20 y Micro C'),
        @('Micronutriente','Mo','Molibdeno','Permite utilizar correctamente el nitrogeno nitrico','20-20-20 y Micro C'),
        @('Elemento benefico','Si','Silicio','Refuerzo estructural y tolerancia a estres','Silicato de potasio Kraff')
    )
    for ($i = 0; $i -lt $elementGuide.Count; $i++) {
        $row = 52 + $i
        for ($j = 0; $j -lt 5; $j++) {
            $column = [char]([int][char]'A' + $j)
            Set-CellText $additives $ns2 "$column$row" $elementGuide[$i][$j]
        }
    }
    Set-CellText $additives $ns2 'A65' 'Como leerlo'
    Set-CellText $additives $ns2 'B65' 'N-P-K son los tres numeros del fertilizante. Por ejemplo, 20-20-20 declara nitrogeno, fosfato y potasa en ese orden.'
    Set-CellText $additives $ns2 'A66' 'Importante'
    Set-CellText $additives $ns2 'B66' 'La EC indica la concentracion ionica total; no identifica cuanto hay de cada elemento.'

    Set-CellText $additives $ns2 'A69' 'LINKS DE COMPRA' '1'
    Set-CellText $additives $ns2 'A70' 'Grupo' '9'
    Set-CellText $additives $ns2 'B70' 'Producto' '9'
    Set-CellText $additives $ns2 'C70' 'Que debe decir la etiqueta' '9'
    Set-CellText $additives $ns2 'D70' 'Estado' '9'
    Set-CellText $additives $ns2 'E70' 'Enlace' '9'
    $purchaseLinks = @(
        @('Tanque','Base 20-20-20','Fertilizante soluble 20-20-20','Ya lo tienes','https://www.sanisidro422.com.ar/shop/products/1709_DSC_sales-minerales-base-20-20-20-125gr-gps-horticultura'),
        @('Tanque','Bloom / MKP','Potenciador de flora compatible con la receta','Ya lo tienes','https://www.sanisidro422.com.ar/shop/products/1710_DSC_big-bloom-poteciador-de-flora125-gr-gps-horticultura'),
        @('Tanque','Calcinit','Nitrato de calcio totalmente soluble','Ya lo tienes','https://www.mercadolibre.com.ar/fert-nitrato-de-calcio-soluble-25k-fertirriego-calcinit-bio/up/MLAU196171698'),
        @('Tanque','Epsom','Sulfato de magnesio','Ya lo tienes','https://www.mercadolibre.com.ar/sales-de-epson-sulfato-de-magnesio-x-1kg-icasa/p/MLA54435900'),
        @('Tanque','Nitrato de potasio','KNO3 soluble; confirmar composicion de tu envase','Ya lo tienes','https://www.mercadolibre.com.ar/nitrato-de-potasio--99--maxima-pureza--1kg-farmashop/up/MLAU3374009278'),
        @('Tanque','Sulfato de potasio Salttech','K2SO4 anhidro P.A. 99%; aprox. 44,4% K y 18,2% S','Comprar 50 g en Norte Insumos','https://www.norteinsumoslab.com/productos/potasio-sulfato-anhidro-pro-analisis-a-c-s-salttech/'),
        @('Materia prima','Silicato de potasio Kraff 5 L','19,5% SiO2 y 8,2% K2O segun la etiqueta; soluble y sin particulas','Composicion verificada en la etiqueta','https://www.mercadolibre.com.ar/silicato-de-potasio-kraff-5-litros-uso-industrial/up/MLAU4345782094'),
        @('Micro C','Afital Hierro EDTA liquido','4% Fe p/p; EDTA y lignosulfonatos','Comprar 1 L','https://articulo.mercadolibre.com.ar/MLA-1842015564-hierro-liquido-quelato-edta-fe-_JM'),
        @('Micro C','Sulfato de manganeso','Monohidratado, 31% Mn','Comprar 100 g en Norte Insumos','https://www.norteinsumoslab.com/productos/manganeso-sulfato-industrial/'),
        @('Micro C','Sulfato de zinc Salttech','ZnSO4.7H2O heptahidratado P.A.','Comprar 100 g en Norte Insumos','https://www.norteinsumoslab.com/productos/zinc-sulfato-7-hidrato-pro-analisis-a-c-s-salttech/'),
        @('Micro C','Acido borico','Pureza 99,9%; H3BO3','Comprar 500 g en Norte Insumos','https://www.norteinsumoslab.com/productos/acido-borico-999/'),
        @('Micro C','Sulfato de cobre grado tecnico','Pentahidratado, CuSO4.5H2O; usar solo si el envase confirma esta formula','Comprar 250 g en Norte Insumos','https://www.norteinsumoslab.com/productos/sulfato-de-cobre-5-hidrato-industrial/'),
        @('Micro C','Molibdato de sodio','Na2MoO4.2H2O P.A.; aproximadamente 39,6% Mo','Comprar 10 g en Norte Insumos','https://www.norteinsumoslab.com/productos/sodio-molibdato-2-hidrato-pro-analisis/'),
        @('Preparacion','Agua destilada','Baja EC, sin minerales agregados','Comprar 2 L','https://listado.mercadolibre.com.ar/agua-destilada')
    )
    for ($i = 0; $i -lt $purchaseLinks.Count; $i++) {
        $row = 71 + $i
        Set-CellText $additives $ns2 "A$row" $purchaseLinks[$i][0]
        Set-CellText $additives $ns2 "B$row" $purchaseLinks[$i][1]
        Set-CellText $additives $ns2 "C$row" $purchaseLinks[$i][2]
        Set-CellText $additives $ns2 "D$row" $purchaseLinks[$i][3]
        $url = $purchaseLinks[$i][4]
        Set-CellFormula $additives $ns2 "E$row" "HYPERLINK(`"$url`",`"Abrir compra`")"
    }
    Set-CellText $additives $ns2 'A85' 'Antes de comprar'
    Set-CellText $additives $ns2 'B85' 'Silicato Kraff verificado por etiqueta: 19,5% SiO2 y 8,2% K2O. Si el envase recibido cambia estos porcentajes, actualizar K6 y K7 antes de preparar el tanque.'

    $dimension = $additives.SelectSingleNode('//s:dimension', $ns2)
    if ($dimension) { $dimension.SetAttribute('ref', 'A1:K85') }

    $wbNs = Get-NamespaceManager $workbook
    $calcPr = $workbook.SelectSingleNode('//s:calcPr', $wbNs)
    if (-not $calcPr) {
        $calcPr = $workbook.CreateElement('calcPr', $script:SpreadsheetNs)
        [void]$workbook.DocumentElement.AppendChild($calcPr)
    }
    $calcPr.SetAttribute('calcMode', 'auto')
    $calcPr.SetAttribute('fullCalcOnLoad', '1')
    $calcPr.SetAttribute('forceFullCalc', '1')

    Write-ZipXml $zip 'xl\worksheets\sheet1.xml' $sheet1
    Write-ZipXml $zip 'xl\worksheets\sheet2.xml' $additives
    Write-ZipXml $zip 'xl\workbook.xml' $workbook
}
finally {
    $zip.Dispose()
}

Get-Item -LiteralPath $OutputPath | Select-Object FullName, Length, LastWriteTime
