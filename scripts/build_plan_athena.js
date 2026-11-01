const path = require('path');
const ExcelJS = require(path.join(process.env.TEMP, 'athena-sheet-build', 'node_modules', 'exceljs'));

const out = path.join(process.env.USERPROFILE, 'Desktop', 'plan_athena_fade_simple.xlsx');
const wb = new ExcelJS.Workbook();
wb.creator = 'OpenAI';
wb.subject = 'Comparacion teorica con Athena Pro para coco';
wb.calcProperties.fullCalcOnLoad = true;

const ink = '17333A';
const green = '176F5B';
const pale = 'E9F4EF';
const amber = 'FFF0C7';
const editable = 'FFF9DF';
const blue = 'E8F3FA';
const potassiumSulfateK = 2 * 39.0983 / 174.27 * 0.99;
const potassiumSulfateS = 32.065 / 174.27 * 0.99;
const calciumChlorideCa = 40.078 / 110.98 * .93;
const calciumChlorideCl = 70.906 / 110.98 * .93;
const fadeMassGPerL = 19 / 3.785411784 * 9.28 * 453.59237 / 3785.411784;
const fadeCaPpm = fadeMassGPerL * .04 * 1000;
const fadeClPpm = fadeMassGPerL * .07 * 1000;
const fadeFePpm = fadeMassGPerL * .0006 * 1000;

function sheet(name, widths) {
  const ws = wb.addWorksheet(name);
  widths.forEach((width, i) => { ws.getColumn(i + 1).width = width; });
  ws.views = [{ state: 'frozen', ySplit: 1 }];
  ws.pageSetup = { fitToPage: true, fitToWidth: 1, orientation: 'landscape' };
  return ws;
}
function title(ws, text, last) {
  ws.mergeCells(1, 1, 1, last);
  const c = ws.getCell(1, 1);
  c.value = text;
  c.font = { bold: true, color: 'FFFFFF', size: 15 };
  c.fill = { type: 'pattern', pattern: 'solid', fgColor: { argb: `FF${ink}` } };
  c.alignment = { vertical: 'middle' };
  ws.getRow(1).height = 30;
}
function header(ws, row, labels) {
  labels.forEach((v, i) => {
    const c = ws.getCell(row, i + 1);
    c.value = v;
    c.font = { bold: true, color: 'FFFFFF' };
    c.fill = { type: 'pattern', pattern: 'solid', fgColor: { argb: `FF${green}` } };
    c.alignment = { wrapText: true, vertical: 'middle' };
  });
  ws.getRow(row).height = 44;
}
function input(c, format) {
  c.fill = { type: 'pattern', pattern: 'solid', fgColor: { argb: `FF${editable}` } };
  if (format) c.numFmt = format;
}
function formula(ws, address, expression, result, format = '0.0') {
  const c = ws.getCell(address);
  c.value = expression === '0' ? 0 : { formula: expression, result };
  c.numFmt = format;
  c.fill = { type: 'pattern', pattern: 'solid', fgColor: { argb: `FF${blue}` } };
}
function note(ws, row, text, last = 5) {
  ws.mergeCells(row, 1, row, last);
  const c = ws.getCell(row, 1);
  c.value = text;
  c.alignment = { wrapText: true, vertical: 'middle' };
  c.fill = { type: 'pattern', pattern: 'solid', fgColor: { argb: `FF${amber}` } };
  ws.getRow(row).height = 34;
}

const daily = sheet('Preparar tanque', [9, 34, 17, 14, 78, 20]);
const microDaily = sheet('Preparar Micro C', [9, 34, 17, 14, 78, 20]);
const weekly = sheet('Tabla semanal', [13, 16, 18, 16, 16, 16, 16, 16, 16, 18, 16, 15, 94]);

const cfg = sheet('Config', [32, 18, 17, 85]);
title(cfg, 'PLAN ATHENA | Parametros editables', 4);
header(cfg, 2, ['Parametro', 'Valor', 'Unidad', 'Uso']);
const params = [
  [3, 'Agua final del tanque', 50, 'L', 'Completar a este volumen final, no sumar litros a una mezcla ya preparada.'],
  [4, 'Volumen de maceta', 2.5, 'L', 'Tu valor actual; Athena describe 100% coco en 4-10 L.'],
  [5, 'Caudal de cada gotero', 33.33, 'mL/min', 'Medir caudal real con todos los goteros funcionando.'],
  [6, 'Goteros por planta', 2, 'unid', 'Determina los segundos por pulso.'],
  [7, 'Luz ON vegetativo', 17 / 24, 'hh:mm', 'Hora editable.'],
  [8, 'Horas luz vegetativo', 18, 'h', 'Hora OFF calculada a partir de ON + horas luz.'],
  [9, 'Luz ON floracion', 12 / 24, 'hh:mm', 'Hora editable.'],
  [10, 'Horas luz floracion', 12, 'h', 'Hora OFF calculada a partir de ON + horas luz.'],
  [11, 'EC agua base medida', 0.25, 'mS/cm', 'No revela composicion mineral; analizar Ca, Mg, Na, alcalinidad.'],
  [12, 'Volumen final Micro C nuevo', 2, 'L', 'No usar la botella Micro C antigua como si fuera esta nueva receta.'],
  [13, 'Micro C nuevo por litro', 0.5, 'mL/L', 'Equivale a 25 mL en 50 L.'],
  [14, 'Primer pulso tras ON', 2, 'h', 'Horario inicial editable, no una prescripcion fija de Athena.'],
  [15, 'Ultimo pulso antes de OFF', 2, 'h', 'Horario inicial editable; se recalculan todos los pulsos.'],
  [16, 'Pureza CaCl2 Salttech', 93, '%', 'Producto anhidro de CAS 10043-52-4; verificar etiqueta del lote.'],
];
for (const [r, label, value, unit, desc] of params) {
  cfg.getCell(`A${r}`).value = label;
  cfg.getCell(`B${r}`).value = value;
  cfg.getCell(`C${r}`).value = unit;
  cfg.getCell(`D${r}`).value = desc;
  cfg.getRow(r).height = 25;
  input(cfg.getCell(`B${r}`), unit === 'hh:mm' ? 'hh:mm' : undefined);
}
formula(cfg, 'B3', "'Preparar tanque'!B5", 50, '0.0');
formula(cfg, 'B11', "'Preparar tanque'!B6", 0.25, '0.00');
formula(cfg, 'B12', "'Preparar Micro C'!B4", 2, '0.00');
formula(cfg, 'B13', "'Preparar Micro C'!B5", 0.5, '0.00');
cfg.getCell('D3').value = 'Editar litros en Preparar tanque!B5.';
cfg.getCell('D11').value = 'Editar EC base en Preparar tanque!B6.';
cfg.getCell('D12').value = 'Editar volumen en Preparar Micro C!B4.';
cfg.getCell('D13').value = 'Editar dosis en Preparar Micro C!B5.';
note(cfg, 17, 'ATENCION: receta teorica por ppm, no copia certificada de Athena. Las dosis estan basadas en etiquetas; la EC real y el agua deben medirse.', 4);
note(cfg, 18, 'Con 2.5 L, el programa de riego es una propuesta inicial. No atribuir los horarios, pulsos o drybacks a Athena sin sensor VWC/pwEC.', 4);
cfg.getCell('A19').value='Micro C factor final';
formula(cfg,'B19',`(Composicion!H5*Composicion!N5*10+${fadeFePpm})/'Micro C nuevo'!B4`,(2.03+fadeFePpm)/2.884,'0.000');
cfg.getCell('C19').value='x';
cfg.getCell('D19').value='Solo equipara Fe teorico de Bloom + Fade; los otros micros no son identicos.';
cfg.getCell('A20').value='Ca objetivo Fade'; cfg.getCell('B20').value=fadeCaPpm; cfg.getCell('C20').value='mg/L';
cfg.getCell('D20').value='Calculado desde Fade 19 mL/gal, densidad 9.28 lb/gal y 4% Ca.';
cfg.getCell('A21').value='Cl objetivo Fade'; cfg.getCell('B21').value=fadeClPpm; cfg.getCell('C21').value='mg/L';
cfg.getCell('D21').value='Calculado desde Fade 19 mL/gal, densidad 9.28 lb/gal y 7% Cl.';
cfg.getCell('A22').value='Ficha CaCl2 Salttech';
cfg.getCell('B22').value={text:'Abrir producto',hyperlink:'https://www.norteinsumoslab.com/productos/calcio-cloruro-anhidro-pro-analisis-salttech/'};
cfg.getCell('D22').value='Titulo publicado 93%; no sustituir por producto hidratado o antihumedad sin recalcular.';

