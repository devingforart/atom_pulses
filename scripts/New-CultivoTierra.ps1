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
$mainNs = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'

function Ns($xml) {
    $n = [Xml.XmlNamespaceManager]::new($xml.NameTable)
    $n.AddNamespace('s', $mainNs)
    return $n
}
function Cell($xml,$ns,[string]$ref) {
    $row = [int]([regex]::Match($ref,'\d+').Value)
    $node = $xml.SelectSingleNode("//*[local-name()='row'][@r='$row']")
    if (-not $node) { $node=$xml.CreateElement('row',$mainNs);$node.SetAttribute('r',[string]$row);[void]$xml.SelectSingleNode("//*[local-name()='sheetData']").AppendChild($node) }
    $c=$node.SelectSingleNode("*[local-name()='c'][@r='$ref']")
    if (-not $c) {$c=$xml.CreateElement('c',$mainNs);$c.SetAttribute('r',$ref);[void]$node.AppendChild($c)}
    return $c
}
function Reset($c) { while($c.HasChildNodes){[void]$c.RemoveChild($c.FirstChild)};[void]$c.RemoveAttribute('t') }
function Text($xml,$ns,$ref,$value,$style='') {$c=Cell $xml $ns $ref;Reset $c;$c.SetAttribute('t','inlineStr');if($style){$c.SetAttribute('s',$style)};$is=$xml.CreateElement('is',$mainNs);$t=$xml.CreateElement('t',$mainNs);$t.InnerText=$value;[void]$is.AppendChild($t);[void]$c.AppendChild($is)}
function Number($xml,$ns,$ref,[double]$value,$style='') {$c=Cell $xml $ns $ref;Reset $c;if($style){$c.SetAttribute('s',$style)};$v=$xml.CreateElement('v',$mainNs);$v.InnerText=$value.ToString([Globalization.CultureInfo]::InvariantCulture);[void]$c.AppendChild($v)}
function Formula($xml,$ns,$ref,$formula,$style='') {$c=Cell $xml $ns $ref;Reset $c;if($style){$c.SetAttribute('s',$style)};$f=$xml.CreateElement('f',$mainNs);$f.InnerText=$formula;[void]$c.AppendChild($f);$v=$xml.CreateElement('v',$mainNs);[void]$c.AppendChild($v)}
function ReadEntry($zip,$name) {$e=$zip.GetEntry($name);$r=[IO.StreamReader]::new($e.Open());try{return [xml]$r.ReadToEnd()}finally{$r.Dispose()}}
function WriteEntry($zip,$name,[xml]$xml) {$e=$zip.GetEntry($name);if($e){$e.Delete()};$new=$zip.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal);$settings=[Xml.XmlWriterSettings]::new();$settings.Encoding=[Text.UTF8Encoding]::new($false);$settings.OmitXmlDeclaration=$false;$w=[Xml.XmlWriter]::Create($new.Open(),$settings);try{$xml.Save($w)}finally{$w.Dispose()}}

