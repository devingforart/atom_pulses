# Probar PULSO en Ableton Live

## Contrato 0.43

Además de procedencia y cobertura, la raíz de `request.json` publica
`audible_thematic_similarity`, `bass_phrase_continuity`, `density_control` y
`peak_active_voices`. Son métricas de las notas compuestas antes de elegir sonidos y permiten
distinguir 15 roles compositivos fuente de las pistas finales que Live expande por articulación
y round-robin.

Si el plan GPT tiene cobertura extremadamente baja, etiquetas temáticas sin semejanza audible,
bajo severamente fragmentado o acumulación persistente de voces, la interfaz muestra
`COMPOSITION GATE - CURRENT IDEA KEPT`. Este rechazo es musical y se diferencia de un defecto de
integridad MIDI o de un fallo tímbrico del despliegue.

El contrato tímbrico interpreta únicamente atributos positivos. Las negaciones `no`, `not`,
`without`, `sin` y `sans` invierten el atributo correspondiente; por ejemplo, `no reverb`
refuerza `dry`. Indicaciones numéricas de gate, release o duración se entregan al adaptador de
playback y no se califican como carácter del preset.

## Contrato 0.42

La solicitud de Live usa schema 8 y conserva `origin` y `narrative_id` en cada nota. En la raíz
publica `narrative_score`, `ai_authored_note_ratio`, `primary_voice_authorship_coverage`,
`thematic_recall_ratio` y los defectos narrativos detectados. Estos campos auditan exclusivamente
la composición MIDI; no dependen del preset ni del audio elegido en Live.

Una transformación de una célula GPT continúa siendo autoría AI; una nota agregada únicamente
para impedir un silencio accidental aparece como `local_continuity`. Así puede comprobarse si
la identidad musical procede realmente del modelo o del fallback procedural.

## Contrato 0.41

Antes de crear pistas, Live resuelve el score completo. Si kick, sub, bajo de movimiento o lead
no tienen una realización que supere el piso tímbrico, el estado muestra `AUDIBLE TIMBRE GATE
REJECTED` y el despliegue anterior permanece intacto. Las voces featured pueden degradarse de
forma explícita sin fingir que el preset coincide.

Closed hats, clap, snare y shaker con suficientes ataques buscan dos o tres muestras aisladas
distintas. Cada variante recibe una pista `RR` y una porción determinista de las notas; la suma
de todas las variantes reproduce exactamente el MIDI original. `status.json` informa contratos,
variantes y parámetros de release limitados.

Al reproducir Arrangement, el puente escribe `%LOCALAPPDATA%\PULSO\LiveBridge\audible_audit.json`.
El archivo mide presencia real y colas mediante los meters de Live. No contiene análisis espectral
porque el Control Surface no recibe buffers de audio; para FFT o masking medido haría falta un
dispositivo de captura insertado en cada pista o en el master.

## Contrato 0.40

La pista exportada conserva el reparto exacto que GPT escribió. PULSO no añade familias genéricas
para completar una cuota y realiza una última auditoría de registro y articulación sobre el MIDI
que realmente llegará a Live. El informe expone los compases sin kick antes/después, los compases
de ancla creados y cualquier reparación percusiva tardía.

En música club puede existir una bajada extensa, pero no más de dieciséis compases consecutivos sin
ancla de kick salvo un silencio completo explícito. Las frases de hats, clap, shaker y percusión
desarrollan su cierre sin desplazar ataques: la reproducción original continúa cuantizada y la
interpretación humana sigue siendo una transformación opcional y reversible.

El matcher decide la coherencia del preset desde el nombre/intención propia de la pista. El texto
descriptivo del rol no contamina la búsqueda; por ejemplo, «ausente durante ataques secos» no hace
que un pad ambiental compatible sea rechazado por contener la palabra «seco».

## Contrato 0.39