const comp = sheet('Composicion', [28, 12, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 55]);
title(comp, 'Composicion declarada y supuestos de calculo', 15);
header(comp, 2, ['Producto', 'N %', 'P2O5 %', 'K2O %', 'Ca %', 'Mg %', 'S %', 'Fe %', 'B %', 'Mn %', 'Zn %', 'Cu %', 'Mo %', 'g/L Athena', 'Origen / advertencia']);
const compositions = [
  [3, 'Athena Pro Core', 14, 0, 0, 17, 0, 0, .07, .015, .025, .01, .01, .002, 1.22, 'Etiqueta Athena 2024; 12.2 g/10 L.'],
  [4, 'Athena Pro Grow', 2, 8, 20, 0, 3, 8, .1, 0, 0, 0, 0, 0, 2.03, 'Etiqueta Athena 2024; 20.3 g/10 L.'],
  [5, 'Athena Pro Bloom', 0, 12, 24, 0, 3, 9, .1, 0, 0, 0, 0, 0, 2.03, 'Etiqueta Athena 2024; 20.3 g/10 L.'],
  [7, 'Calcinit propio', 15.5, 0, 0, 19, 0, 0, 0, 0, 0, 0, 0, 0, null, 'Supuesto de la planilla original; confirmar etiqueta del lote.'],
  [8, 'Nitrato K propio', 13.7, 0, 46.1, 0, 0, 0, 0, 0, 0, 0, 0, 0, null, 'Supuesto de la planilla original; confirmar etiqueta.'],
  [9, 'Epsom propio', 0, 0, 0, 0, 9.86, 13, 0, 0, 0, 0, 0, 0, null, 'Heptahidratado; ajustar si producto distinto.'],
  [10, 'Sulfato K propio', 0, 0, potassiumSulfateK * 100 / .8301, 0, 0, potassiumSulfateS * 100, 0, 0, 0, 0, 0, 0, null, 'Salttech K2SO4 anhidro 99% del enlace de compra; ~44.4% K elemental / ~18.2% S.'],
  [11, 'MKP propio', 0, 52, 34, 0, 0, 0, 0, 0, 0, 0, 0, 0, null, '0-52-34; verificar etiqueta real.'],
];
for (const [r, ...vals] of compositions) {
  vals.forEach((v, i) => { comp.getCell(r, i + 1).value = v; });
  for (let col = 2; col <= 14; col++) if (comp.getCell(r, col).value !== null) input(comp.getCell(r, col), '0.000');
}
note(comp, 13, 'Porcentaje N-P2O5-K2O de etiquetas no son N-P-K elementales. Se convierte P2O5 x 0.4364; K2O x 0.8301.', 15);
note(comp, 14, 'Los analisis garantizados son minimos declarados; formulas exactas y EC resultante pueden diferir entre lotes y aguas.', 15);

const micro = sheet('Micro C nuevo', [22, 18, 32, 18, 20, 52, 18]);
title(micro, 'MICRO C NUEVO | 2 L finales; NO es la receta Micro C anterior', 7);
note(micro, 2, 'INGREDIENTES COTEJADOS CON LOS ENLACES DE NUTRICION.xlsx. Receta teorica: la estabilidad de esta mezcla concentrada NO esta ensayada y sus quelatos no son los de Athena.', 7);
header(micro, 3, ['Elemento', 'Objetivo ppm', 'Fuente usada', 'Elemento en fuente %', 'Cantidad para stock g', 'Limite de equivalencia', 'Ficha usada']);
const microRows = [
  [4, 'Fe', '(Composicion!H3*Composicion!N3+Composicion!H5*Composicion!N5)*10', 'Afital Fe-EDTA liquido', 4, 'Fe EDTA; Athena declara Fe DTPA. Pesar liquido.'],
  [5, 'B', 'Composicion!I3*Composicion!N3*10', 'Acido borico', 17.5, 'Bajar B respecto al Micro C anterior.'],
  [6, 'Mn', 'Composicion!J3*Composicion!N3*10', 'Sulfato Mn monohidratado', 31, 'Athena usa Mn quelatado; aqui no.'],
  [7, 'Zn', 'Composicion!K3*Composicion!N3*10', 'Sulfato Zn heptahidratado', 22.7, 'Athena usa Zn quelatado; aqui no.'],
  [8, 'Cu', 'Composicion!L3*Composicion!N3*10', 'Sulfato Cu pentahidratado', 25.45, 'Athena usa Cu quelatado; aqui no.'],
  [9, 'Mo', 'Composicion!M3*Composicion!N3*10', 'Molibdato Na dihidratado', 39.65, 'Confirmar hidrato y balanza; masa muy pequena.'],
];
const microPercent = { Fe:4, B:17.5, Mn:31, Zn:22.7, Cu:25.45, Mo:39.65 };
const microTargets = { Fe:2.884, B:.183, Mn:.305, Zn:.122, Cu:.122, Mo:.0244 };
const microLinks = {
  Fe: 'https://www.afital.com.ar/wp-content/uploads/2022/08/AFITAL-HIERRO-EDTA-ficha-tecnica.docx.pdf',
  B: 'https://www.norteinsumoslab.com/productos/acido-borico-999/',
  Mn: 'https://www.norteinsumoslab.com/productos/manganeso-sulfato-industrial/',
  Zn: 'https://www.norteinsumoslab.com/productos/zinc-sulfato-7-hidrato-pro-analisis-a-c-s-salttech/',
  Cu: 'https://www.mercadolibre.com.ar/sulfato-de-cobre-x250gr-pentahidratado/up/MLAU3398410194',
  Mo: 'https://www.norteinsumoslab.com/productos/sodio-molibdato-2-hidrato-pro-analisis/',
};
for (const [r, element, expr, source, pct, warning] of microRows) {
  micro.getCell(`A${r}`).value = element;
  formula(micro, `B${r}`, expr, microTargets[element], '0.0000');
  micro.getCell(`C${r}`).value = source;
  micro.getCell(`D${r}`).value = pct;
  input(micro.getCell(`D${r}`), '0.00');
  formula(micro, `E${r}`, `B${r}*Config!$B$12/Config!$B$13/(D${r}/100)`, microTargets[element] * 2 / .5 / (pct / 100), '0.000');
  micro.getCell(`F${r}`).value = warning;
  micro.getCell(`G${r}`).value = { text: 'Abrir ficha', hyperlink: microLinks[element] };
  micro.getRow(r).height = 27;
}
note(micro, 11, 'Volumen FINAL 2 L y dosis 0.5 mL/L (25 mL/50 L). Porcentajes basados en fichas de compra y estequiometria; el titulo/ficha publica de Cu indica pentahidrato, pero no pureza analitica.', 7);
note(micro, 12, 'No mezclar sales secas entre si. Disolver una a una en agua. Verificar claridad, pH y estabilidad de la madre antes de usar; no se ha ensayado almacenamiento.', 7);
note(micro, 13, 'NO ES MICRO C IDENTICO A ATHENA: Athena declara Fe-DTPA y Mn/Zn/Cu EDTA; esta receta usa Fe-EDTA/lignosulfonato y sulfatos. Solo aproxima ppm elementales.', 7);
note(micro, 14, 'No usar la antigua Micro C a esta dosis: su B calculado (~0.39 ppm) supera el objetivo de Athena a 3.0 (~0.183 ppm).', 7);
note(micro, 15, 'Afital declara Fe 4% p/p con EDTA y lignosulfonatos. La cantidad en esta hoja es GRAMOS de producto liquido, no mL; sin densidad no se convierte con precision.', 7);

