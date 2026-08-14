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
├── SongPlan + catálogo de orquestación
├── PerformanceScore + scheduler de interpretación explícita
└── OrchestrationScore + crítico instrumental posterior al render
```

`pulso_core` no depende de JUCE ni del sistema operativo. Puede reutilizarse en un
servicio neuronal, una aplicación móvil, Max o futuras versiones AU/AAX.

## Director de producción electrónica

`ProductionLanguage` clasifica el dominio como adaptativo, electrónico de club, híbrido u
orquestal y conserva nueve dimensiones continuas: intención electrónica, foco de pista,
interlock de graves, evolución del groove, economía del hook, movimiento de automatización,
utilidad para DJ, restricción espectral y permiso orquestal. No representa una lista cerrada
de géneros.

Cuando el dominio es `ClubElectronic`, `ElectronicProductionDirector` sustituye los defaults
orquestales por quince funciones de producción: kick, backbeat, hats, percusiones, sub, bass
groove, cuerpo armónico, stab, aire superior, hook, respuesta, atmósfera y transiciones. El
director limita la sección a un protagonista melódico, protege el espacio temporal entre kick
y bajo, agrega respiraciones al final de frases y escribe automatización CC74 vinculada a la
energía y tensión. Después vuelve a auditar el resultado que llegará a Live.

`AUTO DIRECTOR` infiere el dominio desde el pedido. Los modos explícitos únicamente fijan el
dominio; GPT continúa diseñando el lenguaje interno, motivos y forma, por lo que `CLUB
ELECTRONIC` no equivale a una plantilla fija de house o techno.

## Tiempo real

El callback de audio:

- No accede a red o disco.
- No espera mutexes.
- No ejecuta el generador ni reserva vectores de contexto o patrón.
- Envía contextos y recibe patrones mediante colas SPSC de capacidad fija.
- Programa note-on y note-off en offsets de muestra calculados desde PPQ/BPM.
- Mantiene un registro acotado de notas generadas activas.

La publicación worker→audio usa una cola SPSC y un mailbox de overflow coalescente. Si
Live suspende `processBlock`, la cola puede llenarse, pero el worker conserva únicamente
el resultado más reciente en el mailbox y termina la operación inmediatamente. El callback
consulta ese slot con `try-lock`: nunca espera al worker ni deja el overlay de composición
activo indefinidamente.

Desde 0.2.1, la generación ocurre exclusivamente en un `jthread` dedicado. El worker puede usar
los contenedores dinámicos del core sin bloquear el callback. Antes de publicar,
convierte el resultado a un bloque de máximo 2048 eventos; el callback solo realiza
copias acotadas. El `shared_ptr` atómico se conserva únicamente para la vista de la UI,
nunca en la ruta de audio.

## Partitura orquestal profunda

`OrchestrationLanguage` separa escala del conjunto, profundidad armónica, actividad
contrapuntística, divisi, contraste de articulación, diálogo entre familias y mezcla
híbrida. Cada `InstrumentAssignment` añade función orquestal, articulación intencional y
número de voces divisi. `OrchestrationScore` realiza esas decisiones después de cerrar la
partitura tonal: distribuye la propiedad del material estructural, compone líneas propias
para contrapunto/color/transición, aplica límites de ejecución y repara acumulaciones en
el registro grave. Finalmente publica expresión MIDI con `partId`, por lo que la
exportación multipista conserva automatización e identidad instrumental.

En stop, seek, loop o sustitución de patrón, el scheduler emite note-off para su ledger
y CC 123 en los canales generativos. Si la reproducción comienza dentro de una nota,
reconstruye el note-on en la primera muestra y conserva su note-off original.

La preescucha reserva 24 voces one-shot para batería y 24 voces tonales priorizadas, de modo que
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

Cada voz añade `previewOctaveNN` y `previewLevelNN`. El primero es una elección discreta
de -12/0/+12 semitonos y el segundo trabaja entre -36 y +6 dB. El pitch se resuelve después
de identificar la pieza GM para que transponer un snare no lo convierta en otro instrumento;
el gain se suaviza durante 20 ms dentro de cada voz activa. Un buzón atómico de audition
inyecta exclusivamente en `previewMidi`, por lo que nunca contamina salida ni exportación.

## Modelo de sincronización

El host proporciona BPM, posición PPQ, estado de reproducción y compás. Cada patrón se
expresa en beats, no en muestras, y está anclado al inicio global de la frase. El
scheduler aplica módulo sobre 1–16 compases, transforma el intervalo del bloque actual
a muestras y añade únicamente los eventos que caen dentro de ese intervalo.

## Tiempo compositivo e interpretación

`PerformanceTiming` separa la partitura de su ejecución. Antes de publicar, cada onset se
normaliza a una rejilla de semicorcheas; note-off y CC usan una rejilla fina de 1/64 para
conservar legato, staccato, releases y respiración. Por eso preview apagado, archivos
arrastrados y sesiones restauradas parten de ataques exactos sin destruir articulación.
Los eventos de `PerformanceScore` conservan en cambio su tiempo explícito mediante
`authoredTiming`, incluyendo tuplets y anticipaciones fuera de esa rejilla. Ese indicador
también se persiste en el estado binario v8, por lo que restaurar un proyecto no recuantiza
la interpretación escrita por la IA.
`HUMAN PERFORMANCE`
no muta ese patrón: el scheduler añade una sola desviación determinista y acotada en
milisegundos. Kick y foundation permanecen exactos; backbeat, hats, percusión, bajo y
melodía reciben perfiles distintos con un swing compartido y sin doble humanización.
Grabar la salida MIDI captura la interpretación; arrastrar el archivo conserva ataques
exactos y duraciones expresivas.

## Compositor simbólico 0.16

`NarrativePlanner` convierte cada sección en frases deterministas de longitud variable
(4–12 compases), con función de establish, question, answer, develop, suspend, arrive o
release. El texto `motif_treatment` escrito por GPT se traduce a fragmentación, secuencia,
inversión, aumentación, desplazamiento o cadencia y alcanza directamente las notas.

`PhraseComposer` realiza lead, contramelodía, sub y movement bass como intérpretes con
memoria independiente. Sus ritmos se derivan de identidad, frase, función y transformación;
el movement bass ya no copia eventos del sub. Los apoyos métricos obedecen la armonía,
mientras los pasos débiles desarrollan el ADN diatónico.

`HarmonyEngine` construye un timeline armónico explícito. Según tensión y dirección
semántica agrega séptimas y colores, mantiene pedales intencionales, reconoce llegadas a
tónica y minimiza movimiento, cruces y separaciones excesivas entre cuatro voces. Foundation,
pulse, upper y atmosphere reciben realizaciones distintas del mismo momento armónico.

`MusicalCritic` analiza el score completo después del render. Mide espacio negativo,
repetición exacta, saltos, rango dinámico, densidad y claridad; repara solapamientos
monofónicos, limita acumulación no rítmica y publica curvas CC11 de frase. `TonalContract`
vuelve a validar el resultado revisado antes de que el worker lo publique.

## Puerta de producción 0.30

`ProductionPolish` opera sobre la representación que realmente recibirá Live, después de
orquestación, transposición de tesitura, articulación y reparación tonal. El orden final es:

1. convergencia tonal sobre partes realizadas;
2. eliminación de propiedad MIDI solapada;
3. expresión por voz y por instrumento;
4. contrato métrico de publicación;
5. nueva convergencia de note-offs sobre la rejilla final;
6. compactación de expresión por frase audible;
7. auditoría inmutable y sello de producción.

La auditoría bloquea publicación únicamente cuando encuentra corrupción objetiva: timing
fuera de contrato, notas o duraciones inválidas, eventos sin una parte existente, cromatismo
no permitido, sustains armónicamente inválidos o colisiones verticales no resueltas. Las
rachas rítmicas, tensiones diatónicas, claridad de registro, balance de familias y densidad
expresiva afectan la puntuación y quedan registradas como advertencias; nunca eliminan una
canción válida. Un ostinato, un ensemble de cámara o una tensión preparada pueden ser una
decisión compositiva deliberada.

La compactación construye intervalos audibles por `partId + VoiceId`. CC1, CC11, CC74,
pedal, pressure y bends que caen durante silencio se eliminan; dentro de cada frase se
conservan inicio, final y cambios perceptualmente significativos, con un máximo de seis
puntos por curva. Los mensajes RPN de configuración permanecen intactos.

`TimbrePalette` precede a `InstrumentAssignment`: describe un mundo global de material,
profundidad, calidez, brillo, definición transiente, balance acústico/electrónico, cohesión
y contraste. El resolver de Live mantiene identidad instrumental como contrato duro y usa
la paleta sólo para desempatar candidatos de la familia correcta.

Las pasadas GPT de arquitectura y crítica son trabajos largos de Responses API. Se inician
con `background: true`, conservan el ID de respuesta y sólo consultan mientras el estado sea
`queued` o `in_progress`. Cancelar propaga `POST /v1/responses/{id}/cancel`; los fallos
transitorios de una consulta se reintentan sin bloquear el callback de audio.

## Localización y superficie de control

`Localization` concentra todos los textos estáticos, estados conocidos, nombres de voz,
paletas, timeline, exportación, progreso y tooltips. El parámetro `language` pertenece a
`AudioProcessorValueTreeState`, por lo que español o inglés se restauran con el proyecto.
El cambio se aplica en el message thread sin reconstruir el processor ni tocar audio.

MSVC compila core, plugin y tests con `/utf-8`. Los literales internacionales se construyen
como `char8_t` y se convierten explícitamente mediante `String::fromUTF8`; los separadores
usan U+00B7. Las pruebas verifican code points reales para impedir regresiones como `Â·`
o `composiciÃ³n`.

Los antiguos ComboBox globales de kit, bajo, armonía y melodía ya no se crean en el editor.
Las quince filas conservan selección individual, octava, nivel y audition. Sus parámetros
legacy permanecen únicamente en el processor para abrir proyectos anteriores sin alterar
IDs ni automatizaciones; no reservan componentes, attachments ni espacio de layout.

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

`PerformanceScore` cambia la frontera de autoridad. GPT puede entregar células con eventos
nota por nota y CC, declarar qué voces posee cada célula y colocarlas a lo largo de cualquier
sección. El renderer procedural se ejecuta como respaldo y luego se elimina únicamente en
las voces poseídas, incluyendo sus silencios deliberados. Los eventos explícitos se insertan
después del fraseo procedural para conservar sus ataques y pasan después por tesitura,
contrato tonal, note-off seguro, expresión y realización orquestal.

Las células admiten duración libre y timing con intención declarada; no están restringidas a ocho o
dieciséis pasos. `strict_grid` es el contrato predeterminado, `tuplet` preserva ratios
ternarios y `deliberate_displacement` reserva el timing libre para anticipaciones o fases
métricas con función estructural. Los placements aportan repetición, transposición, velocidad y time scaling,
lo que mantiene acotado el JSON de obras extensas. La normalización limita memoria, valida
referencias y calcula fingerprints musicales sin incluir el nombre de la célula. El crítico
recibe cantidad de células, notas explícitas y duplicados exactos y puede reescribir las
partes débiles conservando el material válido.
Una segunda llamada actúa como compositor-crítico. Antes de invocarla, el core renderiza
el primer plan y produce un informe compacto con ventanas armónicas, notas cromáticas no
justificadas, apoyos fuera del acorde, sustains inválidos, colisiones verticales, reparaciones
y ubicaciones exactas. GPT revisa linaje, respiración, interlock kick–bass, causalidad formal
y también las causas medidas en el MIDI. Si esa revisión falla, la primera respuesta validada
permanece utilizable. Arquitectura y crítica comparten un deadline total de 210 segundos;
la arquitectura tiene prioridad y la crítica recibe como máximo 50 segundos del remanente.
En Windows, `AiComposer` usa WinHTTP nativo con TLS, configuración automática de proxy y
timeouts por etapa. Un watchdog cierra el request activo desde el botón `CANCEL` o al vencer
el deadline; en las demás plataformas se usa `WebInputStream::cancel`. La operación es
transaccional: cancelar conserva patrón, seed y variante anteriores. El destructor solicita
la misma cancelación antes de unir el worker, por lo que cerrar el dispositivo no espera el
deadline completo de la red.
El parser rechaza longitud incorrecta, capas vacías, valores no finitos, pitches,
velocidades o tiempos inválidos. Los locks se aplican nuevamente después de validar,
por lo que una capa bloqueada nunca depende de que el modelo obedezca la instrucción.

`TonalContract` es la última autoridad musical antes de publicar un patrón. Consume una
secuencia de `HarmonicWindow` con inicio y final en beats, raíz, bajo, colección sonora,
función, voicing y tensión. Los acordes prestados declarados son legales; el cromatismo no
declarado sólo sobrevive como paso breve preparado y resuelto. Los apoyos se comparan con
el acorde activo, no con la unión de todo el compás. Las notas sostenidas se cortan en la
frontera exacta si dejan de ser comunes. La reparación vertical itera, protege bajo y voces
estructurales, retunea o elimina la voz secundaria y sólo conserva semitonos/tritonos como
cluster o color cuartal explícito, tenso y en registro superior. Una auditoría final exige
cero conflictos no intencionales. Todo ocurre en el worker, nunca en el callback de audio.

La auditoría de publicación corre después de aplicar articulación y microdinámica a las
notas. Si legato, sustain o swelling crean una cola conflictiva, se recorta la nota anterior
sin cambiar la intención tonal. Una segunda reconstrucción no destructiva vuelve a emitir
CC, bends, pressure y poly-aftertouch contra las notas definitivas; así ningún evento por
nota conserva un pitch o una duración anterior a la reparación.

El exportador escribe además armadura tonal y marcadores de acorde en el conductor MIDI.
Esto no obliga a Ableton a interpretar la armonía, pero mantiene el contexto legible para
DAWs y herramientas que soportan esos metaeventos.

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

La partitura separa dos conceptos. Las quince `VoiceId` son roles de ejecución estables
—pulso, bajos, tejido armónico, hablantes melódicos y transición—; no representan una
plantilla fija. Sobre ellos, `OrchestrationScore` instancia entre 12 y 36 instrumentos del
catálogo y los organiza en tres departamentos: ritmo/percusión, armonía/tejido y melodía.
Cada instancia tiene nombre de pista, rol, registro, actividad, prominencia, probabilidad
de doblaje y secciones opcionales.

El director usa energía y densidad formal junto con `ensembleScale`, `timbralMotion`,
`foregroundRotation`, `registerSeparation`, `chamberContrast`, `doublingRestraint` y
`tuttiRarity`. Así reduce la plantilla en pasajes de cámara, rota el instrumento que expone
el motivo, reparte las voces de acordes por registro y reserva doblajes y tutti para
llegadas raras. Las notas conservan su rol causal mediante `VoiceId` y reciben además un
`partId`; la exportación Full Song crea una pista MIDI por parte realmente poblada.
El exportador no permite fallback legacy dentro de una partitura orquestal, recorta
retriggers ambiguos por parte/canal/pitch, limita sustains extremos y termina cada pista
con pedal, modulación, pitch, pressure y All Notes Off en estado neutral. Un manifiesto
`.pulso.json` acompaña al MIDI y conserva `catalogId` para resolver racks externos.

Para la escucha interna, el scheduler duplica sólo los note-on/off hacia un bus privado y
los precede con CC119 reservado que selecciona `InstrumentSoundModel`. Ese selector nunca
sale por el puerto MIDI del plugin. `PreviewSynth` captura el modelo al crear cada voz, por
lo que varias instancias que comparten `VoiceId` pueden coexistir con fuentes y envolventes
diferentes sin búsquedas, locks ni asignaciones en el callback.

## Intérprete expresivo 0.18

Cada `PlannedVoice` contiene un `PerformanceProfile` escrito por GPT mediante Structured
Outputs: articulación, contorno dinámico, vibrato, gesto de pitch, profundidad, brillo,
humanización, pedal e intención semántica. `PerformanceExpression` no acepta una lista
arbitraria de eventos del modelo; traduce esos conceptos a MIDI validado y reproducible.

El renderer modifica duraciones y microdinámica sin mover ataques, dibuja curvas CC11/1/74,
usa CC64 sólo en voces sostenidas y publica pitch bend, channel pressure y poly-aftertouch.
Los pitch gestures se restringen a lead, contrapunto, sub y movement bass, todos con canal
dedicado, y un RPN fija ±2 semitonos. Rhythm y armonía polifónica permanecen pitch-stable.
Cada final de sección resetea bend, pressure, modulación y pedal; panic hace lo mismo en los
16 canales. Un seek o cambio de patrón reconstruye en sample cero el último estado expresivo
anterior al playhead antes de reactivar notas solapadas.

`Pattern::expressions` separa mensajes de 14 bits y pressure de los CC de 7 bits. Scheduler,
preview, locks, slicing, exportación multitrack y estado binario versión 3 conservan ambos.
El preview interpreta bend, sustain, pressure y aftertouch además de CC11/1/74.

Desde 0.29, `PerformancePlacement` incorpora `voiceMap`, propósito narrativo, fragmento,
retrogradación e inversión de contorno. La misma célula puede establecerse en una familia y
ser contestada o transformada por otra sin copiar notas a unísono. El schema describe las
operaciones disponibles pero no prescribe género, progresión ni combinación instrumental.
La auditoría informa mappings, transformaciones y número de voces en diálogo a la segunda
pasada GPT.

Los locks y filtros de exportación operan sobre identidad y familia de voz, no sólo
sobre canal MIDI. Por eso dos capas que comparten canal siguen siendo pistas separadas
en el archivo multitrack y pueden conservarse o arrastrarse de forma independiente.

## Live Native Sound Director 0.29

PULSO no aloja otros instrumentos dentro de su proceso. `InstrumentAssignment` declara
un dispositivo nativo y una intención de preset; `LiveDeployer` publica el contrato JSON
y `PulsoDeployRemote` lo resuelve contra un inventario incremental del Browser de Live.
El índice excluye explícitamente `Plug-Ins`. La carga se serializa por pista para mantener
responsiva la interfaz y el estado distingue coincidencias, fallbacks y faltantes.
Consulta `docs/SOUND_STAGE.md` para el protocolo completo.

`LiveDeployer` schema 5 publica CC y eventos expresivos junto a cada parte, la paleta tímbrica
global y el resultado de la puerta de producción. El Remote Script
los convierte en propiedades extendidas de nota de Live 12 cuando están disponibles y en
una proyección portable de velocidad, release y sustain en hosts legacy. El MIDI multitrack
continúa siendo la representación cruda sin pérdida para curvas de pitch y pressure.

## Dirección rítmica

Desde 0.19 no existe una enumeración de estilos rítmicos ni plantillas `organic`, `deep`,
`driving` o `hybrid`. GPT escribe un `RhythmLanguage` abierto: descripción musical,
estabilidad del pulso, gravedad de backbeat, síncopa, ghosts, contraste dinámico, libertad
temporal, movimiento de orquestación, silencio y llamada/respuesta. También compone entre
dos y seis motivos y puede orquestar articulaciones GM adicionales (kicks alternativos,
sidestick, toms, ride, crash, shaker, tambourine, cowbell y congas).

El renderer no elige un género: convierte esa partitura en eventos, aplica desarrollo
seccional y valida solamente seguridad, rango e invariantes pedidos expresamente. El
fallback sin red es reproducible, pero deriva sus motivos de la dirección completa y la
semilla; no selecciona una familia estilística cerrada.

## Lenguaje armónico con política tonal 0.23.1

`HarmonicLanguage` reemplaza la progresión global de grados. GPT define gravedad tonal,
movilidad modal, cromatismo estructural, riqueza de extensiones, movimiento de inversiones,
suavidad de voice leading, actividad del ritmo armónico, afinidad por pedales, ambigüedad y
fuerza cadencial. Son dimensiones continuas, no presets ni nombres de género. Sobre ellas
actúa una política independiente: `consolidated` por defecto, `expanded` sólo ante una
petición explícita de recursos como intercambio modal o dominante secundaria, y `free`
únicamente para atonalidad, disonancia deliberada, serialismo o politonalidad solicitados.

La `chordPalette` contiene hasta 24 identidades con raíz percibida, bajo independiente,
pitch classes explícitas, función, tensión y estrategia de voicing (`close`, `open`,
`drop_2`, `quartal`, `cluster`, `shell` o `mixed`). Esto admite inversiones, slash chords,
extensiones, omisiones, intercambio modal, dominantes secundarios, mediantes cromáticas,
pedales y estructuras no terciales sin codificar progresiones concretas en el motor.

Cada sección declara centro tonal, indicación modal y hasta 64 eventos armónicos con compás
y beat exactos. `HarmonyEngine` los convierte en una línea temporal, conduce hasta cuatro
voces preservando memoria entre secciones y distribuye el resultado entre foundation,
pulse, upper y atmosphere. `PhraseComposer` consulta esa misma línea temporal para el bajo
y las notas estructurales de melodía. En `consolidated`, `TonalContract` exige la intersección
entre acorde y escala en todo apoyo estructural: un acorde declarado por GPT no puede
autorizar por sí solo una nota externa. Desde 0.30, `consolidated` no conserva tampoco
cromatismos de paso: la garantía es literalmente cero pitch classes externas. Las políticas más
amplias conservan color sólo dentro del límite solicitado y la auditoría final se ejecuta
después de articulación, releases y orquestación.

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

`PerformanceScore` superpone la escritura note-level de GPT sin apropiarse de una sección
completa: cada placement crea spans de propiedad por voz y por intervalo. Sólo los eventos
procedurales que intersectan esos spans son sustituidos. El auditor de continuidad calcula
el mayor vacío global antes y después del refinamiento; un vacío absoluto superior a cuatro
compases recibe anclas tonales de baja intensidad en una parte orquestal exportable y se
informa a la pasada crítica para que la siguiente revisión lo resuelva compositivamente.
Repeticiones de placements, runs literales y barras renderizadas repetidas también forman
parte de esa crítica. Como última defensa, el crítico rompe una tercera copia literal de
percusión mediante espacio negativo determinista; nunca altera el kick estructural exigido
por el contrato.

La realización orquestal sólo admite desplazamientos de octava exactos y el contrato tonal
vuelve a ejecutarse sobre las notas de las partes finales. En Live, la carga serializada de
dispositivos conserva índices de staging y vuelve a adquirir el objeto `Track` después de cada
operación asíncrona del Browser; los handles transitorios inválidos se reintentan antes de
rechazar atómicamente el despliegue.
