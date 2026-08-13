# PULSO

PULSO es una suite de composición MIDI. GPT propone una obra completa
con armonía, melodía, bajo y batería; un motor local valida y reproduce el resultado
sin bloquear el audio. Sin credencial o red, PULSO continúa con un compositor local
y lo identifica explícitamente como fallback.

Este repositorio contiene un MVP funcional para Ableton Live en Windows:

- Plugin VST3 y aplicación standalone construidos con JUCE.
- Generador musical C++20 desacoplado y cubierto por pruebas.
- Orquestación dinámica en dos niveles: quince roles de ejecución alimentan entre 12 y 36
  instrumentos independientes de ritmo, armonía y melodía; kick, clap/snare, hats,
  dos percusiones, dos bajos, tres capas armónicas, lead, contramelodía, atmósfera y transiciones.
- `RhythmPlan` dirigido por GPT con estados de kick, continuidad, swing y gestos estructurales
  como drops, dobles golpes, pickups, silencios y fills.
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

La versión 0.25.0 integra un arquitecto GPT de forma larga con razonamiento medio y una segunda pasada crítica,
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

Lee [docs/ROADMAP.md](docs/ROADMAP.md) para las siguientes etapas y
[docs/LICENSING.md](docs/LICENSING.md) antes de distribuir binarios.