El reparto electrónico creado por GPT es vinculante: PULSO no completa silenciosamente una
voz sin dueño con una pista orquestal genérica. El audit de composición informa voces y notas
implícitas podadas, respuestas derivadas por instrumento, choques verticales antes/después y
la pausa global más larga. El crítico debe corregir la autoría para que esas reparaciones sean
cero en la siguiente propuesta.

El puente de Live resuelve cada pista usando exclusivamente `live_preset_intent`; las palabras
de otras pistas presentes en la paleta global no contaminan la búsqueda. `status.json` añade
`intent_consistency` por pista y `mean_intent_consistency` global. Una pista llamada, por
ejemplo, “Glassy Piano” con intención “soft felt piano” queda degradada explícitamente.

Los cortes que evitan una colisión grave son timing musical intencional en una grilla de 1/16,
no humanización irreversible. El MIDI exportado continúa exacto; el botón Human Performance
sigue siendo la única capa reversible que desplaza la interpretación.

## Contrato 0.38

La música electrónica publicada atraviesa un gate de identidad posterior a la orquestación.
El groove recuerda su esqueleto durante pares de frases, las contestaciones provienen del ADN
del hook y los efectos de transición sólo aparecen alrededor de cambios de sección. Las notas
de percusión reciben duraciones reproducibles antes de exportarse, sin desplazar sus ataques.

La selección de Live distingue identidad instrumental de fidelidad de carácter. Un preset de
flauta genérico puede mantener audible una parte pedida como `breathy alto flute`, pero queda
registrado como `character_fallback` con `intent_fidelity` baja. `mean_intent_fidelity` entra
en `deployed_audible_score`, de modo que una carga completa ya no oculta un reparto tímbrico
incorrecto.

## Contrato 0.37

- `High/Low`, `Open/Closed/Pedal/Muted` y `China/Crash/Splash/Ride` son identidades audibles.
- Los pitches GM 65 y 66 seleccionan timbal alto y bajo; ya no caen en percusión genérica.
- Una articulación ausente puede usar únicamente una sustitución declarada de su familia.
  La pista conserva los ataques, muestra `(for <identidad original>)` y el estado registra
  `authored`, `deployed` y `reason`.
- `deployed_audible_score` evalúa lo que Live verificó después de cargar dispositivos y crear
  clips; penaliza faltantes, fallbacks y reparaciones necesarias para volver audibles las notas.

## Contrato 0.36

Las pistas de percusión divididas por articulación solo cargan muestras crudas y
tempo-independientes. Archivos con BPM, loops, muestras combinadas y presets `.adv/.adg`
no pueden representar un único golpe GM. Si no existe un golpe semánticamente correcto,
la pista se omite y el despliegue queda explícitamente degradado en lugar de cargar un
sonido engañoso.

El `status.json` informa `duration_floor`, `duration_repairs` e
`inaudible_notes_removed` para cada pista. Las duraciones se adaptan por familia antes de
crear el clip y los ataques nunca se desplazan.

## Contrato 0.35

- Un pitch GM separado exige un sample de esa articulación; un ride ausente no se sustituye por un clap.
- Congas generadas usan 62–64; los pitches 41–50 se identifican honestamente como toms.
- Presets con el mismo nombre cuentan como un único timbre aunque aparezcan en dos rutas del Browser.
- Las palabras de registro del rol (`low`, `upper`, `foundation`, `air`) penalizan presets invertidos.
- Texturas cromáticas usadas como transición obedecen la tonalidad consolidada.
- Canciones de 32 compases o más terminan en la frase de ocho compases más cercana a la duración pedida.

## Contrato 0.34

- Las articulaciones de percusión se separan en pistas reproducibles y conservan su identidad GM.
- Los samples compuestos (`combo`, loops o mezclas completas) no pueden representar una articulación aislada.
- Un open hat nunca selecciona un sample que también contenga kick, snare, clap o tom.
- La exportación mantiene `Live.Clip.MidiNoteSpecification`; un fallback queda registrado explícitamente.
- El mundo sonoro de GPT es vinculante: los instrumentos concretos que nombra deben existir como partes audibles.

