# PULSO

### 0.42 — autoría narrativa verificable

- GPT escribe células con `theme_id` y función narrativa explícita; statement, answer,
  development y resolution comparten memoria comprobable a través de la forma.
- Cada nota conserva procedencia (`ai_authored`, `ai_transformed`, `procedural` o
  `local_continuity`) hasta Live y el manifiesto MIDI.
- `NarrativeScoreGate` mide cobertura autoral de voces principales, retorno temático,
  continuidad del bajo, dirección armónica y desarrollo rítmico.
- El crítico compara el render inicial y su revisión; una revisión musicalmente peor ya no
  reemplaza automáticamente un plan superior.
- En canciones GPT, los motores locales validan y realizan la interpretación, pero dejan de
  inventar hooks, variaciones, recuerdos temáticos y desarrollos de bajo posteriormente.

### 0.41 — realización audible vinculante

- `Audible Timbre Gate` resuelve el reparto completo antes de tocar el Set. Kick, sub,
  movement bass y lead deben alcanzar su piso perceptual; un fallo crítico conserva el
  despliegue anterior en vez de publicar una instrumentación engañosa.
- Hats, clap, snare y shaker repetidos pueden usar entre dos y tres muestras exactas distintas.
  Los ataques se distribuyen mediante round-robin determinista sensible a frase y velocity,
  conservando timing, identidad GM y edición MIDI independiente.
- Cada pista publica un release máximo por función. El puente limita únicamente parámetros
  de envelope cuyo tiempo puede verificar en la API de Live y nunca reescribe releases de
  compresores, reverbs o delays como si fueran el instrumento.
- El bajo de movimiento reconoce frases de ocho compases copiadas y desarrolla los dos
  compases finales sin salir de la grilla, chocar con el kick ni cambiar la tonalidad.
- `audible_audit.json` observa los medidores reales mientras Live reproduce: confirma presencia
  durante notas y detecta colas después de la ventana de release. La API Remote Script no entrega
  buffers ni espectro; esa limitación queda declarada explícitamente y nunca se inventa un análisis.

### 0.40 — reparto cerrado, macroforma y percusión con fraseo

- El reparto instrumental explícito de GPT es un contrato cerrado: los normalizadores ya no
  agregan `Upper Air`, `High Percussion` ni instrumentos esenciales genéricos que la IA no escribió.
- Un pase final restablece el registro de cada parte después de continuidad y orquestación; una
  voz superior no puede reaparecer en registro de bajo ni la percusión alta convertirse en kick.
- La macroforma electrónica admite breakdowns largos, pero recupera un ancla de kick antes de
  superar dieciséis compases sin pulso, salvo que GPT declare un silencio completo real.
- Hats, shaker, clap y percusiones conservan seis compases de identidad y desarrollan los dos
  compases finales mediante articulación, respuesta y sustracción, no sólo cambios de velocity.
- El selector de Live evalúa la intención de cada pista sin interpretar frases negadas del rol
  como características del preset, evitando falsos fallbacks de sonidos correctos.

### 0.39 — reparto autoral, armonía vertical y continuidad electrónica

- Una composición electrónica con reparto completo conserva únicamente las voces que GPT
  asignó a instrumentos concretos. Ya no aparecen propietarios orquestales genéricos ni una
  pista de percusión multipropósito agregada por el normalizador.
- `VerticalHarmonyGate` audita lo que realmente suena después de orquestar, cuantizar y
  reparar releases. Elimina choques graves de semitono/tritono mediante respiraciones exactas
  de soporte, sin cambiar la tonalidad ni inventar otras notas.
- Las respuestas del hook se vinculan por instrumento y frase; guitarra, sintetizador, cello
  u otra voz conservan identidades contrapuntísticas independientes en vez de fusionarse.
- Los silencios electrónicos se regulan según energía y función: una pausa completa debe
  declararse; una introducción club sin ritmo recibe continuidad superior escasa, nunca un
  kick automático ni una cama continua.
- Live selecciona cada sonido sólo desde la intención de esa pista, distingue un sub seno
  limpio de un sub saw/complex y publica también la coherencia entre nombre, rol e intención.

### 0.38 — identidad musical y fidelidad tímbrica

- El groove conserva seis compases de ADN reconocible entre frases de ocho y reserva los dos
  últimos para variación, evitando tanto el patrón congelado como el random permanente.
