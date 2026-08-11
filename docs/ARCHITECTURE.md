# Arquitectura

## Capas

```text
Host VST3 / Standalone
        │
        ▼
PulsoAudioProcessor
├── transporte y scheduling sample-accurate
├── captura de contexto MIDI
├── colas SPSC de memoria fija
├── worker de generación dedicado
├── cliente Responses API + validación estructurada
└── sintetizador de preescucha
        │
        ▼
pulso_core
├── MusicTypes
├── Scale
├── Random determinista
├── CompositionPlan + intérpretes por rol
└── SongPlan + catálogo de orquestación
```

`pulso_core` no depende de JUCE ni del sistema operativo. Puede reutilizarse en un
servicio neuronal, una aplicación móvil, Max o futuras versiones AU/AAX.

## Tiempo real

El callback de audio:

- No accede a red o disco.
- No espera mutexes.
- No ejecuta el generador ni reserva vectores de contexto o patrón.
- Envía contextos y recibe patrones mediante colas SPSC de capacidad fija.
- Programa note-on y note-off en offsets de muestra calculados desde PPQ/BPM.
- Mantiene un registro acotado de notas generadas activas.

Desde 0.2.1, la generación ocurre exclusivamente en un `jthread` dedicado. El worker puede usar
los contenedores dinámicos del core sin bloquear el callback. Antes de publicar,
convierte el resultado a un bloque de máximo 2048 eventos; el callback solo realiza
copias acotadas. El `shared_ptr` atómico se conserva únicamente para la vista de la UI,
nunca en la ruta de audio.

En stop, seek, loop o sustitución de patrón, el scheduler emite note-off para su ledger
y CC 123 en los canales generativos. Si la reproducción comienza dentro de una nota,
reconstruye el note-on en la primera muestra y conserva su note-off original.

La preescucha reserva 24 voces one-shot para batería y 48 voces tonales, de modo que
los acordes densos no roben el pulso. Los canales de las quince voces se traducen a nueve
motores: sub, bajo móvil, foundation, pulse, upper, lead, counter, atmosphere y
transitions. El canal GM 10 sintetiza kick, snare, clap, hats, toms, percusión y cymbals.

Ocho perfiles coordinan brillo, calidez, saturación, decay, anchura, peso rítmico, delay
y room. El modo `AUTO` clasifica la dirección creativa fuera del callback y publica sólo
un índice atómico al audio. El DSP utiliza bancos de voces y buffers preasignados: no
reserva memoria ni toma locks durante `processBlock`. La salida usa ganancia suavizada y
limitador a -0.5 dBFS; las colas espaciales decaen de forma controlada al detenerse.
Cada mundo selecciona además una familia oscilatoria propia —orgánica, analógica, FM,
minimal, cinematográfica o saturada— con saw y square PolyBLEP para reducir aliasing.

Desde 0.13 la identidad instrumental es una capa ortogonal al mundo: `DrumKit`,
`BassTone`, `HarmonyTone` y `MelodyTone` seleccionan modelos DSP independientes. El mundo
conserva el ambiente compartido —brillo, drive, estéreo y espacio—, mientras la paleta
elige la fuente. Los kits 808 y 909 difieren en barrido, cuerpo, transiente, ruido y
envolvente; los grupos tonales difieren en osciladores, modulación, filtro y ADSR. Todos
son parámetros persistentes del host y nunca mutan el patrón MIDI.

`previewVoice00`–`previewVoice14` forman la capa de override por voz. Cero hereda la
familia y uno a cuatro selecciona un modelo específico. El procesador conserva punteros
atómicos pre-resueltos a esos parámetros; `processBlock` copia índices enteros al preview
sin búsquedas, asignación ni locks. `PreviewSynth` resuelve kit o modelo por `VoiceId`, de
modo que snare, closed hat y open hat pueden pertenecer a máquinas diferentes durante la
misma escucha. La interfaz escribe mediante gestos de parámetro normales del host.

## Modelo de sincronización