## 1. Instalar y encontrar el plugin

Compila e instala `PULSO.vst3`. El script usa por defecto la ubicación oficial de
desarrollo `%LOCALAPPDATA%\Programs\Common\VST3`. En Live abre
`Settings > Plug-Ins`, habilita VST3 y pulsa `Rescan`. El plugin aparece como
`Pulso Audio > PULSO`. Si tu versión de Live no explora esa ruta, selecciona la
carpeta como `VST3 Custom Folder` o instala globalmente en
`C:\Program Files\Common Files\VST3` desde una consola elevada.

Para actualizar una versión existente, cierra Live antes de ejecutar el instalador:
Windows bloquea el DLL mientras el plugin está cargado. El script comprueba ese bloqueo
antes de tocar la instalación anterior.

## 2. Escuchar inmediatamente

1. Crea una pista MIDI.
2. Inserta PULSO.
3. Escribe opcionalmente una dirección creativa.
4. Indica `9:00`, `3:30` u otra duración en `SONG LENGTH`.
5. Pulsa `COMPOSE SONG`.
6. Inicia el transporte.

El sintetizador de `Preview` permite escuchar el patrón sin cargar otro instrumento.
El selector junto a `PREVIEW AUDIO` ofrece `AUTO` y ocho mundos sonoros completos:
`DEEP PROGRESSIVE`, `ORGANIC MOTION`, `ANALOG WARMTH`, `DUB SPACE`, `MINIMAL PULSE`,
`HYPNOTIC NIGHT`, `CINEMATIC ARC` y `DARK CLUB`. No son sólo drum racks: cada opción
reasigna el carácter de todas las voces y su espacio compartido. `AUTO` toma su decisión
de la dirección creativa. La selección se guarda en el Set y no modifica el MIDI.
El cambio tímbrico se oye sobre la composición actual; para obtener la nueva dramaturgia
de silencios y respiraciones hay que pulsar `COMPOSE SONG` o `REGENERATE UNLOCKED`, ya
que esas decisiones forman parte del MIDI y no del preset de escucha.
El indicador distingue una composición GPT validada del motor local.

### Producir con instrumentos nativos de Live

La franja `LIVE SOUND DIRECTOR` muestra el inventario que el puente encontró realmente
en el Browser. GPT selecciona un dispositivo nativo por parte y describe el carácter del
preset sin inventar nombres. Puedes cambiar la elección desde el menú de cada pista.

1. Activa `PulsoDeployRemote` como Control Surface y espera `LIVE SOUNDS INDEXED`.
2. Elige `FULL ORCHESTRATION` o `QUICK 3-STEM`.
3. Pulsa `CREATE IN LIVE`.
4. El puente crea clips editables y carga dispositivos/Racks de uno en uno.

El estado final indica cuántos sonidos cargaron, cuántos usaron fallback y cuáles faltan.
El índice incluye Sounds, Drums, Instruments, Max for Live y User Library; excluye VSTs.
La franja superior muestra la forma completa. Pulsa una sección para seleccionarla;
después arrastra `SECTION` para convertirla en un clip MIDI independiente. `FULL SONG`
exporta la obra completa con una pista por instrumento orquestal que tenga material. Una
canción puede contener entre 12 y 36 partes, aunque nunca tienen que sonar todas a la vez.
Junto al `.mid`, PULSO escribe un archivo `.pulso.json` con `catalog_id`, departamento,
rol, nombre exacto de pista y ruta de rack recomendada para cada parte. Ese manifiesto es
el contrato estable del puente: permite que racks propios, Max for Live o un Remote Script
resuelvan la instrumentación sin inferirla a partir del nombre visible. Si no existe un
rack compatible, el puente informa el faltante; nunca carga silenciosamente otro preset.