- Las respuestas melódicas se derivan del hook mediante inversión o retrogradación dentro de
  la tonalidad, en lugar de funcionar como melodías independientes sin parentesco.
- Las transiciones sólo pueden vivir alrededor de límites formales y se limitan a dos gestos
  por parte y llegada; una pista etiquetada como transición ya no se convierte en percusión continua.
- La tonalidad consolidada permite la sensible elevada del modo menor únicamente en una
  dominante o transición declarada y con resolución inmediata al centro tonal.
- Los descriptores de sonido (`breathy`, `glassy`, `felt`, `muted`, registro, espacio y
  envolvente) son vinculantes. Live informa `intent_fidelity`, su promedio y un score audible
  que penaliza coincidencias de familia cuyo carácter real no cumple la intención de GPT.

### 0.36 — Audible Production Gate

- Live rechaza loops con BPM, muestras compuestas y presets cromáticos cuando necesita un golpe aislado.
- Cada familia instrumental recibe una duración mínima físicamente audible; los fragmentos irrepresentables se eliminan.
- Una pista latina conserva congas, bongos y timbales, sin convertirse silenciosamente en una colección de toms.
- El foreground rota después de dos frases consecutivas y ninguna reducción club pierde su narrativa por más de ocho compases.
- El crítico GPT recibe las reparaciones audibles como defectos concretos y debe corregirlas en el score siguiente.

### 0.35 — memoria, continuidad y ejecución semántica

La canción publicada conserva ahora memoria temática reconocible, evita breakdowns de ocho
compases sostenidos por una única textura y redondea duraciones largas a la frase de ocho
compases más cercana. El groove mantiene el four-on-the-floor pero incorpora puntuaciones de
frase deterministas y escasas. Congas, toms, rides, claves, hats y metales conservan su
articulación GM exacta; ninguna articulación de soporte puede superar dos tercios de su capa.

El contrato tonal revisa el modelo de reproducción real: una voz de transición cargada como
textura cromática deja de estar exenta. El director de sonidos distingue registros graves y
agudos, evita duplicar presets con el mismo nombre y rechaza una articulación ausente antes de
convertir, por ejemplo, un ride en un clap.

### 0.34.1 — gate de integridad transparente

Los criterios artísticos reparables producen advertencias y afectan la puntuación, pero no
descartan una composición completa. Sólo MIDI corrupto, duraciones inseguras, ownership
inválido o colisiones tonales sin resolver activan el Integrity Gate. Cada rechazo real queda
registrado con su lista exacta de causas en el log del host.

## 0.34 — identidad musical vinculante

PULSO conserva ahora el reparto instrumental creado por GPT en vez de sustituirlo por una
plantilla electrónica fija. Los instrumentos mencionados explícitamente en el mundo sonoro
se convierten en partes reales; el hook recupera un núcleo reconocible durante la forma; la
percusión escrita por la IA se valida contra articulaciones GM; y el gate revisa recurrencia,
diversidad percusiva y materialización tímbrica antes de publicar. La auditoría tonal final se
ejecuta después de cuantización, articulación y releases, sobre el MIDI exacto enviado a Live.

PULSO es una suite de composición MIDI. GPT propone una obra completa
con armonía, melodía, bajo y batería; un motor local valida y reproduce el resultado
sin bloquear el audio. Sin credencial o red, PULSO continúa con un compositor local
y lo identifica explícitamente como fallback.

Este repositorio contiene un MVP funcional para Ableton Live en Windows:

- Plugin VST3 y aplicación standalone construidos con JUCE.
- Generador musical C++20 desacoplado y cubierto por pruebas.
- `Electronic Production Director`: AUTO detecta pedidos de club y cambia el pensamiento
  orquestal por roles de producción, relación kick-bajo, ADN de groove, un único hook
  protagonista, automatización de filtro y arreglo sustractivo. `CLUB ELECTRONIC` permite
  forzarlo; `DEEP HYBRID` y `SYMPHONIC` conservan los otros dominios.
- Orquestación dinámica en dos niveles: quince roles de ejecución alimentan entre 12 y 36
  instrumentos independientes de ritmo, armonía y melodía; kick, clap/snare, hats,
  dos percusiones, dos bajos, tres capas armónicas, lead, contramelodía, atmósfera y transiciones.