El host proporciona BPM, posición PPQ, estado de reproducción y compás. Cada patrón se
expresa en beats, no en muestras, y está anclado al inicio global de la frase. El
scheduler aplica módulo sobre 1–16 compases, transforma el intervalo del bloque actual
a muestras y añade únicamente los eventos que caen dentro de ese intervalo.

## Tiempo compositivo e interpretación

`PerformanceTiming` separa la partitura de su ejecución. Antes de publicar, todo onset,
note-off y CC se normaliza a una rejilla de semicorcheas; por eso preview apagado, archivos
arrastrados y sesiones restauradas parten siempre de tiempo exacto. `HUMAN PERFORMANCE`
no muta ese patrón: el scheduler añade una sola desviación determinista y acotada en
milisegundos. Kick y foundation permanecen exactos; backbeat, hats, percusión, bajo y
melodía reciben perfiles distintos con un swing compartido y sin doble humanización.
Grabar la salida MIDI captura la interpretación; arrastrar el archivo conserva la rejilla.

## Gramática de composición

El processor conserva una línea armónica por posiciones de compás. El core recibe esa
línea junto con una semilla estable y genera en cuatro niveles:

1. ADN rítmico y contorno interválico independiente del rol;
2. funciones de statement, answer, development y cadence por sección;
3. realizaciones coordinadas de bajo, batería y contramelodía;
4. microtiming, dinámica y función final de retorno.

`variationIndex` identifica regeneraciones de una idea; `compositionSeed` identifica
su familia. Los locks por canal determinan qué material se conserva exactamente.

`repetition` controla cuánto sobrevive del motivo; `complexity`, su detalle local;
`development`, la diferencia entre exposición y cierre. `evolutionStep` cambia las
decisiones secundarias sin reemplazar la semilla del motivo. Las pruebas del core
verifican duración, tonos de acorde en apoyos, movimiento melódico acotado, cadencias y
porcentaje mínimo de identidad entre evoluciones.

Si no existe transporte —por ejemplo, en standalone— se usa un reloj interno de 120
BPM para poder probar el resultado.

## Estado

Los parámetros pertenecen a `AudioProcessorValueTreeState`. JUCE serializa ese árbol
en el estado del plugin. También se persisten `compositionSeed` y `variationIndex`,
de modo que ADN y linaje vuelven exactamente al abrir el proyecto. La armonía se reconstruye desde MIDI
entrante durante la reproducción.

## Composición con GPT

`AiComposer` llama a Responses API exclusivamente desde el worker. Structured Outputs
produce un plan jerárquico con título, tonalidad, intención, voces, funciones, registros,
interacciones y presencia por sección. No se aceptan identificadores de voz libres: el
esquema limita la respuesta al catálogo conocido por el renderer.
Una segunda llamada actúa como compositor-crítico: compara alternativas de desarrollo,
revisa linaje, respiración, interlock kick–bass y causalidad formal, y devuelve un plan
completo revisado. Si esa revisión falla, la primera respuesta validada permanece utilizable.
Arquitectura y crítica comparten un deadline total de 65 segundos. La arquitectura tiene
prioridad y la crítica opcional recibe como máximo 10 segundos del tiempo restante.
`WebInputStream::cancel`
interrumpe conexión o lectura desde un watchdog y desde el botón `CANCEL`. La operación es
transaccional: cancelar conserva patrón, seed y variante anteriores. El destructor solicita
la misma cancelación antes de unir el worker, por lo que cerrar el dispositivo no espera el
deadline completo de la red.
El parser rechaza longitud incorrecta, capas vacías, valores no finitos, pitches,
velocidades o tiempos inválidos. Los locks se aplican nuevamente después de validar,
por lo que una capa bloqueada nunca depende de que el modelo obedezca la instrucción.

`TonalContract` es la última autoridad musical antes de publicar un patrón. Deriva el
nombre de tonalidad de raíz y modo, ajusta el motivo a la escala, exige tonos del acorde
en apoyos estructurales y conserva cromatismo únicamente como paso breve preparado y
resuelto. Después evalúa las voces simultáneamente, desplaza colisiones ásperas de menor
prioridad y termina bajos o armonías incompatibles antes del cambio de acorde. Esta
pasada es determinista y se ejecuta en el worker, nunca en el callback de audio.