const plan = sheet('Plan', [12, 14, 21, 13, 14, 16, 15, 15, 16, 14, 16, 16, 18, 13, 14, 14, 15, 102, 16, 15, 15, 55,
  12, 12, 12, 12, 12, 12, 14, 14, 14, 14, 14, 14, 14, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 14, 14, 14, 14]);
title(plan, 'PLAN ATHENA APROXIMADO | recetas calculadas con tus sales; NO es producto Athena', 22);
header(plan, 3, ['Semana', 'Etapa', 'Perfil', 'EC Athena referencia', 'EC real tanque', 'Calcinit g', 'KNO3 g', 'Epsom g', 'K2SO4 g', 'MKP g', 'Micro C nuevo mL', 'pH entrada', 'PPFD guia', 'Pulsos/dia', '% maceta/pulso', 'mL/pulso', 'Seg/pulso', 'Todos los horarios', 'mL/dia/planta', 'Runoff real %', 'EC drenaje real', 'Notas riego / equivalencia',
  'N objetivo', 'P objetivo', 'K objetivo', 'Ca objetivo', 'Mg objetivo', 'S objetivo', 'N calculado', 'P calculado', 'K calculado', 'Ca calculado', 'Mg calculado', 'S calculado', 'Diferencia S', 'P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8', 'P9', 'P10', 'P11', 'P12', 'Estado', 'CaCl2 g', 'Cl calculado ppm', 'Cl Fade ppm', 'Diferencia Cl']);