El preview integrado sí resuelve esos `catalog_id` inmediatamente. Piano, harp, familias
de cuerdas, maderas, brass, choir, mallets, bajos, sintes, taiko, timpani, percusión latina,
shaker y cymbals tienen modelos DSP y envolventes diferentes aunque compartan canal o rol.
El MIDI estándar continúa siendo neutral: para obtener el mismo timbre dentro de Live hay
que cargar el rack recomendado o conectar una biblioteca propia al mismo `catalog_id`.

## Sonido individual desde cada pista

La columna izquierda de la partitura funciona como un banco de pestañas instrumentales.
Haz clic sobre el nombre de cualquier voz para abrir su menú de sonidos. La fila muestra
el modelo que está escuchándose; `AUTO` sigue la familia y el mundo sonoro actual.
Una elección manual sólo reemplaza esa voz: elegir `808 Body Snare` para
`SNARE / CLAP`, por ejemplo, no cambia kick, hats ni percusiones.

Las quince selecciones son parámetros automatizables y persistentes del host. SOLO,
MUTE, arrastre MIDI y selección tímbrica comparten una fila, pero el audio de preview y
el contenido MIDI continúan siendo capas independientes.

El clic abre un inspector completo por voz:

- `PREVIEW SOUND` escoge o hereda el modelo de síntesis.
- `-12 / 0 / +12` cambia únicamente el registro de escucha de esa pista.
- `LEVEL` ajusta su balance entre -36 y +6 dB con suavizado para evitar zipper noise.
- `AUDITION` reproduce inmediatamente un golpe o nota representativa, incluso con el
  transporte detenido, sin enviar ese evento de prueba a la salida MIDI.

El transpose y el nivel pertenecen al preview: no cambian pitches, velocities ni archivos
MIDI. La fila muestra los overrides activos para revisar la mezcla de un vistazo.

La línea cian sobre el arreglo es el playhead de PULSO. Sigue la posición PPQ de Ableton
y muestra `PLAY` o `PAUSED`, el compás, la sección musical y el tiempo dentro de la obra.
Al hacer seek, detener o reanudar Live, el indicador salta inmediatamente a la posición
correcta. Si el host no ofrece transporte, muestra `PREVIEW` y utiliza el reloj interno.

## 3. Desplegar la orquesta como pistas editables

