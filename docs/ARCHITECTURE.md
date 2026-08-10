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
los acordes densos no roben el pulso. Los canales de las doce voces se traducen a nueve
motores: sub, bajo móvil, foundation, pulse, upper, lead, counter, atmosphere y
transitions. El canal GM 10 sintetiza kick, snare, clap, hats, toms, percusión y cymbals.

Ocho perfiles coordinan brillo, calidez, saturación, decay, anchura, peso rítmico, delay
y room. El modo `AUTO` clasifica la dirección creativa fuera del callback y publica sólo
un índice atómico al audio. El DSP utiliza bancos de voces y buffers preasignados: no
reserva memoria ni toma locks durante `processBlock`. La salida usa ganancia suavizada y
limitador a -0.5 dBFS; las colas espaciales decaen de forma controlada al detenerse.
Cada mundo selecciona además una familia oscilatoria propia —orgánica, analógica, FM,
minimal, cinematográfica o saturada— con saw y square PolyBLEP para reducir aliasing.

## Modelo de sincronización

El host proporciona BPM, posición PPQ, estado de reproducción y compás. Cada patrón se
expresa en beats, no en muestras, y está anclado al inicio global de la frase. El
scheduler aplica módulo sobre 1–16 compases, transforma el intervalo del bloque actual
a muestras y añade únicamente los eventos que caen dentro de ese intervalo.

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
El parser rechaza longitud incorrecta, capas vacías, valores no finitos, pitches,
velocidades o tiempos inválidos. Los locks se aplican nuevamente después de validar,
por lo que una capa bloqueada nunca depende de que el modelo obedezca la instrucción.

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

El catálogo tiene doce voces agrupadas en cinco familias: ritmo (`Core Drums`, `Low
Percussion`, `High Percussion`), bajo (`Sub Bass`, `Movement Bass`), armonía (`Harmonic
Foundation`, `Harmonic Pulse`, `Harmonic Upper`), melodía (`Lead`, `Countermelody`) y
textura (`Atmosphere`, `Transitions`). Cada sección contiene un conjunto explícito de
voces activas. `SongComposer` normaliza ese conjunto, mantiene registros musicales,
evita colisiones principales y genera CC 11/1/74 para dinámica, tensión y transición.

Los locks y filtros de exportación operan sobre identidad y familia de voz, no sólo
sobre canal MIDI. Por eso dos capas que comparten canal siguen siendo pistas separadas
en el archivo multitrack y pueden conservarse o arrastrarse de forma independiente.

## Respiración y dramaturgia

Después del desarrollo temático, `SongComposer` aplica una matriz determinista de
presencia por voz y frase. Lead y contrapunto se responden; sub y movement bass alternan;
pulse, upper y atmosphere trabajan a escalas temporales distintas; rhythm deja ventanas
antes de límites de ocho compases. La energía amplía la presencia, pero ni siquiera el
clímax obliga a todas las voces a atacar permanentemente.

Una segunda pasada introduce microtiming correlacionado por función, arcos de velocidad
y pequeñas variaciones de duración. Las decisiones proceden de ADN, sección y posición,
por lo que son naturales pero reproducibles, no jitter aleatorio.