plan.views = [{ state: 'frozen', xSplit: 3, ySplit: 3 }];
plan.autoFilter = { from: 'A3', to: 'AZ15' };
const stages = [
  ['Veg 1', 'Veg', 'Grow + Core', 5, .02, '300-600', 'Propuesta de pulsos; evaluar raiz, peso y drenaje.'],
  ['Veg 2', 'Veg', 'Grow + Core', 6, .02, '300-600', 'No forzar 3.0 si el sistema o la planta no lo toleran.'],
  ['Veg 3', 'Veg', 'Grow + Core', 7, .025, '300-600', 'Coco 2.5 L fuera del rango 4-10 L citado por Athena.'],
  ['Flora 1', 'Flora', 'Bloom + Core', 7, .03, '600-1000', 'Inicio de floracion; medir drenaje y evitar marchitez.'],
  ['Flora 2', 'Flora', 'Bloom + Core', 8, .03, '600-1000', 'P1: rehidratar; P2: mantener. Ajustar con sensor o pesadas.'],
  ['Flora 3', 'Flora', 'Bloom + Core', 8, .03, '600-1000', 'Horarios iguales son propuesta; Athena no fija horas universales.'],
  ['Flora 4', 'Flora', 'Bloom + Core', 9, .03, '850-1200', 'PPFD de guia Athena, no una meta automatica sin CO2.'],
  ['Flora 5', 'Flora', 'Bloom + Core', 9, .03, '850-1200', 'Si EC sustrato sube, revisar drenaje y dryback antes de seguir.'],
  ['Flora 6', 'Flora', 'Bloom + Core', 9, .03, '850-1200', 'No equiparar EC drenaje con pwEC del sustrato.'],
  ['Flora 7', 'Flora', 'Bloom + Core', 8, .03, '850-1200', 'Continuar Core equivalente; monitorear drenaje y madurez.'],
  ['Flora 8', 'Flora', 'Bloom + Fade aprox.', 7, .03, '600-900', 'Sin Calcinit/KNO3: CaCl2 reemplaza Ca; Cl alto similar a Fade. Medir drenaje.'],
  ['Flora 9', 'Flora', 'Bloom + Fade aprox.', 7, .03, '600-900', 'Cero N de sales agregadas; no garantiza mejor sabor ni es Fade identico.'],
];
for (let i = 0; i < stages.length; i++) {
  const r = i + 4;
  const [week, phase, profile, count, shot, ppfd, warning] = stages[i];
  const isFade = i >= 10;
  const source = phase === 'Veg' ? '4' : '5';
  const t = phase === 'Veg' ? { N:211.4,P:70.87136,K:337.0206,Ca:207.4,Mg:60.9,S:162.4 } : { N:isFade?0:170.8,P:106.30704,K:404.42472,Ca:isFade?fadeCaPpm:207.4,Mg:60.9,S:182.7 };
  const c=isFade?0:t.Ca/190, kn=isFade?0:Math.max(0,(t.N-c*155)/137), mk=t.P/(.52*.4364*1000), ep=t.Mg/98.6;
  const ks=(t.K-(mk*.34+kn*.461)*.8301*1000)/(potassiumSulfateK*1000);
  const amounts={F:c*50,G:kn*50,H:ep*50,I:ks*50,J:mk*50,AW:isFade?t.Ca/calciumChlorideCa/1000*50:0};
  plan.getCell(`A${r}`).value = week;
  plan.getCell(`B${r}`).value = phase;
  plan.getCell(`C${r}`).value = profile;
  plan.getCell(`D${r}`).value = 3;
  input(plan.getCell(`E${r}`), '0.00');
  plan.getCell(`K${r}`).value = { formula: `Config!$B$3*Config!$B$13${isFade?'*Config!$B$19':''}`, result: isFade?25*(2.03+fadeFePpm)/2.884:25 };
  plan.getCell(`K${r}`).numFmt = '0.00';
  plan.getCell(`L${r}`).value = '5.8-6.2';
  plan.getCell(`M${r}`).value = ppfd;
  plan.getCell(`N${r}`).value = count; input(plan.getCell(`N${r}`), '0');
  plan.getCell(`O${r}`).value = shot; input(plan.getCell(`O${r}`), '0.0%');
  plan.getCell(`T${r}`).value = null; input(plan.getCell(`T${r}`), '0.0%');
  plan.getCell(`U${r}`).value = null; input(plan.getCell(`U${r}`), '0.00');
  plan.getCell(`V${r}`).value = warning;
  formula(plan, `W${r}`, isFade?'0':`(Composicion!$B$3*Composicion!$N$3+Composicion!$B$${source}*Composicion!$N$${source})*10`, t.N);
  formula(plan, `X${r}`, `Composicion!$C$${source}*Composicion!$N$${source}*10*0.4364`, t.P);
  formula(plan, `Y${r}`, `Composicion!$D$${source}*Composicion!$N$${source}*10*0.8301`, t.K);
  formula(plan, `Z${r}`, isFade?'Config!$B$20':`Composicion!$E$3*Composicion!$N$3*10`, t.Ca);
  formula(plan, `AA${r}`, `Composicion!$F$${source}*Composicion!$N$${source}*10`, t.Mg);
  formula(plan, `AB${r}`, `Composicion!$G$${source}*Composicion!$N$${source}*10`, t.S);
  formula(plan, `F${r}`, isFade?'0':`Z${r}*Config!$B$3/(Composicion!$E$7*10)`, amounts.F, '0.000');
  formula(plan, `G${r}`, isFade?'0':`MAX(0,(W${r}-F${r}*Composicion!$B$7*10/Config!$B$3)*Config!$B$3/(Composicion!$B$8*10))`, amounts.G, '0.000');
  formula(plan, `H${r}`, `AA${r}*Config!$B$3/(Composicion!$F$9*10)`, amounts.H, '0.000');
  formula(plan, `J${r}`, `X${r}*Config!$B$3/(Composicion!$C$11*10*0.4364)`, amounts.J, '0.000');
  formula(plan, `I${r}`, `MAX(0,(Y${r}-(J${r}*Composicion!$D$11+G${r}*Composicion!$D$8)*10*0.8301/Config!$B$3)*Config!$B$3/(Composicion!$D$10*10*0.8301))`, amounts.I, '0.000');
  formula(plan, `P${r}`, `Config!$B$4*1000*O${r}`, 2.5*1000*shot, '0.0');
  formula(plan, `Q${r}`, `P${r}*60/(Config!$B$5*Config!$B$6)`, 2.5*1000*shot*60/(33.33*2), '0.0');
  formula(plan, `S${r}`, `P${r}*N${r}`, 2.5*1000*shot*count, '0.0');
  formula(plan, `AC${r}`, `(F${r}*Composicion!$B$7+G${r}*Composicion!$B$8)*10/Config!$B$3`, t.N);
  formula(plan, `AD${r}`, `J${r}*Composicion!$C$11*10*0.4364/Config!$B$3`, t.P);
  formula(plan, `AE${r}`, `(G${r}*Composicion!$D$8+I${r}*Composicion!$D$10+J${r}*Composicion!$D$11)*10*0.8301/Config!$B$3`, t.K);
  formula(plan, `AW${r}`, isFade?`Z${r}*Config!$B$3/(1000*(40.078/110.98)*(Config!$B$16/100))`:'0', amounts.AW, '0.000');
  formula(plan, `AX${r}`, `AW${r}*(70.906/110.98)*(Config!$B$16/100)*1000/Config!$B$3`, isFade?amounts.AW/50*calciumChlorideCl*1000:0);
  formula(plan, `AY${r}`, isFade?'Config!$B$21':'0', isFade?fadeClPpm:0);
  formula(plan, `AZ${r}`, `AX${r}-AY${r}`, isFade?amounts.AW/50*calciumChlorideCl*1000-fadeClPpm:0);
  formula(plan, `AF${r}`, `(F${r}*Composicion!$E$7*10+AW${r}*(40.078/110.98)*(Config!$B$16/100)*1000)/Config!$B$3`, t.Ca);
  formula(plan, `AG${r}`, `H${r}*Composicion!$F$9*10/Config!$B$3`, t.Mg);
  formula(plan, `AH${r}`, `(H${r}*Composicion!$G$9+I${r}*Composicion!$G$10)*10/Config!$B$3`, ep*130+ks*potassiumSulfateS*1000);
  formula(plan, `AI${r}`, `AH${r}-AB${r}`, ep*130+ks*potassiumSulfateS*1000-t.S);
  const times=[];
  const on=phase==='Veg'?17/24:12/24;
  const hrs=phase==='Veg'?18:12;
  for(let j=0;j<12;j++){
    const col=plan.getColumn(36+j).letter; // AJ:AU
    const time= j<count ? (on+2/24+j*(hrs-4)/24/Math.max(1,count-1))%1 : '';
    const minuteOfDay = time === '' ? null : Math.round(time * 1440) % 1440;
    times.push(minuteOfDay === null ? '' : `${String(Math.floor(minuteOfDay / 60)).padStart(2,'0')}:${String(minuteOfDay % 60).padStart(2,'0')}`);
    const fc=plan.getCell(`${col}${r}`);
    fc.value={formula:`IF($N${r}<${j+1},"",TEXT(MOD(IF($B${r}="Veg",Config!$B$7,Config!$B$9)+Config!$B$14/24+${j}*MAX(0,IF($B${r}="Veg",Config!$B$8,Config!$B$10)-Config!$B$14-Config!$B$15)/24/MAX(1,$N${r}-1),1),"hh:mm"))`,result:times[j]};
  }
  formula(plan, `R${r}`, `TEXTJOIN(" / ",TRUE,AJ${r}:AU${r})`, times.filter(Boolean).join(' / '), '@');
  formula(plan, `AV${r}`, `IF(OR(Config!$B$4<4,ABS(AI${r})>20),"REVISAR LIMITES",IF(E${r}="","MEDIR EC","COMPARABLE"))`, 'REVISAR LIMITES', '@');
  plan.getRow(r).height = 28;
}
plan.getColumn('AJ').hidden=true; for(let i=37;i<=47;i++) plan.getColumn(i).hidden=true;
plan.getColumn('AV').width=22;
for (let col=49; col<=52; col++) plan.getColumn(col).hidden=true;
for (let r = 4; r <= 15; r++) plan.getCell(`N${r}`).dataValidation={type:'whole',operator:'between',formulae:[1,12],allowBlank:false,showErrorMessage:true,error:'Ingresar 1 a 12 pulsos.'};

