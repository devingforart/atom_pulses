param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Escape-Xml {
    param([string]$Value)
    return [System.Security.SecurityElement]::Escape($Value)
}

function Text-Cell {
    param([string]$Reference, [string]$Value, [int]$Style = 0)
    $escaped = Escape-Xml $Value
    return "<c r=`"$Reference`" t=`"inlineStr`" s=`"$Style`"><is><t xml:space=`"preserve`">$escaped</t></is></c>"
}

function Number-Cell {
    param([string]$Reference, [double]$Value, [int]$Style = 5)
    $number = $Value.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    return "<c r=`"$Reference`" s=`"$Style`"><v>$number</v></c>"
}

function Formula-Cell {
    param([string]$Reference, [string]$Formula, [int]$Style = 5)
    $escaped = Escape-Xml $Formula
    return "<c r=`"$Reference`" s=`"$Style`"><f>$escaped</f><v></v></c>"
}

function Row-Xml {
    param([int]$Number, [string[]]$Cells, [double]$Height = 0)
    $heightAttributes = if ($Height -gt 0) { " ht=`"$Height`" customHeight=`"1`"" } else { '' }
    return "<row r=`"$Number`"$heightAttributes>$($Cells -join '')</row>"
}

function Add-ZipText {
    param($Archive, [string]$Name, [string]$Content)
    $entry = $Archive.CreateEntry($Name, [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = [System.IO.StreamWriter]::new($entry.Open(), [System.Text.UTF8Encoding]::new($false))
    try { $writer.Write($Content) } finally { $writer.Dispose() }
}

$rows = [System.Collections.Generic.List[string]]::new()
$rows.Add((Row-Xml 1 @((Text-Cell 'A1' 'RECETA FINAL MICRO C' 1)) 28))
$rows.Add((Row-Xml 2 @((Text-Cell 'A2' 'Solucion madre: completar hasta 2,00 L finales | Dosis base: 20 ml por 40 L' 2)) 24))
$rows.Add((Row-Xml 4 @(
    (Text-Cell 'A4' 'Ingrediente' 2),
    (Text-Cell 'B4' 'Cantidad' 2),
    (Text-Cell 'C4' 'Unidad' 2),
    (Text-Cell 'D4' 'Especificacion obligatoria' 2),
    (Text-Cell 'E4' 'Control practico' 2)
) 26))

$ingredients = @(
    @('Afital Hierro EDTA liquido',210.00,'g','Fe 4% p/p; EDTA/lignosulfonatos','Pesar el producto; no medir 210 ml.'),
    @('Sulfato de manganeso',7.74,'g','MnSO4.H2O monohidratado; 31% Mn','Recalcular si la etiqueta indica otro porcentaje.'),
    @('Sulfato de zinc',2.11,'g','ZnSO4.7H2O heptahidratado','No sustituir por monohidratado gramo por gramo.'),
    @('Acido borico',8.91,'g','H3BO3; 17,5% B','Pureza declarada cercana a 99,9%.'),
    @('Sulfato de cobre',0.48,'g','CuSO4.5H2O pentahidratado; aprox. 25% Cu','Pesar con balanza de 0,001 g.'),
    @('Molibdato de sodio',0.20,'g','Na2MoO4.2H2O dihidratado; 39,6% Mo','Pesar con balanza de 0,001 g.'),
    @('Agua destilada, bidestilada u osmosis',0,'','Completar hasta 2,00 L de volumen final','No agregar 2 L encima de los ingredientes.')
)

for ($i = 0; $i -lt $ingredients.Count; $i++) {
    $row = 5 + $i
    $item = $ingredients[$i]
    $quantityCell = if ($i -eq 6) { Text-Cell "B$row" 'c.s.p. 2,00' 4 } else { Number-Cell "B$row" ([double]$item[1]) 5 }
    $rows.Add((Row-Xml $row @(
        (Text-Cell "A$row" $item[0] 4),
        $quantityCell,
        (Text-Cell "C$row" $item[2] 4),
        (Text-Cell "D$row" $item[3] 4),
        (Text-Cell "E$row" $item[4] 4)
    ) 34))
}

$rows.Add((Row-Xml 14 @((Text-Cell 'A14' 'ORDEN DE PREPARACION' 3)) 24))
$steps = @(
    'Usar guantes, antiparras, recipientes limpios y balanza calibrada de 0,001 g.',
    'Colocar aproximadamente 1,5 L de agua destilada, bidestilada o de osmosis.',
    'Disolver cada sal por separado. Agregar: acido borico, manganeso, zinc, cobre y molibdato.',
    'Mezclar completamente entre cada agregado. No juntar polvos ni concentrados.',
    'Agregar los 210,00 g de Afital Fe-EDTA al final y homogeneizar.',
    'Completar con agua hasta un volumen final exacto de 2,00 L.',
    'Envasar en botella opaca y rotular formula, fecha y dosis. Agitar antes de usar.'
)
for ($i = 0; $i -lt $steps.Count; $i++) {
    $row = 15 + $i
    $rows.Add((Row-Xml $row @(
        (Number-Cell "A$row" ($i + 1) 6),
        (Text-Cell "B$row" $steps[$i] 4)
    ) 34))
}

$rows.Add((Row-Xml 24 @((Text-Cell 'A24' 'APORTE ELEMENTAL CON 20 ml EN 40 L (ppm = mg/L)' 3)) 24))
$rows.Add((Row-Xml 25 @(
    (Text-Cell 'A25' 'Fe' 2),(Text-Cell 'B25' 'Mn' 2),(Text-Cell 'C25' 'Zn' 2),
    (Text-Cell 'D25' 'B' 2),(Text-Cell 'E25' 'Cu' 2),(Text-Cell 'F25' 'Mo' 2)
) 22))
$rows.Add((Row-Xml 26 @(
    (Number-Cell 'A26' 2.10 5),(Number-Cell 'B26' 0.600 5),(Number-Cell 'C26' 0.120 5),
    (Number-Cell 'D26' 0.390 5),(Number-Cell 'E26' 0.0305 5),(Number-Cell 'F26' 0.0198 5),
    (Text-Cell 'G26' 'ppm = mg/L' 4)
) 24))
$rows.Add((Row-Xml 27 @(
    (Number-Cell 'A27' 84.0 5),(Number-Cell 'B27' 24.0 5),(Number-Cell 'C27' 4.8 5),
    (Number-Cell 'D27' 15.6 5),(Number-Cell 'E27' 1.22 5),(Number-Cell 'F27' 0.792 5),
    (Text-Cell 'G27' 'mg totales en 40 L' 4)
) 24))

$rows.Add((Row-Xml 29 @((Text-Cell 'A29' 'CALCULADORA DE DOSIS' 3)) 24))
$rows.Add((Row-Xml 30 @(
    (Text-Cell 'A30' 'Litros reales del tanque' 4),
    (Number-Cell 'B30' 40 7),
    (Text-Cell 'C30' 'L (editable)' 4)
) 24))
$rows.Add((Row-Xml 31 @(
    (Text-Cell 'A31' 'Micro C para ese tanque' 4),
    (Formula-Cell 'B31' 'B30*20/40' 5),
    (Text-Cell 'C31' 'ml' 4)
) 24))

$rows.Add((Row-Xml 34 @((Text-Cell 'A34' 'CONTROLES IMPORTANTES' 3)) 24))
$controls = @(
    'Micro C no contiene Calcinit, KNO3, Epsom, sulfato de potasio, MKP ni silicato.',
    'Agregar Micro C al tanque con agua y circulacion. Nunca mezclarlo directamente con otros concentrados.',
    'El silicato fue eliminado del cultivo: no agregarlo a esta botella ni al tanque.',
    'Si una etiqueta no coincide con la forma hidratada o porcentaje indicado, hay que recalcular.',
    'No usar Micro C para subir la EC. Ajustar la EC con la receta principal y medir el tanque.',
    'No dosificar si aparecen cristales persistentes, turbidez, gel, olor o contaminacion.'
)
for ($i = 0; $i -lt $controls.Count; $i++) {
    $row = 35 + $i
    $rows.Add((Row-Xml $row @(
        (Number-Cell "A$row" ($i + 1) 6),
        (Text-Cell "B$row" $controls[$i] 4)
    ) 32))
}

$sheetXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <dimension ref="A1:G40"/>
  <sheetViews><sheetView workbookViewId="0"><pane ySplit="4" topLeftCell="A5" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews>
  <sheetFormatPr defaultRowHeight="18"/>
  <cols>
    <col min="1" max="1" width="32" customWidth="1"/>
    <col min="2" max="2" width="19" customWidth="1"/>
    <col min="3" max="3" width="16" customWidth="1"/>
    <col min="4" max="4" width="47" customWidth="1"/>
    <col min="5" max="5" width="47" customWidth="1"/>
    <col min="6" max="7" width="13" customWidth="1"/>
  </cols>
  <sheetData>$($rows -join '')</sheetData>
  <mergeCells count="13">
    <mergeCell ref="A1:G1"/><mergeCell ref="A2:G2"/><mergeCell ref="A14:G14"/>
    <mergeCell ref="B15:G15"/><mergeCell ref="B16:G16"/><mergeCell ref="B17:G17"/>
    <mergeCell ref="B18:G18"/><mergeCell ref="B19:G19"/><mergeCell ref="B20:G20"/>
    <mergeCell ref="B21:G21"/><mergeCell ref="A24:G24"/><mergeCell ref="A29:G29"/><mergeCell ref="A34:G34"/>
  </mergeCells>
</worksheet>
"@

$stylesXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <numFmts count="1"><numFmt numFmtId="164" formatCode="0.000"/></numFmts>
  <fonts count="3">
    <font><sz val="11"/><name val="Calibri"/><family val="2"/></font>
    <font><b/><color rgb="FFFFFFFF"/><sz val="16"/><name val="Calibri"/></font>
    <font><b/><color rgb="FFFFFFFF"/><sz val="11"/><name val="Calibri"/></font>
  </fonts>
  <fills count="5">
    <fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill>
    <fill><patternFill patternType="solid"><fgColor rgb="FF1F5D42"/><bgColor indexed="64"/></patternFill></fill>
    <fill><patternFill patternType="solid"><fgColor rgb="FF34775A"/><bgColor indexed="64"/></patternFill></fill>
    <fill><patternFill patternType="solid"><fgColor rgb="FFFFF2CC"/><bgColor indexed="64"/></patternFill></fill>
  </fills>
  <borders count="2"><border/><border><left style="thin"><color rgb="FFD9E2DC"/></left><right style="thin"><color rgb="FFD9E2DC"/></right><top style="thin"><color rgb="FFD9E2DC"/></top><bottom style="thin"><color rgb="FFD9E2DC"/></bottom></border></borders>
  <cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
  <cellXfs count="8">
    <xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
    <xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0"><alignment horizontal="center" vertical="center"/></xf>
    <xf numFmtId="0" fontId="2" fillId="3" borderId="1" xfId="0"><alignment horizontal="center" vertical="center" wrapText="1"/></xf>
    <xf numFmtId="0" fontId="2" fillId="2" borderId="0" xfId="0"><alignment horizontal="left" vertical="center"/></xf>
    <xf numFmtId="0" fontId="0" fillId="0" borderId="1" xfId="0"><alignment vertical="center" wrapText="1"/></xf>
    <xf numFmtId="164" fontId="0" fillId="0" borderId="1" xfId="0"><alignment horizontal="center" vertical="center"/></xf>
    <xf numFmtId="0" fontId="0" fillId="0" borderId="1" xfId="0"><alignment horizontal="center" vertical="center"/></xf>
    <xf numFmtId="0" fontId="0" fillId="4" borderId="1" xfId="0"><alignment horizontal="center" vertical="center"/></xf>
  </cellXfs>
  <cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
</styleSheet>
'@

$contentTypes = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
  <Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
  <Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
  <Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
  <Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
</Types>
'@

$rootRels = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>
'@

$workbookXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
  <bookViews><workbookView xWindow="0" yWindow="0" windowWidth="24000" windowHeight="14000"/></bookViews>
  <sheets><sheet name="Receta Micro C" sheetId="1" r:id="rId1"/></sheets>
  <calcPr calcId="191029" calcMode="auto" fullCalcOnLoad="1" forceFullCalc="1"/>
</workbook>
'@

$workbookRels = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>
'@

$coreXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"><dc:title>Receta final Micro C</dc:title><dc:creator>Codex</dc:creator><dcterms:created xsi:type="dcterms:W3CDTF">2026-10-02T00:00:00Z</dcterms:created></cp:coreProperties>
'@

$appXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes"><Application>Microsoft Excel Compatible</Application><AppVersion>16.0000</AppVersion></Properties>
'@

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    [void](New-Item -ItemType Directory -Path $outputDirectory -Force)
}
if (Test-Path -LiteralPath $OutputPath) { Remove-Item -LiteralPath $OutputPath -Force }

$archive = [System.IO.Compression.ZipFile]::Open($OutputPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    Add-ZipText $archive '[Content_Types].xml' $contentTypes
    Add-ZipText $archive '_rels/.rels' $rootRels
    Add-ZipText $archive 'docProps/core.xml' $coreXml
    Add-ZipText $archive 'docProps/app.xml' $appXml
    Add-ZipText $archive 'xl/workbook.xml' $workbookXml
    Add-ZipText $archive 'xl/_rels/workbook.xml.rels' $workbookRels
    Add-ZipText $archive 'xl/styles.xml' $stylesXml
    Add-ZipText $archive 'xl/worksheets/sheet1.xml' $sheetXml
}
finally {
    $archive.Dispose()
}

Get-Item -LiteralPath $OutputPath | Select-Object FullName, Length, LastWriteTime