- `RhythmPlan` dirigido por GPT con estados de kick, continuidad, swing y gestos estructurales
  como drops, dobles golpes, pickups, silencios y fills.
- Crítico electrónico posterior al render: mide colisiones de low-end, repetición literal,
  correspondencia instrumental con la intención y competencia entre protagonistas.
- Un `CompositionPlan` global con motivo, contorno, secciones, función armónica y tensión.
- Modo `IDEA` para frases y modo canción de 30 segundos a 30 minutos.
- Arquitectura jerárquica con secciones, ADN temático, curva dramática y cadencia final.
- Contrato tonal E2E por beat: cada evento armónico conserva su ventana exacta, los apoyos
  se validan contra el acorde realmente activo y los sustains incompatibles terminan antes
  del cambio, incluso cuando ocurre dentro del compás.
- Dramaturgia de presencia por frase: entradas tardías, diálogo, compases de respiración,
  drops antes de fronteras y densidades que cambian sin perder el hilo temático.
- Modos `Loop` y `Evolve`, con evolución gradual en cada vuelta de la frase.
- Flujo sin perillas: describir, generar, bloquear capas, regenerar, avanzar y deshacer.
- Paleta de audio independiente: kits 808/909/Modern/Organic y sonidos seleccionables
  para bajo, armonía y melodía, sin alterar el MIDI.
- Variaciones reproducibles y estado persistente en el proyecto del DAW.
- Motor de tiempo real: generación en worker, panic MIDI y recuperación de seek/loop.
- Preescucha multitimbral con ocho mundos sonoros, selección automática desde el prompt,
  polifonía rítmica reservada, espacio estéreo y limitador a -0.5 dBFS.
- `Live Native Sound Director`: GPT elige dispositivos nativos e intención tímbrica;
  el puente valida contra el Browser instalado, crea las pistas y carga sonidos reales.
- Ayuda contextual en toda la interfaz: deja el cursor sobre cualquier elemento durante 0.35 s.
- Salida MIDI para grabar o alimentar cualquier instrumento.
- Arrastre directo de la canción, una sección, una familia o una voz individual como `.mid`.
- Puente experimental Max for Live para disparar variaciones.
- Scripts de compilación, validación e instalación.

## Inicio rápido en Windows

Requisitos:

- Windows 10/11 de 64 bits.
- CMake 3.25 o superior.
- Visual Studio 2022 Build Tools con la carga de trabajo C++.
- Git.
- Conexión a Internet durante la primera configuración para descargar JUCE 9.0.0.

Desde PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
```

El script configura el proyecto, compila el plugin y ejecuta las pruebas. El artefacto
queda normalmente en:

```text
build/windows-release/Pulso_artefacts/Release/VST3/PULSO.vst3
```

Para instalarlo en la ubicación VST3 del usuario —no requiere administrador—:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/install-vst3.ps1
```

Cierra Ableton Live y cualquier otro host que tenga PULSO cargado antes de actualizar;
Windows no permite reemplazar un VST3 que está en uso.

El destino predeterminado es `%LOCALAPPDATA%\Programs\Common\VST3`, una ubicación
VST3 oficial orientada a desarrollo. Para una instalación global, ejecuta una consola
elevada y usa `-Destination 'C:\Program Files\Common Files\VST3'`.

Después abre Ableton Live, entra en `Settings > Plug-Ins` y pulsa `Rescan`.

Para activar el despliegue automático, instala además el puente desde una PowerShell
elevada, reinicia Live y selecciónalo como Control Surface:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/install-ableton-bridge.ps1
```

### Activar composición con GPT

PULSO usa `gpt-5.6-terra` mediante OpenAI Responses API y Structured Outputs. La clave
no está incluida en el plugin ni en el repositorio. Configúrala para tu usuario:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/configure-openai.ps1
```

Cierra Ableton por completo y vuelve a abrirlo. El indicador mostrará
`GPT-5.6 TERRA · VALIDATED` tras una composición válida. Si falta la clave, la red
falla o la respuesta no supera la validación, mostrará `LOCAL ENGINE` o
`LOCAL FALLBACK`; nunca presenta el fallback como IA.