title(daily, 'PREPARAR TANQUE | Receta de uso diario', 6);
daily.getCell('A4').value = 'Etapa';
daily.getCell('B4').value = 'Flora 8';
daily.getCell('E4').value = 'Elegir la semana real en la lista. La tabla de abajo cambia sola.';
daily.getCell('A5').value = 'Agua';
daily.getCell('B5').value = 50;
daily.getCell('C5').value = 'L finales';
daily.getCell('E5').value = 'Litros finales del tanque, no litros iniciales.';
daily.getCell('A6').value = 'EC base';
daily.getCell('B6').value = 0.25;
daily.getCell('C6').value = 'mS/cm';
daily.getCell('E6').value = 'Lectura del agua antes de agregar fertilizantes.';
daily.getCell('A7').value = 'EC guia';
formula(daily, 'B7', 'INDEX(Plan!$D$4:$D$15,MATCH($B$4,Plan!$A$4:$A$15,0))', 3, '0.0');
daily.getCell('C7').value = 'mS/cm';
daily.getCell('E7').value = 'Referencia Athena, NO una lectura garantizada ni una orden de subir sales.';
daily.getCell('A8').value = 'EC real';
daily.getCell('B8').value = null;
daily.getCell('C8').value = 'mS/cm';
daily.getCell('E8').value = 'Anotar aqui lo que mide el tanque ya terminado.';
daily.getCell('A9').value = 'pH guia';
formula(daily, 'B9', 'INDEX(Plan!$L$4:$L$15,MATCH($B$4,Plan!$A$4:$A$15,0))', '5.8-6.2', '@');
daily.getCell('E9').value = 'Medir y ajustar pH al final de la mezcla.';
for(const r of [4,5,6,8]) input(daily.getCell(`B${r}`),r===4?undefined:r===5?'0.0':'0.00');
daily.getCell('B4').dataValidation={type:'list',allowBlank:false,formulae:['"'+stages.map(x=>x[0]).join(',')+'"'],showErrorMessage:true,error:'Elegir una etapa de la lista.'};
for(const r of [5,6]) daily.getCell(`B${r}`).dataValidation={type:'decimal',operator:'greaterThan',formulae:[r===5?0:0],allowBlank:false,showErrorMessage:true,error:'Ingresar un valor positivo.'};
header(daily, 10, ['Paso', 'Producto', 'Cantidad', 'Unidad', 'Que hacer']);
const chosen = stages.findIndex(x=>x[0]==='Flora 8')+4;
const tankIngredients = [
  [11,'Micro C nuevo','K','mL','Agitar el frasco; agregar al agua del tanque.'],
  [12,'Calcinit / nitrato de calcio','F','g','Disolver aparte y verter lentamente.'],
  [13,'Nitrato de potasio','G','g','Disolver aparte; agregar despues de Calcinit.'],
  [14,'Epsom / sulfato de magnesio','H','g','Disolver aparte; agregar con el tanque circulando.'],
  [15,'Sulfato de potasio','I','g','Disolver aparte; esperar a que no queden cristales.'],
  [17,'CaCl2 anhidro Salttech 93%','AW','g','Solo en Flora 8-9; disolver aparte. Omitir si marca cero.'],
  [16,'MKP / fosfato monopotásico','J','g','Disolver aparte; agregar al final de las sales.'],
];
tankIngredients.forEach(([r,name,col,unit,help])=>{
  daily.getCell(`A${r}`).value=r-10;
  daily.getCell(`B${r}`).value=name;
  const planValue = plan.getCell(`${col}${chosen}`).value;
  formula(daily,`C${r}`,`INDEX(Plan!$${col}$4:$${col}$15,MATCH($B$4,Plan!$A$4:$A$15,0))`,typeof planValue==='object'?planValue.result:planValue,'0.000');
  daily.getCell(`D${r}`).value=unit;
  daily.getCell(`E${r}`).value=help;
  daily.getRow(r).height=26;
});
note(daily,18,'ORDEN DE PREPARACION DEL TANQUE | Las cantidades de arriba son para el volumen FINAL indicado en B5.',6);
const tankSteps=[
  [19,'Cargar cerca del 75% del agua en el tanque y encender la circulacion.',`Cargar cerca de ${Number(50*.75).toFixed(1)} L de agua en el tanque y encender la circulacion.`,`"Cargar cerca de "&TEXT($B$5*0.75,"0.0")&" L de agua en el tanque y encender la circulacion."`],
  [20,'Agregar Micro C nuevo al agua; mezclar bien.',`Agregar ${plan.getCell(`K${chosen}`).value.result.toFixed(2)} mL de Micro C nuevo al agua; mezclar bien.`,`"Agregar "&TEXT(C11,"0.00")&" mL de Micro C nuevo al agua; mezclar bien."`],
  [21,'Agregar Calcinit solo si corresponde.','Omitir Calcinit.',`IF(C12=0,"Omitir Calcinit.","Disolver "&TEXT(C12,"0.000")&" g de Calcinit por separado y agregar lentamente.")`],
  [22,'Agregar KNO3 solo si corresponde.','Omitir KNO3.',`IF(C13=0,"Omitir KNO3.","Disolver "&TEXT(C13,"0.000")&" g de KNO3 por separado y agregar.")`],
  [23,'Agregar CaCl2 solo si corresponde.',`Disolver ${plan.getCell(`AW${chosen}`).value.result.toFixed(3)} g de CaCl2 por separado.`,`IF(C17=0,"Omitir CaCl2.","Disolver "&TEXT(C17,"0.000")&" g de CaCl2 por separado y agregar lentamente.")`],
  [24,'Disolver y agregar Epsom por separado.',`Disolver ${plan.getCell(`H${chosen}`).value.result.toFixed(3)} g de Epsom por separado.`,`"Disolver "&TEXT(C14,"0.000")&" g de Epsom por separado."`],
  [25,'Disolver y agregar sulfato de potasio por separado; confirmar que se disuelva.',`Disolver ${plan.getCell(`I${chosen}`).value.result.toFixed(3)} g de sulfato de potasio por separado; confirmar disolucion.`,`"Disolver "&TEXT(C15,"0.000")&" g de sulfato de potasio por separado; confirmar disolucion."`],
  [26,'Disolver y agregar MKP por separado. Nunca juntar las sales secas ni concentrados.',`Disolver ${plan.getCell(`J${chosen}`).value.result.toFixed(3)} g de MKP por separado.`,`"Disolver "&TEXT(C16,"0.000")&" g de MKP por separado. Nunca juntar sales secas ni concentrados."`],
  [27,'Completar hasta el volumen FINAL indicado arriba. Mezclar, medir EC y pH.',null,null],
  [28,'Registrar EC real en B8. Si difiere de la guia, no aumentar todas las sales a ciegas.',null,null],
];
tankSteps.forEach(([r,label,cached,expr],i)=>{
  daily.getCell(`A${r}`).value=i+1;
  daily.mergeCells(r,2,r,5);
  if(expr) daily.getCell(`B${r}`).value={formula:expr,result:cached};
  else daily.getCell(`B${r}`).value=label;
  daily.getCell(`B${r}`).alignment={wrapText:true,vertical:'middle'};
  daily.getRow(r).height=31;
});
note(daily,30,'Flora 8-9 aproxima Fade con CaCl2, pero no replica quelatos ni garantiza EC o sabor. Medir EC/pH de entrada y drenaje; Cl exige analisis aparte.',6);
daily.views=[{state:'frozen',ySplit:10,showGridLines:false}];