Copy-Item -LiteralPath $InputPath -Destination $OutputPath -Force
$zip=[IO.Compression.ZipFile]::Open($OutputPath,[IO.Compression.ZipArchiveMode]::Update)
try {
    $sheet=ReadEntry $zip 'xl\worksheets\sheet1.xml';$ns=Ns $sheet
    Text $sheet $ns 'A1' 'CULTIVO TIERRA - PLAN BASE' '1'
    Text $sheet $ns 'A2' 'Version para suelo mineral u organico con riego manual o fertirriego conservador. Requiere ajustar con analisis del sustrato.'
    Text $sheet $ns 'A10' 'EC agua base';Text $sheet $ns 'D10' 'EC medida antes de fertilizar. En tierra se controla la EC de entrada y el drenaje ocasional.'
    $soilRates=@(
        @(30,12,18,4,7), @(40,15,22,5,9), @(45,18,25,5.5,10), @(45,18,25,6,11),
        @(45,18,26,6,13), @(42,17,26,5.5,12), @(36,15,23,5.5,9), @(27,9,19,7,7), @(12,3,10,4,3)
    )
    for($i=0;$i -lt 9;$i++) {
        $row=12+$i;$r=$soilRates[$i]
        Formula $sheet $ns "D$row" "`$B`$3*(0/100)" '1'
        Formula $sheet $ns "E$row" "`$B`$3*($($r[0].ToString([Globalization.CultureInfo]::InvariantCulture))/100)" '1'
        Formula $sheet $ns "F$row" "`$B`$3*($($r[1].ToString([Globalization.CultureInfo]::InvariantCulture))/100)" '1'
        Formula $sheet $ns "G$row" "`$B`$3*($($r[2].ToString([Globalization.CultureInfo]::InvariantCulture))/100)" '1'
        Formula $sheet $ns "H$row" "`$B`$3*($($r[3].ToString([Globalization.CultureInfo]::InvariantCulture))/100)" '1'
        Formula $sheet $ns "I$row" "`$B`$3*($($r[4].ToString([Globalization.CultureInfo]::InvariantCulture))/100)" '1'
        Formula $sheet $ns "J$row" "`$B`$3*'Aditivos'!`$B`$4/40" '1'
        Number $sheet $ns "K$row" 0 '1';Number $sheet $ns "AH$row" 0 '1';Number $sheet $ns "AI$row" 0 '1'
        Number $sheet $ns "O$row" 0;Number $sheet $ns "P$row" 0;Number $sheet $ns "Q$row" 0;Number $sheet $ns "R$row" 0
        Formula $sheet $ns "S$row" "IF(O$row<1,`"MANUAL: ver peso del sustrato`",`"Usar solo en dia de fertilizacion`")"
        Formula $sheet $ns "T$row" "O$row*Q$row"
        Text $sheet $ns "U$row" 'Sin objetivo fijo; evitar saturar'
        Text $sheet $ns "V$row" 'Secado moderado; no dejar marchitar'
        Text $sheet $ns "W$row" '1 riego nutritivo; luego 1-2 riegos de agua segun peso y drenaje'
        Formula $sheet $ns "X$row" "ROUND(Z$row,0)&`" N / `"&ROUND(AC$row,0)&`" P / `"&ROUND(AD$row,0)&`" K / `"&ROUND(AE$row,0)&`" Ca / `"&ROUND(AF$row,0)&`" Mg / `"&ROUND(AG$row,0)&`" S ppm`""
    }
    Text $sheet $ns 'A22' 'REGLAS PARA TIERRA 100%' '1'
    Text $sheet $ns 'B23' 'Riego: esperar que la capa superior se seque y que la maceta pierda peso; no usar pulsos automaticos diarios de coco.'
    Text $sheet $ns 'B24' 'Fertilizacion: aplicar una solucion nutritiva y luego 1-2 riegos de agua, ajustando por respuesta, drenaje y peso de la maceta.'
    Text $sheet $ns 'B25' 'pH orientativo: 6,2-6,8. EC orientativa de entrada: 0,6-1,2 mS/cm, segun el suelo y la fase.'
    Text $sheet $ns 'B26' 'No perseguir runoff en cada riego. Medir drenaje ocasionalmente y bajar dosis si la EC de salida se acumula.'
    Text $sheet $ns 'B27' 'Orden: agua > Micro C > Calcinit prediluido > KNO3 > Epsom > K2SO4 > MKP; completar > EC > pH.'
    Text $sheet $ns 'B28' 'El silicato esta eliminado. No agregarlo.'
    Text $sheet $ns 'B30' 'Esta es una base provisional para tierra. Para cerrarla hace falta conocer mezcla, volumen, pH, EC y si contiene compost o fertilizante.'
    Text $sheet $ns 'A31' 'Supuestos de esta version'
    Text $sheet $ns 'B31' 'Suelo con capacidad de retencion de nutrientes, sin informacion de analisis; dosis minerales reducidas y fertirriego espaciado.'
    $wb=ReadEntry $zip 'xl\workbook.xml';$sheetNameNode=$wb.SelectSingleNode('//*[local-name()="sheet"][@name="Sheet1"]');if($sheetNameNode){$sheetNameNode.SetAttribute('name','Tierra')}
    WriteEntry $zip 'xl\worksheets\sheet1.xml' $sheet;WriteEntry $zip 'xl\workbook.xml' $wb
}
finally {$zip.Dispose()}
Get-Item -LiteralPath $OutputPath | Select-Object FullName,Length,LastWriteTime