Instala el Remote Script desde una PowerShell elevada:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/install-ableton-bridge.ps1
```

Reinicia Live y elige `PulsoDeployRemote` en `Settings > Link, Tempo & MIDI > Control
Surface`. Compón una canción y pulsa `CREATE IN LIVE`. El puente crea una pista MIDI y un
clip de Arrangement por cada `partId` poblado. Después resuelve la intención tímbrica
contra presets y dispositivos nativos realmente instalados. Si no encuentra el preset,
prueba el dispositivo elegido y un Rack seguro, siempre informando el fallback.

El intercambio usa JSON atómico en `%LOCALAPPDATA%\PULSO\LiveBridge`. El despliegue crea
notas y velocities editables. Para conservar además CC, aftertouch, bends y metaeventos,
arrastra `FULL SONG`, que continúa siendo la exportación MIDI canónica.

Antes de escribir `request.json`, PULSO ejecuta la puerta de producción. Si el score final
no es tonal, métricamente válido, seguro y expresivamente razonable, `CREATE IN LIVE` no
crea pistas parciales: muestra `PRODUCTION GATE BLOCKED INVALID SCORE`. El contrato schema 5
incluye además el `sound_world` global. Live resuelve primero la identidad exacta o familia
correcta y usa esa paleta compartida para elegir entre alternativas compatibles.

La entrega expresiva se identifica como `native_editable_with_lossless_midi_source`:
las pistas creadas reciben la mejor proyección editable que ofrece la API de Live, mientras
`FULL SONG` conserva la fuente MIDI sin pérdida con CC, pedal, bend y pressure originales.

## 4. Conservar y regenerar capas

1. Genera una idea y escúchala completa.
2. Activa `LOCK MELODY` si te gusta el hook.
3. Activa `LOCK HARMONY` si quieres conservar progresión y voicings.
4. Pulsa `REGENERATE UNLOCKED`: las capas bloqueadas quedan idénticas nota por nota.
5. Usa `UNDO` para volver al resultado completo anterior.

Los locks actúan por familias: `Harmony + FX`, `Melodic`, `Bass` y `Rhythm`. Las quince
voces conservan identidad propia aunque varias compartan el mismo canal MIDI.

## 5. Enviar el MIDI a otro instrumento

### Despliegue nativo en Live

Con `PulsoDeployRemote` activo, el selector de despliegue ofrece dos destinos:

- `FULL ORCHESTRATION` (predeterminado): crea una pista MIDI editable por cada instrumento
  poblado y carga en cada pista el sonido nativo elegido por la IA.
- `QUICK 3-STEM`: crea únicamente `RHYTHM`, `HARMONY` y `MELODY` para una prueba ligera.

`CREATE IN LIVE` reemplaza sólo las pistas creadas por esa instancia del puente; no
duplica el arreglo ni toca pistas del usuario. PULSO conserva su preview interno para
audicionar antes de materializar la producción en Live.

1. Conserva PULSO en la primera pista.
2. Crea una segunda pista MIDI y carga el instrumento deseado.
3. En `MIDI From`, selecciona la pista que contiene PULSO.
4. En el segundo selector de entrada elige la salida del plugin cuando Live la muestre.
5. Pon `Monitor` en `In` o arma la segunda pista.
6. Desactiva `Preview` en PULSO si solo quieres escuchar el instrumento externo.

Para convertir el resultado en un clip, graba la segunda pista. Esta ruta conserva
notas y velocidades como MIDI normal.

## 6. Arrastrar clips MIDI directamente

Cuando la partitura ya contiene notas, mantén pulsada una de las asas inferiores y
arrástrala a una pista o espacio vacío del Arrangement de Live:

- `FULL SONG`: archivo multitrack con conductor, marcadores y una pista por voz.
- `RHYTHM`: batería central y dos percusiones independientes.
- `BASS`: subgrave y bajo de movimiento.
- `HARMONY`: fundamento, pulso y capa superior armónica.
- `LEADS+FX`: lead, contramelodía, atmósfera y transiciones.
- `SECTION`: solamente el bloque seleccionado de la forma.

Las filas de la partitura son también asas: arrastra una fila para exportar esa voz sola.
Los botones `S` y `M` junto a cada fila permiten escucharla en SOLO o MUTE. Son controles
de audición: afectan el MIDI enviado y el preview interno, pero nunca eliminan notas del
archivo que arrastras. Si cambias uno durante playback, PULSO corta con seguridad las notas
anteriores y recupera las notas largas que todavía deban estar sonando.

`HUMAN PERFORMANCE` está apagado por defecto: así la escucha y los archivos arrastrados
quedan exactamente en semicorcheas. Al activarlo, PULSO desplaza sólo la salida reproducida
unos pocos milisegundos según la función de cada voz. Graba esa salida en otra pista si
quieres capturar la interpretación; arrastra desde la partitura si quieres el MIDI limpio.

El clip conserva longitud, tempo, compás, velocidades, articulaciones, marcadores, CC11
de expresión, CC1 de modulación, CC74 de brillo, CC64 de pedal, pitch bend, channel
pressure y poly-aftertouch. PULSO configura por RPN un rango de bend de ±2 semitonos.
El instrumento receptor debe mapear o soportar esos mensajes para oírlos. Si una voz no
existe en el patrón actual, su fila aparece atenuada.

`CREATE IN LIVE` confirma todas las pistas verificadas aunque una identidad aislada no pueda
resolverse. El estado `degraded` enumera únicamente las pistas omitidas. Sólo un error global
de la API de Live o cero pistas audibles provoca rollback total. Percusión multiarticulación
se divide por identidad y usa one-shots de Drum Hits antes de aceptar un Drum Rack opaco.

PULSO no activa MPE implícitamente: Live y muchos instrumentos tratan los canales MPE como
canales miembro y eso entraría en conflicto con las pistas por voz. Los bends expresivos
se aplican solamente a voces monofónicas que ya poseen un canal independiente.

## 7. Variaciones desde Max for Live

El plugin interpreta la nota MIDI 127 del canal 16 como `Regenerate Unlocked`.
No la reenvía ni la usa como información armónica.

`ableton/PulsoBridge.maxpat` es un patch Max que emite ese comando. Para usarlo:

1. Crea un Max MIDI Effect antes de PULSO.
2. Abre el dispositivo en Max.
3. Abre o copia el contenido de `PulsoBridge.maxpat`.
4. Asegúrate de que `pulso_bridge.js` esté en la misma carpeta o en el search path.
5. Guarda el dispositivo como `.amxd` en tu User Library.

El puente es opcional: el botón de la interfaz del VST realiza la misma acción.

## Activar GPT

Ejecuta `scripts/configure-openai.ps1`, cierra Live y vuelve a abrirlo. PULSO lee
`OPENAI_API_KEY` únicamente en el worker de generación. No hay red en el hilo de audio.
Las instrucciones y notas bloqueadas se envían al servicio; el audio no se envía.

La versión 0.19 no traduce la instrucción a uno de cuatro grooves internos. GPT construye
el lenguaje rítmico y sus motivos a partir de la descripción completa. Pedidos explícitos,
como un bombo constante en negras, siguen actuando como restricciones porque forman parte
de la intención del usuario, no porque pertenezcan a una plantilla de género.

Desde 0.20 tampoco existe una progresión armónica global de cuatro grados. GPT escribe una
paleta de acordes y una línea temporal distinta para cada sección, incluyendo inversiones,
bajos pedal, extensiones, acordes prestados y cambios de centro tonal cuando tienen función
narrativa. Las pistas `Harmonic Foundation`, `Harmonic Pulse`, `Harmonic Upper`, los bajos y
los apoyos de melodía reciben el mismo mapa, por lo que exportarlas por separado conserva
una composición armónicamente coordinada.

El archivo `FULL SONG` incluye armadura tonal y marcadores `Chord: ...` en la pista
conductora. Cada nota sigue siendo MIDI estándar y puede editarse normalmente en Live.

## Limitaciones conocidas

- Live no ofrece a un VST acceso general al contenido de todas las pistas.
- El MVP aprende la armonía del MIDI que llega a su propia instancia.
- El canal de batería 10 puede requerir
  remapeo de canal.
## Contrato de publicación 0.32

- `production_mode_source` registra si el dominio vino de GPT, inferencia local o una selección explícita del usuario.
- `electronic_production_audited` indica si se ejecutó la crítica electrónica; el score sólo existe cuando corresponde.
- Sub y Bass Groove no comparten preset. Un instrumento neutro audible es preferible a una identidad falsa.
- Las notas se insertan con `Clip.add_new_notes({"notes": ...})` en Live 11/12; `set_notes` queda únicamente como compatibilidad antigua.
- El MIDI desplegado está cuantizado. Human Performance afecta la escucha de PULSO, no el clip editable.

## Contrato de publicación 0.33

- El Remote Script crea `Live.Clip.MidiNoteSpecification` y entrega esos objetos directamente a `add_new_notes`.
- Si Live rechaza la ruta moderna, `modern_note_error` conserva tipo y mensaje antes del fallback compatible.
- Open hat, shaker, clap, rim, tom, conga, claves, metal y cymbal se separan antes de elegir el sonido.
- Un sample etiquetado como `click layer`, `top layer` o `transient layer` no puede representar el kick completo.
- Stabs electrónicos tienen una duración máxima de un beat y los one-shots conservan sus límites instrumentales.