title(microDaily, 'PREPARAR MICRO C | Una sola vez, volumen final editable', 6);
microDaily.getCell('A4').value='Volumen'; microDaily.getCell('B4').value=2;
microDaily.getCell('C4').value='L finales';
microDaily.getCell('E4').value='Cambiar aqui el tamano de la botella; todas las cantidades se recalculan.';
microDaily.getCell('A5').value='Dosis'; microDaily.getCell('B5').value=.5;
microDaily.getCell('C5').value='mL/L';
microDaily.getCell('E5').value='Dosis base hasta Flora 7; Flora 8-9 usa el factor final de Config para Fe.';
microDaily.getCell('A6').value='Dosis final';
formula(microDaily,'B6','B5*Config!B19',.5*(2.03+fadeFePpm)/2.884,'0.000');
microDaily.getCell('C6').value='mL/L';
microDaily.getCell('E6').value='Solo Flora 8-9; otros micros no igualan Fade.';
for(const r of [4,5]){input(microDaily.getCell(`B${r}`),'0.00');microDaily.getCell(`B${r}`).dataValidation={type:'decimal',operator:'greaterThan',formulae:[0],allowBlank:false,showErrorMessage:true,error:'Ingresar un valor positivo.'};}
header(microDaily,7,['Paso','Producto','Cantidad','Unidad','Especificacion']);
const microIngredients=[
  [8,'Acido borico',5,'H3BO3 99.9%; aporta B.'],
  [9,'Sulfato de manganeso',6,'Monohidratado; 31% Mn.'],
  [10,'Sulfato de zinc',7,'Heptahidratado; ZnSO4.7H2O.'],
  [11,'Sulfato de cobre',8,'Pentahidratado; CuSO4.5H2O.'],
  [12,'Molibdato de sodio',9,'Dihidratado; Na2MoO4.2H2O.'],
  [13,'Afital hierro EDTA liquido',4,'4% Fe p/p. PESAR en gramos, no medir en mL.'],
];
microIngredients.forEach(([r,name,src,help],i)=>{
  microDaily.getCell(`A${r}`).value=i+1;
  microDaily.getCell(`B${r}`).value=name;
  formula(microDaily,`C${r}`,`'Micro C nuevo'!E${src}`,micro.getCell(`E${src}`).value.result,'0.000');
  microDaily.getCell(`D${r}`).value='g';
  microDaily.getCell(`E${r}`).value=help;
  microDaily.getRow(r).height=27;
});
note(microDaily,15,'COMO PREPARAR MICRO C | Agua destilada u osmosis; completar al volumen FINAL B4, no agregar B4 litros a las sales.',6);
const microSteps=[
  ['Poner aproximadamente 1.50 L de agua limpia en un recipiente limpio.','"Poner aproximadamente "&TEXT($B$4*0.75,"0.00")&" L de agua limpia en un recipiente limpio."'],
  ['Disolver 4.183 g de acido borico por separado en un poco de agua y agregar.','"Disolver "&TEXT(C8,"0.000")&" g de acido borico por separado en un poco de agua y agregar."'],
  ['Agregar por separado Mn 3.935 g, Zn 2.150 g, Cu 1.917 g y Mo 0.246 g; mezclar entre cada uno.','"Agregar por separado Mn "&TEXT(C9,"0.000")&" g, Zn "&TEXT(C10,"0.000")&" g, Cu "&TEXT(C11,"0.000")&" g y Mo "&TEXT(C12,"0.000")&" g; mezclar entre cada uno."'],
  ['Pesar 288.400 g de Afital liquido, agregarlo al final y homogeneizar.','"Pesar "&TEXT(C13,"0.000")&" g de Afital liquido, agregarlo al final y homogeneizar."'],
  'Completar con agua hasta el volumen FINAL indicado en B4 y mezclar.',
  'Rotular contenido y fecha. Guardar cerrado, fresco, oscuro y separado de alimentos.',
  'Observar si aparecen cristales, turbidez o sedimento antes de usar. Vida util no validada.',
];
microSteps.forEach((entry,i)=>{const r=i+16;microDaily.getCell(`A${r}`).value=i+1;microDaily.mergeCells(r,2,r,5);microDaily.getCell(`B${r}`).value=Array.isArray(entry)?{formula:entry[1],result:entry[0]}:entry;microDaily.getCell(`B${r}`).alignment={wrapText:true,vertical:'middle'};microDaily.getRow(r).height=32;});
note(microDaily,24,'Esta receta aproxima los ppm de micronutrientes; NO es igual a Athena: Fe EDTA/lignosulfonato y sulfatos en lugar de los quelatos de Pro Core.',6);
microDaily.views=[{state:'frozen',ySplit:7,showGridLines:false}];

title(weekly,'TABLA SEMANAL | Cantidades para los litros elegidos en Preparar tanque',13);
header(weekly,3,['Etapa','Fase','Micro C mL','Calcinit g','CaCl2 g','Nitrato K g','Epsom g','Sulfato K g','MKP g','EC guia','pH entrada','Pulsos/dia','Horarios de riego']);
for(let i=0;i<stages.length;i++){
  const r=i+4;
  weekly.getCell(`A${r}`).value=stages[i][0];
  weekly.getCell(`B${r}`).value=stages[i][1];
  for(const [target,source] of [['C','K'],['D','F'],['E','AW'],['F','G'],['G','H'],['H','I'],['I','J'],['J','D'],['K','L'],['L','N'],['M','R']]){
    const v=plan.getCell(`${source}${r}`).value;
    weekly.getCell(`${target}${r}`).value={formula:`Plan!${source}${r}`,result:v&&typeof v==='object'?v.result:v};
    if('CDEFGHI'.includes(target))weekly.getCell(`${target}${r}`).numFmt='0.000';
  }
  weekly.getRow(r).height=27;
}
weekly.autoFilter={from:'A3',to:'M15'};
weekly.views=[{state:'frozen',xSplit:2,ySplit:3,showGridLines:false}];
note(weekly,17,'Flora 8-9 usa CaCl2 en lugar de Calcinit y KNO3. Las cantidades cambian con los litros; EC guia 3.0 no es la EC medida. Ver Config para pureza y horarios.',13);
for(let col=23;col<=35;col++)plan.getColumn(col).hidden=true;
micro.state='hidden';
comp.state='hidden';