La dirección creativa y la duración se envían a OpenAI. Para canciones largas GPT
diseña una partitura estructural compacta. El motor local renderiza un primer MIDI,
calcula métricas de cromatismo, apoyos, sustains y colisiones con ubicaciones exactas, y
entrega ese informe simbólico a la segunda pasada crítica. No se envía audio. El uso de
la API puede tener coste según tu cuenta.

## Primera prueba sin Ableton

El ejecutable `pulso_cli` permite comprobar el motor sin cargar un DAW:

```powershell
./build/windows-release/Release/pulso_cli.exe bass 42 0 4
./build/windows-release/Release/pulso_cli.exe drums 42 0 8
./build/windows-release/Release/pulso_cli.exe counter 42 0 4 2
./build/windows-release/Release/pulso_cli.exe ensemble 42 0 8 1
```

Los argumentos son `rol semilla raíz compases variación evolución`. La utilidad usa una
progresión menor i–VI–iv–V para que se pueda inspeccionar la conducción armónica sin
abrir el plugin.

La aplicación standalone permite probar la interfaz y el sintetizador de preescucha.
Consulta [docs/ABLETON.md](docs/ABLETON.md) para el enrutamiento MIDI completo.

El selector de mundo ofrece `AUTO` y ocho ambientes: `DEEP PROGRESSIVE`, `ORGANIC MOTION`,
`ANALOG WARMTH`, `DUB SPACE`, `MINIMAL PULSE`, `HYPNOTIC NIGHT`, `CINEMATIC ARC` y
`DARK CLUB`. Cada mundo cambia coordinadamente los instrumentos de las quince voces,
drums, filtros, envolventes, estéreo, delay y espacio. `AUTO` interpreta la dirección
creativa. La monitorización nunca altera las notas ni el MIDI exportado.

Cada fila abre su propio inspector sonoro. Desde allí se elige el timbre individual,
octava, nivel y audition de esa voz sin ocupar espacio permanente con selectores globales.
Los cambios son inmediatos, se guardan con el proyecto de Ableton y sólo afectan la preescucha.

Las paletas no son etiquetas sobre el mismo oscilador: alternan familias band-limited,
FM, triángulos orgánicos, capas analógicas, texturas, saturación y articulaciones. Los
osciladores PolyBLEP reducen el aliasing áspero asociado a previews tipo chiptune.

También puedes arrastrar desde la franja inferior de la partitura: `FULL SONG` crea el
archivo multitrack completo; `RHYTHM`, `BASS`, `HARMONY` y `LEADS+FX` crean familias independientes.
Selecciona un bloque de la forma y arrastra `SECTION` para exportar sólo esa parte.
También puedes arrastrar directamente cualquiera de las quince filas. PULSO escribe una
pista MIDI por voz, tempo, compás, armadura tonal, marcadores de sección y acorde, CC
expresivos, nombres, canales y velocidades en el archivo.

## Interpretación MIDI instrumental

GPT asigna a cada voz una identidad de ejecución estructurada: articulación, contorno
dinámico, vibrato, gesto de afinación, profundidad expresiva, brillo, pedal y humanidad.
El motor local convierte esa intención en note lengths, microdinámica, CC11, CC1, CC74,
CC64, pitch bend con rango RPN seguro, channel pressure y poly-aftertouch. Los bends se
limitan a bajos y melodías monofónicas con canal dedicado; acordes y batería nunca se
desafinan por una automatización compartida. Al iniciar playback desde mitad de la obra,
PULSO reconstruye el estado expresivo vigente antes de recuperar notas sostenidas.

## Flujo de composición

- **Creative direction:** una instrucción opcional en lenguaje natural.
- **Song Length:** acepta `9:00`, `9 min` o `IDEA`; el valor predeterminado es `3:30`.
- **Compose Song:** diseña y renderiza la obra completa a la duración indicada.
- **Lock Harmony + FX/Melodic/Bass/Rhythm:** conserva exactamente esa familia de voces.
- **Regenerate Unlocked:** reemplaza únicamente lo que no está bloqueado.
- **Next Idea:** avanza a otra propuesta respetando locks.
- **Undo:** recupera la composición completa anterior.
- **Preview Audio:** activa el sintetizador interno.
- **MIDI Thru:** conserva también el MIDI de entrada.