Ante ausencia de credencial, error HTTP, timeout o composición inválida se ejecuta el
motor local. El callback continúa reproduciendo la última idea durante toda la llamada.

## Canciones completas

`AiComposer::planSong` solicita una arquitectura compacta: motivo global, progresión,
tonalidad y secciones con función, energía, tensión y tratamiento temático. Nunca pide a
GPT miles de eventos MIDI en una sola respuesta. `SongComposer` normaliza la duración,
garantiza cobertura continua y desarrolla el plan en bloques de hasta 16 compases que
comparten el mismo ADN. Los snapshots largos se transfieren al audio mediante referencias
ligeras; su memoria se retira en el worker y no en el callback.

## Orquestación dinámica

El catálogo tiene quince voces agrupadas en cinco familias: ritmo (`Kick`, `Snare / Clap`,
`Closed Hats`, `Open Hats / Shaker`, `Low Percussion`, `High Percussion`), bajo (`Sub Bass`,
`Movement Bass`), armonía (`Harmonic
Foundation`, `Harmonic Pulse`, `Harmonic Upper`), melodía (`Lead`, `Countermelody`) y
textura (`Atmosphere`, `Transitions`). Cada sección contiene un conjunto explícito de
voces activas. `SongComposer` normaliza ese conjunto, mantiene registros musicales,
evita colisiones principales y genera CC 11/1/74 para dinámica, tensión y transición.

Los locks y filtros de exportación operan sobre identidad y familia de voz, no sólo
sobre canal MIDI. Por eso dos capas que comparten canal siguen siendo pistas separadas
en el archivo multitrack y pueden conservarse o arrastrarse de forma independiente.

## Dirección rítmica

`RhythmPlan` separa identidad, estructura y gesto. `RhythmMotif` conserva celdas de 1–4
compases en seis máscaras independientes (`0` silencio, `1` golpe, `2` acento), y cada
sección puede desarrollarlas con add/remove/shift/ratchet/velocity y un propósito textual.
Cada sección declara además el estado del
kick, su continuidad, densidad de percusión, síncopa y swing. GPT puede escribir drops,
dobles golpes, pickups, mutes y fills con compás y beat exactos; el renderer desarrolla
el resto mediante ciclos deterministas de acento y microtiming específico por instrumento.

`RhythmEngine` reemplaza la batería genérica del ensemble, coordina los ataques del bajo
con el kick y ejecuta una validación final. En zonas `four_on_floor + required` restituye
cualquier negra perdida, salvo una excepción explícita. Un mute elimina kicks residuales
y un pickup sólo puede aparecer en el último cuarto del compás. Como cada instrumento
tiene su propio `VoiceId`, hats y claps nunca consumen el presupuesto del kick y el
exportador genera pistas MIDI independientes aunque compartan el canal GM 10.

SOLO/MUTE es una capa de audición atómica en el processor. Filtra scheduling y preview,
envía panic/retrigger al cambiar durante playback y se persiste con la sesión. No altera
el `Pattern`, los locks ni los archivos arrastrados/exportados.

## Respiración y dramaturgia

Antes del render, `PhraseDirector` crea una partitura de atención con foreground,
response, support, texture, accent, presupuestos de ataques, ventanas de entrada/salida,
respiraciones y ritmo armónico de 1–4 compases. `SongComposer` sólo puede escribir dentro
de ese contrato. Lead y contrapunto se responden; sub y movement bass alternan;
pulse, upper y atmosphere trabajan a escalas temporales distintas; rhythm deja ventanas
antes de límites de ocho compases. La energía amplía la presencia, pero ni siquiera el
clímax obliga a todas las voces a atacar permanentemente.

Las melodías se escriben después como frases con posiciones, duraciones y silencios
explícitos; ya no proceden del stream continuo del ensemble. Una segunda pasada introduce
microtiming correlacionado por función, arcos de velocidad
y pequeñas variaciones de duración. Las decisiones proceden de ADN, sección y posición,
por lo que son naturales pero reproducibles, no jitter aleatorio.