const guide = sheet('Guia', [32, 108]);
title(guide, 'Como usar | limites y decisiones', 2);
header(guide, 3, ['Paso', 'Indicacion']);
const steps=[
  ['1. Verificar productos','Comprobar analisis real de Calcinit, KNO3, Epsom, K2SO4, MKP y micros. Editar Composicion si cambian las etiquetas.'],
  ['2. Micro C nuevo','Las fichas de compra de NUTRICION.xlsx se cotejaron para los seis insumos; Cu pentahidratado se toma de la ficha publica, sin pureza analitica declarada. Los ppm son teoricos. Comprobar claridad/estabilidad de la madre; no sustituye los quelatos DTPA/EDTA de Athena.'],
  ['3. Agua','EC 0.25 del grifo no identifica Ca/Mg/Na/bicarbonatos. Sin analisis del agua, ppm finales no son exactos. Athena usa agua de baja EC en sus referencias.'],
  ['4. Mezcla','Seguir Preparar tanque: agua, Micro C, Calcinit o CaCl2 segun semana, KNO3 si corresponde, Epsom, K2SO4, MKP. Disolver cada sal por separado y agregar al tanque diluido. Completar volumen final; medir EC y pH. Nunca mezclar concentrados de Ca con sulfatos/fosfatos.'],
  ['5. EC','3.0 es la referencia publicada Athena Pro para veg/floracion, no garantia de que esta mezcla marque 3.0. Anotar EC real en Plan; no escalar todas las sales solo por una lectura.'],
  ['6. Riego','Los horarios salen de ON, horas luz, retraso inicial, margen final y pulsos/dia. Se muestran TODOS los pulsos. Cambiar esos parametros actualiza el horario.'],
  ['7. Crop steering','Athena define P0/P1/P2/P3 segun VWC y pwEC. Sin sensor de sustrato, estos pulsos son solo un calendario inicial; medir peso de maceta, drenaje y salud.'],
  ['8. Maceta 2.5 L','Guia Athena cita coco 4-10 L; en 2.5 L el riesgo de dryback excesivo y fallos de gotero es mayor. No asumir el mismo margen operativo.'],
  ['9. Final de flora','En Flora 8-9 se sustituye Calcinit/KNO3 por CaCl2 Salttech 93%, manteniendo Bloom equivalente. Aporta ~223 ppm Ca y ~395 ppm Cl sin N agregado. Cl es alto; verificar etiqueta, EC y drenaje. No es Fade identico por la especiacion micro.'],
  ['11. Especiacion de N','Athena Core etiqueta 14% N nitrico. Calcinit suele incluir N amoniacal: verificar etiqueta del lote. Igualar N total no garantiza igualar la forma de nitrogeno.'],
  ['10. Calidad','Quitar N de sales en final imita Fade, pero no demuestra que el exceso de N causara mal sabor. No hay garantia de mayor rendimiento, resina, terpenos ni sabor en tu genetica.'],
];
steps.forEach(([a,b],i)=>{guide.getCell(i+4,1).value=a;guide.getCell(i+4,2).value=b;guide.getCell(i+4,2).alignment={wrapText:true,vertical:'middle'};guide.getRow(i+4).height=48;});

const src = sheet('Fuentes', [35, 107]);
title(src, 'Fuentes primarias y trazabilidad', 2);
header(src, 3, ['Fuente', 'Enlace / uso']);
const links=[
  ['Athena programa Pro','https://www.athenaag.com/pro-line'],
  ['Athena tabla Pro','https://support.athenaag.com/hc/en-us/articles/17190427112859-Pro-Line-Feed-Schedules'],
  ['Athena dosificacion Pro','https://support.athenaag.com/hc/en-us/articles/17294260141595-Pro-Line-Dosage-Reference-Guide'],
  ['Athena etiquetas oficiales','https://www.athenaag.com/sds-sheets'],
  ['Core etiqueta Athena','https://cultivatextreme.com/wp-content/uploads/2024/09/Athena-Pro-Core-Label.pdf'],
  ['Grow etiqueta Athena','https://cultivatextreme.com/wp-content/uploads/2024/09/Athena-Pro-Grow-Label.pdf'],
  ['Bloom etiqueta Athena','https://cultivatextreme.com/wp-content/uploads/2024/09/Athena-Pro-Bloom-Label.pdf'],
  ['Fade etiqueta Athena','https://cultivatextreme.com/wp-content/uploads/2024/09/Athena-Pro-Fade-Label.pdf'],
  ['Athena procedimiento Fade','https://support.athenaag.com/hc/en-us/article_attachments/14949498083739'],
  ['CaCl2 Salttech anhidro 93%','https://www.norteinsumoslab.com/productos/calcio-cloruro-anhidro-pro-analisis-salttech/'],
  ['Athena riego precision','https://support.athenaag.com/hc/en-us/articles/25975395644315-Precision-Irrigation-Strategy'],
  ['Afital Fe EDTA ficha tecnica','https://www.afital.com.ar/wp-content/uploads/2022/08/AFITAL-HIERRO-EDTA-ficha-tecnica.docx.pdf'],
  ['Supuestos de sales propias','Tomados de la planilla NUTRICION Emergen C - produccion y terpenos.xlsx; confirmar cada etiqueta.'],
  ['Sulfato K propio','https://www.norteinsumoslab.com/productos/potasio-sulfato-anhidro-pro-analisis-a-c-s-salttech/'],
];
links.forEach(([a,b],i)=>{const r=i+4;src.getCell(r,1).value=a;src.getCell(r,2).value=b.startsWith('https')?{text:b,hyperlink:b}:b;src.getRow(r).height=27;});