Durante cada solicitud aparece un indicador animado con el tiempo transcurrido. Los
controles de composición se bloquean temporalmente para impedir peticiones duplicadas,
mientras la idea anterior continúa reproduciéndose sin bloquear el hilo de audio.

## Estructura

```text
src/core/       Motor musical puro, sin dependencias de JUCE
src/plugin/     Adaptador VST3, transporte, interfaz y preescucha
tests/          Pruebas unitarias del comportamiento musical
tools/          Utilidades de diagnóstico
ableton/        Puente experimental Max for Live
scripts/        Construcción, validación e instalación
docs/           Producto, arquitectura, Ableton y desarrollo
```

## Estado del producto

La versión 0.37.0 hace vinculante la identidad acústica que llega a Live: registro alto/bajo,
estado abierto/cerrado/pedal/mute y familia China/Crash/Splash/Ride forman parte del contrato,
no simples palabras de ranking. GM 65/66 se publican como timbales. Si una articulación no
existe en la biblioteca, sólo se permite una sustitución declarada de la misma familia que
conserva exactamente los ataques y deja la identidad original en telemetría. El estado final
incluye un `deployed_audible_score` calculado sobre los dispositivos y clips realmente creados.

La versión 0.33.0 convierte la naturalidad electrónica en contratos verificables: notas de Live 12
mediante `MidiNoteSpecification`, articulaciones de percusión resueltas por identidad, kicks completos
en lugar de capas de click, un único ornamento libre de kick por frase de ocho compases, transformaciones
rítmicas que cambian ataques reales, stabs de hasta un beat, respiración armónica y rotación de soportes
en clímax extensos. La telemetría conserva el error exacto si Live necesita usar compatibilidad antigua.

La versión 0.32.0 completa el contrato de publicación: CLUB conserva prioridad explícita sobre GPT,
HYBRID recibe crítica electrónica real, el score electrónico informa `N/A` cuando no fue auditado,
la percusión repetida evoluciona antes de superar cuatro compases literales y todo MIDI fuente sale
en grilla exacta. Human Performance queda como capa reversible de escucha. El director de sonidos de
Live separa Sub/Bass Groove, evita falsos reemplazos como contrabajo por violín, prioriza kicks cortos
y usa `Clip.add_new_notes` de Live 12 sin caer en la API obsoleta.

La versión 0.31.0 agrega el `Electronic Production Director`, selección AUTO/CLUB/HYBRID/SYMPHONIC,
contrato kick-bajo, arreglo sustractivo, hooks con propietario único y auditoría electrónica.

La versión 0.30.0 integra un arquitecto GPT-5.6 Terra de forma larga con razonamiento alto y una segunda pasada crítica de razonamiento medio,
Structured Outputs, motivos rítmicos abiertos y mutaciones con propósito, renderizado
jerárquico de hasta 512 compases, quince roles compositivos y una plantilla dinámica de
12–36 instrumentos con rotación de foreground, contraste cámara–tutti, divisi y doblajes restringidos, expresión
instrumental dirigida por IA con CC, bends, pressure, aftertouch y pedal, dirección previa de foreground/response/support, presupuestos de ataques,
respiración coordinada por frase, contrato tonal E2E consolidado por defecto (acorde y escala
deben coincidir en cada apoyo estructural), expansión sólo por pedido explícito, dirección rítmica por estados y gestos,
SOLO/MUTE persistente por voz sin modificar el MIDI fuente,
generación cancelable, deadlines de red y fallback automático al primer borrador válido,
transporte WinHTTP nativo en Windows con proxy automático y cancelación inmediata,
un plan narrativo de frases variables, transformación temática semántica, armonía extendida
con conducción de cuatro voces, bajo sub y bajo móvil compuestos independientemente,
crítica simbólica alimentada por una auditoría del primer render, contrato armónico exacto
por beat, reparación vertical iterativa, ataques MIDI exactos con releases expresivos y una
interpretación humana opcional no destructiva,
preview multitimbral band-limited por mundos sonoros,
playhead visual sincronizado con el PPQ de Ableton,
paletas persistentes de sonido individual directamente en las quince filas,
inspectores por voz con octava -12/0/+12, nivel en dB y audition instantáneo,
selección manual por cada parte orquestal con retorno inmediato al instrumento elegido por IA,
interfaz completa español/inglés con cambio persistente dentro del VST y tooltips localizados,
codificación UTF-8 explícita y layout inferior simplificado sin selectores globales redundantes,
timeline de secciones, exportación por voz y fallback algorítmico. La red nunca se usa
desde el callback de audio.