const compare = sheet('Comparativa', [13, 13, 15, 15, 14, 15, 15, 14, 15, 15, 14, 15, 15, 14, 15, 15, 14, 15, 15, 14, 22, 15, 64, 13, 13, 13, 13, 13]);
title(compare, 'COMPARATIVA | Plan anterior vs perfil Athena Pro 3.0', 23);
note(compare, 2, 'Valores elementales calculados en mg/L (ppm), sin minerales del agua. Athena 3.0 es referencia publicada; la EC final de la imitacion aun no fue medida.', 23);
header(compare, 3, ['Semana', 'Fase', 'N anterior', 'N Athena', 'Delta N', 'P anterior', 'P Athena', 'Delta P', 'K anterior', 'K Athena', 'Delta K', 'Ca anterior', 'Ca Athena', 'Delta Ca', 'Mg anterior', 'Mg Athena', 'Delta Mg', 'S anterior', 'S Athena', 'Delta S', 'EC anterior medida', 'EC Athena guia', 'Lectura / limite']);
compare.views = [{ state: 'frozen', xSplit: 2, ySplit: 3, showGridLines: false }];
compare.autoFilter = { from: 'A3', to: 'W15' };
const oldPerLiter = [
  [.526,.207,.299,.069,.113], [.684,.248,.350,.096,.157],
  [.737,.298,.401,.084,.179], [.769,.298,.390,.092,.201],
  [.776,.291,.390,.084,.223], [.749,.284,.385,.081,.201],
  [.690,.314,.380,.057,.176], [.664,.307,.375,.056,.167],
  [.637,.301,.370,.055,.159], [.611,.294,.365,.054,.154],
  [.585,.250,.355,.077,.141], [.558,.207,.355,.098,.132],
];
for (let i = 0; i < oldPerLiter.length; i++) {
  const r = i + 4;
  const [ca, kn, ep, ks, mkp] = oldPerLiter[i];
  const old = {
    N: (ca*.155+kn*.137)*1000,
    P: mkp*.52*.4364*1000,
    K: ((mkp*.34+kn*.461)*.8301+ks*potassiumSulfateK)*1000,
    Ca: ca*.19*1000,
    Mg: ep*.0986*1000,
    S: (ep*.13+ks*potassiumSulfateS)*1000,
  };
  const t = i < 3 ? {N:211.4,P:70.87136,K:337.0206,Ca:207.4,Mg:60.9,S:162.4}
                  : {N:i>=10?0:170.8,P:106.30704,K:404.42472,Ca:i>=10?fadeCaPpm:207.4,Mg:60.9,S:182.7};
  compare.getCell(`A${r}`).value = stages[i][0];
  compare.getCell(`B${r}`).value = stages[i][1];
  [ca,kn,ep,ks,mkp].forEach((v,j)=>compare.getCell(r,24+j).value=v);
  const original = {
    C: [`(X${r}*Composicion!$B$7+Y${r}*Composicion!$B$8)*10`,old.N],
    F: [`AB${r}*Composicion!$C$11*10*0.4364`,old.P],
    I: [`(Y${r}*Composicion!$D$8+AA${r}*Composicion!$D$10+AB${r}*Composicion!$D$11)*10*0.8301`,old.K],
    L: [`X${r}*Composicion!$E$7*10`,old.Ca],
    O: [`Z${r}*Composicion!$F$9*10`,old.Mg],
    R: [`(Z${r}*Composicion!$G$9+AA${r}*Composicion!$G$10)*10`,old.S],
  };
  for (const [col,[expression,value]] of Object.entries(original)) formula(compare,`${col}${r}`,expression,value);
  const pairs = [['D','W',t.N],['G','X',t.P],['J','Y',t.K],['M','Z',t.Ca],['P','AA',t.Mg],['S','AB',t.S]];
  for (const [col,source,value] of pairs) formula(compare,`${col}${r}`,`Plan!${source}${r}`,value);
  for (const [diff,a,b,val] of [['E','D','C',t.N-old.N],['H','G','F',t.P-old.P],['K','J','I',t.K-old.K],['N','M','L',t.Ca-old.Ca],['Q','P','O',t.Mg-old.Mg],['T','S','R',t.S-old.S]]) {
    formula(compare,`${diff}${r}`,`${a}${r}-${b}${r}`,val,'+0.0;-0.0;0.0');
    if(Math.abs(val)>30)compare.getCell(`${diff}${r}`).fill={type:'pattern',pattern:'solid',fgColor:{argb:`FF${amber}`}};
  }
  compare.getCell(`U${r}`).value = i===4 ? '~1.35 (lote previo)' : 'no medida';
  compare.getCell(`V${r}`).value = 3;
  compare.getCell(`W${r}`).value = i>=10 ? 'Replica final: CaCl2 por Ca, N cero de sales; micros y S no identicos a Fade.' :
    (i<3 ? 'En vegetativo la imitacion queda ~24 ppm S por debajo de Athena.' : 'Athena aporta bastante mas P/K/S que el plan anterior; no garantiza mejor cosecha.');
  compare.getRow(r).height=27;
}
for(let col=24;col<=28;col++)compare.getColumn(col).hidden=true;
header(compare, 18, ['Microelemento', 'Anterior ppm', 'Athena 3.0 ppm', 'Diferencia', 'Forma anterior / imitacion', 'Forma declarada Athena']);
const oldMicros = [['Fe',2.10,2.884,'Fe EDTA','Fe DTPA'],['B',.39,.183,'Acido borico','Acido borico'],['Mn',.60,.305,'Sulfato Mn','Mn EDTA'],['Zn',.12,.122,'Sulfato Zn','Zn EDTA'],['Cu',.030,.122,'Sulfato Cu','Cu EDTA'],['Mo',.020,.0244,'Molibdato Na','Molibdato Na']];
oldMicros.forEach(([el,oldVal,newVal,oldForm,athenaForm],i)=>{
  const r=19+i;
  compare.getCell(`A${r}`).value=el;
  compare.getCell(`B${r}`).value=oldVal; compare.getCell(`B${r}`).numFmt='0.0000';
  const targetRow={Fe:4,B:5,Mn:6,Zn:7,Cu:8,Mo:9}[el];
  formula(compare,`C${r}`,`'Micro C nuevo'!B${targetRow}`,newVal,'0.0000');
  formula(compare,`D${r}`,`C${r}-B${r}`,newVal-oldVal,'+0.0000;-0.0000;0.0000');
  compare.getCell(`E${r}`).value=oldForm;
  compare.getCell(`F${r}`).value=athenaForm;
});
note(compare, 26, 'La receta Micro C nueva aproxima cantidades, pero NO iguala la forma quelatada de Athena. El Micro C anterior no sirve como sustituto a la misma dosis.', 23);
note(compare, 27, 'Original = copia de las cantidades g/L de NUTRICION Emergen C - produccion y terpenos.xlsx al generar este archivo. Si cambias el original, esta copia NO se actualiza sola.', 23);
note(compare, 28, 'Delta = Athena menos anterior. EC no es ppm: solo se registro ~1.35 en un lote anterior de semana 5; no se midio la EC real de la imitacion.', 23);
note(compare, 29, 'Eleccion orientativa para tu situacion actual: usar la anterior como base observada y evaluar ajustes controlados; la imitacion 3.0 no esta validada en tu agua/maceta.', 23);
note(compare, 31, 'IMPORTANTE: la tabla superior compara el PLAN ANTERIOR con Athena. La tabla siguiente compara la REPLICA NUEVA con Athena.', 23);
header(compare, 32, ['Perfil replica', 'Fase', 'N replica', 'N Athena', 'Delta N', 'P replica', 'P Athena', 'Delta P', 'K replica', 'K Athena', 'Delta K', 'Ca replica', 'Ca Athena', 'Delta Ca', 'Mg replica', 'Mg Athena', 'Delta Mg', 'S replica', 'S Athena', 'Delta S']);
for (const [r, sourceRow, label] of [[33,4,'Vegetativo'],[34,7,'Floracion 1-7'],[35,14,'Floracion 8-9']]) {
  compare.getCell(`A${r}`).value=label;
  compare.getCell(`B${r}`).value=sourceRow===4?'Veg':'Flora';
  const fields=[['C','AC','D','W','E'],['F','AD','G','X','H'],['I','AE','J','Y','K'],['L','AF','M','Z','N'],['O','AG','P','AA','Q'],['R','AH','S','AB','T']];
  for(const [replicaCol,actual,athenaCol,target,delta] of fields){
    const replica=plan.getCell(`${actual}${sourceRow}`).value.result;
    const athena=plan.getCell(`${target}${sourceRow}`).value.result;
    formula(compare,`${replicaCol}${r}`,`Plan!${actual}${sourceRow}`,replica);
    formula(compare,`${athenaCol}${r}`,`Plan!${target}${sourceRow}`,athena);
    formula(compare,`${delta}${r}`,`${athenaCol}${r}-${replicaCol}${r}`,athena-replica,'+0.0;-0.0;0.0');
  }
  compare.getRow(r).height=27;
}
note(compare, 36, 'La replica nueva iguala N/P/K/Ca/Mg por calculo de etiquetas. El sulfato K Salttech 99% aporta ~44.4% K y ~18.2% S; ver diferencia de S calculada en Plan.', 23);
note(compare, 37, 'Semanas 8-9: CaCl2 93% aproxima Ca/Cl de Fade a 19 mL/gal. No replica los quelatos, los micros completos ni garantiza EC final 3.0.', 23);
guide.state='hidden';
src.state='hidden';
compare.state='hidden';

for (const ws of wb.worksheets) {
  ws.views = ws.views.map(view => ({ ...view, showGridLines: false }));
  ws.eachRow((row, rowNumber)=>{
    if(rowNumber<=3) return;
    row.eachCell(c=>{if(!c.alignment || !c.alignment.wrapText)c.alignment={...c.alignment,vertical:'middle'};});
  });
}

wb.xlsx.writeFile(out).then(()=>console.log(out)).catch(err=>{console.error(err);process.exitCode=1;});