### Interpretación explícita por IA

`performance_score` permite que GPT escriba ataques, duraciones, pitches, velocidades,
silencios y controles MIDI por voz. Para escalar hasta 512 compases, la partitura usa
células de duración libre y placements por sección con repetición, transposición y escala
temporal. La propiedad es local al intervalo exacto de cada placement: una célula breve ya
no puede borrar accidentalmente el resto de una sección. Las voces y los intervalos no
escritos conservan el renderer local como respaldo. Una auditoría de continuidad mide el
silencio global real y, únicamente ante un vacío absoluto mayor de dos compases, conserva
el arco con anclas atmosféricas tonales discretas; las respiraciones normales permanecen intactas.

Cada placement puede trasladar el material a otra voz, fragmentarlo, invertir su contorno,
retrogradarlo y cambiar tiempo, registro y dinámica. GPT decide libremente qué instrumento
establece, responde, transforma, retira, intensifica o resuelve una idea. El crítico exige
relaciones audibles entre familias, no más capas simultáneas ni una plantilla estilística.

PULSO valida límites MIDI, tesituras, duración, duplicados, referencias y forma. La rejilla
es estricta por defecto: sólo conserva timing libre cuando la IA declara explícitamente
`tuplet` o `deliberate_displacement`. Una huella independiente del nombre
detecta células musicalmente idénticas. La segunda pasada crítica recibe además las barras
repetidas, la repetición literal de placements y los silencios accidentales reparados para
reescribir el material débil sin destruir las partes válidas.

### Orquestación profunda

El selector `AUTO ORCHESTRA / DEEP PRODUCTION / SYMPHONIC` define la escala de pensamiento
instrumental antes de componer. GPT asigna a cada parte una función —fundamento, cuerpo,
extensión, contrapunto, color o transición— además de registro, articulación, divisi y
presencia por secciones. El realizador escribe líneas independientes para contrapunto y
color, conserva los materiales estructurales, añade CC11/CC1/CC74 por instrumento y pasa
el MIDI final por un crítico de tesitura, polifonía y claridad grave. `FULL ORCHESTRATION`
conserva cada parte como pista editable independiente al desplegar en Live.

Desde 0.22.1, las partes orquestales poseen modelos sonoros reales en el preview; una
partitura completa no vuelve a emitir pistas legacy duplicadas. El exportador recorta
retriggers ambiguos, garantiza resets MIDI al final, individualiza la expresión y crea
un manifiesto `.pulso.json` para asignar cada `catalog_id` a racks de Ableton o bibliotecas
externas.

Desde 0.26.0, PULSO no aloja instrumentos de terceros. El puente indexa únicamente sonidos
nativos de Live, consume un manifiesto atómico y crea una pista de Arrangement por parte.
La IA elige el dispositivo y describe el carácter del preset; un resolver determinista
elige sólo entre contenido realmente instalado y reporta cualquier fallback o faltante.

Desde 0.29.0, `CREATE IN LIVE` transporta también controles y expresión por cada parte. El
puente conserva las curvas originales y proyecta CC11, CC1, CC74, sustain y pressure sobre
velocidad, duración, release y propiedades expresivas de nota compatibles con Live 12. El
archivo MIDI arrastrable sigue conservando además los mensajes MIDI crudos completos.

Desde 0.30.0, una puerta de producción revisa el MIDI ya orquestado antes de publicarlo:
cero notas externas en tonalidad consolidada, ataques exactos salvo excepción declarada,
duraciones y propiedad de pista seguras, repetición rítmica acotada, claridad de registro,
balance de familias y densidad expresiva. Las curvas CC/pressure se recortan al material
realmente audible y se reducen a puntos significativos por frase. Una `TimbrePalette`
global coordina material, espacio, brillo, calidez y balance acústico/electrónico antes de
resolver los sonidos individuales instalados en Live.

Lee [docs/ROADMAP.md](docs/ROADMAP.md) para las siguientes etapas y
[docs/LICENSING.md](docs/LICENSING.md) antes de distribuir binarios.
