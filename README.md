# PULSO

## Web comercial

La aplicación de producto, suscripciones y descargas vive en [`web/`](web/README.md). Usa React para la interfaz y Rust/Axum para cuentas, Stripe y GitHub Releases. Cada tag `vX.Y.Z` puede construir y publicar el instalador correspondiente mediante [Release PULSO](.github/workflows/release.yml); la web consulta esa release como fuente única de versión.

### 0.58.33 — cierre verificable para toda protagonista poblada

- La reparación transaccional de coda se ejecuta ahora también cuando Terra escribió una protagonista completa pero omitió únicamente su cierre terminal.
- Después de agotar la recuperación remota, PULSO reutiliza el mejor fragmento protagonista ya escrito por la IA, verifica la resolución y vuelve a clasificar el score completo antes de decidir la publicación.
- El flujo ya no limita esta garantía al caso excepcional de una protagonista originalmente vacía y promovida desde otra voz.
- Si la reparación no supera la auditoría independiente, se conserva íntegramente el score anterior y el registro informa el fallo real.

### 0.58.32 — coda protagonista transaccional y verificable

- La recuperación de una protagonista vacía extrae un fragmento acotado de tres ataques escritos por la IA, conserva su ritmo y contorno, y lo transforma como retorno terminal sin inventar notas.
- Las celdas fuente más largas que la sección de resolución ya no quedan truncadas antes del ataque final: el fragmento se rebasa y escala dentro de la ventana de coda.
- Una reparación de coda solo informa éxito después de pasar la misma auditoría independiente utilizada por el gate de publicación.
- Si el ataque final, la armonía estable o la ventana terminal no quedan satisfechos, la operación revierte íntegramente el score en lugar de dejar una reparación parcial o engañosa.

### 0.58.31 — respiración estructural sin vacíos accidentales

- El auditor de continuidad permite una ventana aislada de cuatro compases como breakdown, umbral o respiración de frase en una obra larga.
- Siguen rechazándose ventanas silenciosas consecutivas, silencios que ocupen una proporción material de la forma, pisos armónicos insuficientes y huecos individuales mayores al margen musical permitido.
- La densidad, el piso armónico, el porcentaje de cobertura y la duración real del silencio continúan midiéndose exclusivamente sobre las notas escritas por la IA.
- El registro informa ahora también la racha máxima de ventanas silenciosas para distinguir una pausa deliberada de una composición abandonada.

### 0.58.30 — la crítica musical no descarta una obra completa

- Una protagonista poblada, resuelta y musicalmente válida ya no bloquea la publicación por quedar apenas debajo de una meta de presencia, desarrollo o fraseo.
- La repetición temática literal y el desarrollo melódico insuficiente continúan generando reparaciones focalizadas y diagnósticos editoriales, pero no borran una composición completa cuando esas reparaciones no convergen.
- Permanecen bloqueantes las invariantes reales: identidad esencial ausente, instrumento concreto solicitado y vacío, coda protagonista ausente, duplicación MIDI independiente grave, discontinuidad global e integridad MIDI inválida.

### 0.58.29 — compromisos explícitos y promoción sin clones

- El inventario técnico de reproducción de Ableton queda separado del prompt humano: una identidad instalada ya no se interpreta como un instrumento solicitado por el usuario.
- Solamente los instrumentos concretos escritos antes del contexto de ejecución se convierten en compromisos duros de publicación.
- Cuando la protagonista declarada llega vacía, una interpretación melódica AI existente se transfiere a esa identidad; la pista de origen deja de conservar una copia MIDI redundante.
- La transferencia preserva notas, fraseo, placements y autoría de la IA, y evita que la propia recuperación active después el detector de duplicación independiente.

### 0.58.28 — continuidad orquestal sin una protagonista permanente

- La protagonista puede respirar entre frases: su presencia narrativa se separa de la continuidad del conjunto y conserva exigencias independientes de desarrollo, diálogo y resolución.
- Un nuevo control audita exclusivamente el MIDI escrito por la IA en ventanas de cuatro compases. Rechaza silencios accidentales, falta de piso armónico o densidad insuficiente sin ocultarlos mediante relleno procedural.
- Las secciones declaradas explícitamente como silencio total se respetan como decisiones dramáticas y quedan fuera de la medición de huecos accidentales.
- Solicitar una cantidad de pistas guía el reparto y la recuperación, pero ya no convierte cada nombre inventado por la IA en una obligación absoluta. Las identidades opcionales vacías se retiran tras recuperación acotada, sin duplicar MIDI ni fabricar notas.
- Los instrumentos concretos nombrados por el usuario continúan siendo compromisos duros y nunca se eliminan silenciosamente.

### 0.58.27 — reconciliación estable de la matriz orquestal

- Los alias, abreviaturas, ordinales y diferencias de acentuación en `active_sections` se vinculan con los nombres autoritativos de la macroforma antes de solicitar otra respuesta remota.
- Si una sección queda por debajo de su densidad declarada, PULSO completa únicamente los enlaces de actividad con integrantes existentes y funciones complementarias; no crea instrumentos, roles ni notas.
- La reconciliación garantiza al menos dos responsabilidades armónicas o atmosféricas por sección, distribuye la continuidad favoreciendo secciones adyacentes y conserva la rotación del elenco.
- La operación es idempotente, queda registrada con cantidades normalizadas y evita rechazar o volver a pagar una composición por metadatos reparables.

### 0.58.26 — narrativa AI-first y desarrollo audible

- La protagonista, su respuesta, el bajo de movimiento y el conductor hipnótico se escriben juntos en un bloque narrativo prioritario, sin aumentar normalmente la cantidad de solicitudes remotas.
- Una protagonista debe estar escrita por IA en al menos el 65 % de sus ventanas narrativas, desarrollar sus retornos y cerrar la obra. El render local ya no copia frases para rellenar huecos ni fabrica una coda.
- Los retornos literales dominantes, las melodías formadas por saltos desconectados y las caminatas escalares se convierten en reparaciones focalizadas sobre la única identidad responsable.
- La música electrónica sin percusión conserva movimiento tonal mediante un bajo melódico independiente, salvo que el usuario excluya explícitamente el bajo.
- Las capas mantienen un piso armónico continuo, pero rotan responsabilidades, entradas, retiradas y respiraciones para evitar una meseta de tutti permanente.
- Las pruebas verifican presencia narrativa, transformación temática, fraseo melódico y protección del material ya aceptado.

### 0.58.25 — consolidación segura de clones redundantes

- Tras una recuperación acotada fallida, un clon MIDI literal de un reparto no solicitado deja de provocar el descarte de toda la obra: se conserva la interpretación original y se retira únicamente la pista redundante.
- La consolidación no se aplica a cantidades explícitas de pistas ni puede retirar la protagonista, el único bajo o el único conductor de movimiento.
- No se generan notas locales, no se fusionan líneas distintas y toda información MIDI única permanece intacta.

### 0.58.24 — reconciliación estructural del reparto

- La sección de resolución definida por la macroforma se enlaza automáticamente a la protagonista cuando Terra omite únicamente esa referencia en `active_sections`.
- La reconciliación ocurre antes de reintentar el manifiesto, evita una llamada remota innecesaria y no modifica identidades, notas, armonía, forma ni decisiones tímbricas.
- Las contradicciones reales continúan bloqueadas: protagonista ausente, duplicada o asignada a una voz distinta de `lead`.

### 0.58.23 — recuperación narrativa sin falsos rechazos

- Si Terra omite la identidad protagonista pero entrega una línea melódica válida, PULSO conserva esa interpretación, reasigna su propiedad y vuelve a ejecutar la garantía de coda después de la promoción.
- El cierre reutiliza exclusivamente notas escritas por la IA: agrega un placement terminal y una transposición tonal mínima, sin sintetizar notas procedurales.
- La validación vuelve a medir el MIDI ya reparado, por lo que una obra completa no se descarta por conservar la evidencia narrativa de la identidad vacía anterior.

### 0.58.22 — independencia musical sin falsos rechazos armónicos

- La detección bloqueante de clones exige coincidencia casi literal de ataque y altura MIDI exacta en más del 82 % de la línea menor. La duración y el contorno se evalúan dentro de los estados de frase; compartir clases tonales, cadencias o ritmo armónico ya no convierte pads, coro, piano y contrapunto en duplicados falsos.
- La evolución se mide mediante estados de frase de ocho compases normalizados por origen tonal y temporal. Una obra extensa requiere entre 3 y 6 estados según la función; la repetición hipnótica dentro de cada estado es válida.
- La estasis seccional es una observación reparable y no puede borrar una composición completa. Solo una copia MIDI severa o una coda protagonista ausente conservan autoridad bloqueante.
- Cada conflicto musical recibe una única reparación focalizada. Las reparaciones de independencia incluyen hasta 64 eventos reales de la pista contraparte para que la IA pueda escribir alrededor de ellos sin adivinar.
- Los índices de comparación se calculan una sola vez por pista y se reutilizan en repartos grandes.

### 0.58.21 — contrapunto independiente y resolución narrativa verificables

- Las pistas declaradas como independientes se comparan por ataques y clases de altura. Si más de dos tercios de la línea menor coincide con otra, PULSO conserva la propietaria estructural y solicita un reemplazo transaccional únicamente para la copia.
- Las capas persistentes de movimiento, voces interiores y cuerpo armónico deben desarrollar una cantidad proporcional de gramáticas de compás. Transponer literalmente el mismo patrón ya no cuenta como evolución.
- Una protagonista poblada que no complete su retorno dentro de los últimos ocho compases y no cierre en la armonía terminal queda como reparación bloqueante, no como observación editorial publicable.
- Las recuperaciones reciben evidencia cuantitativa y operaciones genéricas (`separate_independent_line`, `develop_sectional_evolution`, `resolve_narrative`); nunca se añaden pistas ni notas procedurales para ocultar el defecto.
- Las pruebas focalizadas cubren clonación, estasis seccional, reemplazo quirúrgico y coda obligatoria.

### 0.58.7 — exclusiones instrumentales coherentes de extremo a extremo

- Una dirección explícita sin batería o percusión prevalece sobre el dominio musical inferido.
- El manifiesto elimina instrumentos y voces rítmicas contradictorias antes de escribir las notas y completa el reparto únicamente con identidades compatibles.
- La normalización ya no añade automáticamente kick, snare ni hats a un reparto adaptativo sin percusión.
- La validación final ignora defensivamente cualquier identidad rítmica prohibida que pudiera sobrevivir en un plan antiguo, evitando rechazos falsos al terminar una composición costosa.

### 0.58.6 — configuración segura de IA dentro de PULSO

- `CONFIGURAR IA` permite pegar, guardar, probar y eliminar la clave sin salir del VST.
- En Windows la credencial se conserva mediante Credential Manager y nunca se serializa en Ableton, presets o logs.
- `OPENAI_API_KEY` continúa disponible como fallback para instalaciones automatizadas.
- La prueba de conexión valida autenticación sin lanzar ni facturar una composición.

### 0.58.5 — narrativa instrumental íntegra y respiración contextual

- El bajo hipnótico de fase estable se reconoce como una frase coherente; su variación se evalúa por separado.
- Las pistas testimoniales se desarrollan desde material GPT o se integran en un dueño compatible conservando notas, CC y expresión.
- El piso armónico exige dos capas en secciones activas y una en breakdowns, intros y retiradas intencionales.
- La audición neutral distingue correctamente pads, pedales y texturas, y ya no se informa como sonido faltante.

### 0.58.4 — audición neutral y MIDI limpio en Live

`CREAR EN LIVE` ya no busca presets expresivos a partir de descripciones poéticas. Cada
pista recibe una plantilla de audición estable según su función: Operator para subgraves,
bajos, arpegios y plucks; Drift para cuerpo armónico; Wavetable para pads, texturas y
voces melódicas; y un 909/808 verificado para material rítmico. Las alternativas sólo se
usan dentro de esa pequeña paleta nativa y nunca se eligen al azar.

La intención tímbrica escrita por la IA permanece en `authored_native_device` y
`authored_preset_intent`, de modo que puede recuperarse durante una fase posterior de diseño
sonoro sin contaminar la evaluación de las notas. Ataque, filtro, anchura y release se
normalizan mediante perfiles versionados y sin jitter. `Shift + clic` en `CREAR EN LIVE`
publica exactamente las mismas pistas y clips sin cargar ningún dispositivo, incluso si el
índice de sonidos de Live todavía no está listo.

### 0.58.3 — arquitectura tímbrica congelada y validación por propietario

La arquitectura de carriles queda congelada cuando comienza la escritura AI. Las normalizaciones
posteriores ya no pueden promover un destino tímbrico sin notas y reemplazar a un propietario para
el cual Terra ya compuso. Cada destino se conecta localmente con un propietario independiente y
compatible antes de crear los bloques de performance.

Durante la escritura se valida el contenido del carril propietario; la pista de destino se valida
después de realizar el relevo. El renderizador distribuye primero una ventana de frase a cada color
compatible para evitar que una pista solicitada quede vacía por un simple orden de rotación.

### 0.58.2 — liderazgo de movimiento sin rechazos falsos

Una composición electrónica sin percusión puede contener varios arpegios, secuencias, pulsos u
ostinatos complementarios. PULSO conserva todas esas identidades y elige localmente una sola como
`primary_motion_owner`; las restantes quedan declaradas como `supporting_motion`. La elección no
escribe, elimina ni transforma notas y no consume una solicitud adicional de IA.

El registro operativo informa la cantidad de candidatos, el identificador del conductor elegido y
la cantidad de apoyos. El gate solamente rechaza un reparto si no existe ningún instrumento tonal
capaz de asumir la conducción, no porque Terra haya diseñado más de una capa móvil válida.

### 0.58.1 — densidad perceptual sin borrar la composición

PULSO ya no reduce una obra electrónica a un techo rígido de pistas simultáneas. El director de
atención mide ocupación, duración y peso espectral por función: un pad ancho, un sub sostenido, un
hat corto y un impacto no consumen el mismo espacio. Las notas armónicas, melódicas, de bajo y de
movimiento escritas por Terra se conservan; el contador `density_notes_removed` debe permanecer en
cero. Solamente una transición semánticamente inválida que se comporte como una cama continua puede
ser acotada y queda declarada por separado.

El piso armónico se adapta ahora entre dos y cuatro capas según la densidad y energía de cada
sección, sin superar su objetivo perceptual. Los repartos grandes conservan como líneas completas
los fundamentos, cuerpos, bajos, movimientos, protagonista y contrapuntos. Los relevos tímbricos se
reservan para cambios de color explícitos, no para fragmentar automáticamente la instrumentación.

LiveBridge, el manifiesto MIDI, el estado del plug-in y el registro publican carga perceptual,
compases subocupados/sobrecargados, notas preservadas y la separación entre saneamiento semántico y
recorte de densidad.

### 0.58.0 — arquitectura musical antes que cantidad de pistas

En 0.58.0 los repartos electrónicos extensos separaban entre 12 y 18 arcos tonales independientes de
sus destinos tímbricos. Terra escribe las líneas musicales una sola vez; `relay`,
`timbral_handoff`, `doubling` y `octave_reinforcement` se realizan después como relevos de frases
completas. El número total de notas no aumenta y cada instrumento solicitado sigue llegando como
una pista MIDI distinta.

Esto reduce los bloques de escritura remota y evita pagar por copias del mismo motivo. El contrato
rítmico también exige desarrollo seccional proporcional al lenguaje creado por la IA: un único
golpe ya no puede justificar una pista de clap, hat o percusión. Esa versión reducía las respiraciones
a dos responsabilidades; 0.58.1 reemplaza ese límite y los auditores siguen al protagonista a
través de todo su `content_lane_id`.

LiveBridge y el sidecar MIDI publican `content_lane_count`, `timbral_handoff_destinations`,
`timbral_handoff_windows`, `timbral_handoff_notes` y `exact_instrument_cast_published`. El gate
declara un error explícito si el reparto solicitado no llega completo a Live.

### 0.57.0 — restricciones universales y publicación no destructiva

PULSO separa ahora tres autoridades: invariantes técnicos, compromisos explícitos del músico y
objetivos musicales. Solamente MIDI técnicamente inválido o una identidad explícitamente solicitada
sin ningún evento pueden bloquear la publicación. Cobertura, fraseo, relación temática, tensión y
resolución son objetivos editoriales reparables; nunca vuelven a borrar por sí solos una canción
completa.

Las recuperaciones reciben descriptores estructurados e independientes del género o del prompt:
`supply_missing_identity`, `extend_coverage`, `develop_phrase`, `resolve_narrative` y
`establish_thematic_relationship`. Cada bloque termina y conserva sus checkpoints. Si una mejora no
converge, PULSO continúa, evalúa la obra global y publica la mejor versión que cumpla los contratos
duros.

Las resoluciones ya no están forzadas universalmente a la tónica. El último ataque puede descansar en
un tono estable de la armonía terminal; la tónica permanece obligatoria cuando el blueprint la exige
explícitamente. No se crean notas para aprobar una observación editorial y ninguna pista poblada se
retira por quedar debajo de una métrica musical.

### 0.56.11 — tolerancia musical de un compás

Una interpretación AI que ya cumple cantidad de notas y desarrollo de frases puede quedar un solo
compás por debajo de su horizonte cuantitativo sin invalidar una canción completa. Este margen no
genera notas, no se aplica a material procedural y no acepta déficits de dos compases, de notas o de
fraseo.

La recuperación incremental y la auditoría final comparten el mismo criterio. Cada uso excepcional se
registra como `accepted one-bar coverage margin`, con identidad y mediciones completas, para que una
aceptación editorial nunca sea silenciosa.

### 0.56.10 — partitura global sin truncamiento oculto

La capacidad de cada respuesta parcial y la capacidad de la canción completa son ahora contratos
distintos. Los shards continúan siendo pequeños y económicos, mientras que la partitura ensamblada
admite hasta 512 celdas y 4096 ubicaciones MIDI. Una orquestación extensa ya no pierde silenciosamente
los instrumentos finales al atravesar el saneamiento global.

Cada fusión se normaliza inmediatamente con el mismo contrato global. El registro muestra las
cantidades anteriores y posteriores, los eventos inválidos y cualquier pérdida por capacidad. Si se
alcanzara un límite, la generación conserva el checkpoint y declara las identidades incompletas en vez
de terminar con el ambiguo rechazo `did not complete every explicitly requested instrument`.

### 0.56.9 — reparto de tamaño exacto

Una cantidad explícita de pistas es ahora parte del JSON Schema enviado a OpenAI. Si el músico pide 50
identidades, el arreglo `instruments` se genera con `minItems=50` y `maxItems=50`; Terra ya no puede
responder válidamente con 47, 48 o 51 pistas. El mismo esquema se reutiliza en la recuperación del
manifiesto, antes de comenzar cualquier bloque de interpretación MIDI.

Cuando el prompt no fija una cantidad, el reparto continúa siendo creativo y flexible dentro del máximo
profesional. No se elimina ninguna pista después de componer ni se altera la música para ajustar una
cuota: la arquitectura nace con el tamaño correcto.

### 0.56.8 — protagonista autoritativo entre fases

La macro narrativa ya no intenta nombrar un instrumento antes de que exista. El manifiesto global de
reparto declara un único `protagonist_instrument_id`, que debe coincidir exactamente con una identidad
Lead y estar activa en la sección de resolución. Ese ID se incorpora a la narrativa antes de solicitar
cualquier interpretación MIDI y permanece inmutable en los shards posteriores.

La corrección elimina el falso rechazo `lost its declared protagonist`: una composición completa ya no
puede descartarse porque la macro y el reparto hayan usado nombres distintos para la misma función. No
se infiere por texto ni se reescriben notas; el vínculo es estructurado, validado y registrado.

### 0.56.7 — enrutamiento estable de interpretación

El `instrument_id` declarado por el reparto es ahora la autoridad al interpretar cada bloque de Terra.
Si el modelo devuelve ese ID con una voz incorrecta, PULSO conserva íntegramente tiempo, altura,
duración y velocidad, y normaliza únicamente la voz hacia la propietaria declarada. Así una apertura
de hi-hat válida ya no desaparece por un error de etiqueta del modelo.

Cada respuesta se limita a las identidades del shard solicitado: notas de otras pistas ya no pueden
hacer que una reparación vacía parezca válida. El registro muestra por identidad eventos recibidos,
aceptados, reasignados y descartados. Para una pista con cero eventos, la recuperación exige una nota
y una ubicación concretas con el ID exacto; motivos globales, controles o prosa no sustituyen MIDI.

### 0.56.6 — recuperación quirúrgica de respiración

Los déficits cuantitativos continúan completándose de forma aditiva. Cuando una pista ya supera sus
mínimos de notas y cobertura pero sigue formando una única frase continua, PULSO reconoce que añadir
eventos no puede crear silencio y solicita una interpretación completa de reemplazo para esa sola
identidad. Terra debe conservar función, registro y relación formal, introduciendo frases separadas.

El reemplazo es transaccional: el material original permanece intacto hasta que la nueva interpretación
cumple por sí misma el contrato medido. Sólo entonces se sustituye esa pista; las demás identidades,
celdas y notas se preservan. Una propuesta que no converge se descarta sin dañar el score aceptado.

### 0.56.5 — contrato único de escritura instrumental

La escritura incremental y la publicación consultan ahora el mismo contrato `TrackViability`.
Desapareció la regla genérica oculta que exigía tres notas y dos secciones incluso cuando una pista
rítmica especializada estaba correctamente declarada como evento escaso. Las exigencias musicales
de protagonista, relación temática y cierre en la coda permanecen intactas.

Cada recuperación recibe y registra evidencia concreta por instrumento: notas, compases activos,
frases y, cuando corresponde, cierre o parentesco temático faltante. Terra completa únicamente ese
déficit medido y deja de repetir reparaciones ciegas que ya satisfacían el contrato comunicado.

### 0.56.4 — reconciliación incremental del reparto

Un manifiesto válido pero corto ya no se descarta ni se solicita nuevamente por completo. PULSO
conserva todas las identidades aceptadas y pide a Terra únicamente el déficit mediante una respuesta
estructurada pequeña, de razonamiento bajo y presupuesto acotado. Por ejemplo, un resultado 47/50
se transforma en una solicitud de tres identidades complementarias antes de comenzar la escritura.

Si esa reparación remota no está disponible, el catálogo completa exclusivamente los metadatos de
identidad, respetando exclusiones, voces existentes, secciones y el único propietario de movimiento.
No genera notas: el registro, el detalle sonoro y la interpretación continúan hacia las fases de IA.
Los identificadores originales permanecen en el mismo orden y cualquier duplicado se rechaza.

### 0.56.3 — recuperación granular sin pérdida compositiva

Los pedidos explícitos de pistas se convierten en un contrato numérico antes de escribir MIDI.
PULSO distingue el total de sus subconjuntos —por ejemplo, 50 pistas con 10 de batería— y rechaza
un manifiesto de tamaño incorrecto antes de iniciar los bloques costosos. Durante la escritura,
cada bloque aceptado queda intacto: únicamente los instrumentos incompletos se recuperan en shards
paralelos de hasta dos pistas y un reparto solicitado explícitamente nunca se reduce retirando voces.

Los choques verticales graves se corrigen primero como voicing: la voz armónica secundaria sube una
o dos octavas sin cambiar clase tonal, ritmo, duración, velocidad ni identidad narrativa. Solo cuando
ese desplazamiento no es posible se conserva la reparación anterior mediante espacio negativo.

### 0.56.2 — roles electrónicos y cierre audible

La escritura electrónica sin percusión exige ahora un único propietario de movimiento —arp,
secuencia, pulso, órbita u ostinato— y una pista de transición no puede satisfacer ese contrato.
Las transiciones quedan limitadas a ventanas breves alrededor de cambios formales, evitando que
una textura técnica domine decenas de compases como un secuenciador continuo.

El protagonista debe materializar su retorno en la coda con al menos tres notas, atacar dentro de
los últimos dos compases y cerrar sobre la tónica. Los mínimos de notas, compases activos y frases
se validan sobre el MIDI realmente renderizado. Las líneas independientes escritas por la IA ya no
se fusionan ni podan durante la publicación: una carencia vuelve al bloque exacto de Terra para su
reparación, preservando instrumentación, registro y diálogo propios.

### 0.56.1 — autoría narrativa, resolución y atención convergente

En las composiciones de IA, PULSO ya no completa localmente protagonistas, respuestas,
contrapuntos ni motores hipnóticos incompletos. Esas voces deben llegar escritas por Terra y
se validan antes de publicar: el protagonista vuelve transformado en la coda, la respuesta
comparte su familia temática sin copiar ritmo ni contorno, y el movimiento electrónico posee
una línea propia. La generación local queda limitada al piso armónico y a continuidad técnica.

`PlanDerived` deja de contabilizarse como autoría de IA. El origen `gpt_plan` y el modelo Terra
se preservan también al usar intención adaptativa, por lo que los manifiestos MIDI y Live
informan correctamente quién escribió la obra. La tonalidad consolidada admite ahora color
cromático escaso únicamente cuando GPT lo declara en el acorde exacto y lo resuelve por grado
conjunto hacia la escala principal.

El director de atención limita la respuesta al 60 % de las notas del protagonista y a un 15 %
de sus compases simultáneos. Después de completar el piso armónico ejecuta una segunda pasada
sustractiva: el resultado final mantiene normalmente entre cinco y siete propietarios activos,
sube hasta nueve en el ápice y ya no puede volver a congestionarse por las capas añadidas tarde.

### 0.56.0 — propiedad temática y orquestación con roles reales

La narrativa ya no se valida contando copias del mismo leitmotiv. Un nuevo contrato de propiedad
concentra `relay`, `timbral_handoff`, doblajes y refuerzos de octava en un protagonista, conserva
como máximo una respuesta que cite el motivo y deja intactos los contrapuntos genuinamente
independientes. La densidad se calcula por propietarios de contenido y detecta clones mediante
el contorno relativo de cada frase, incluso cuando fueron transportados a otra tonalidad.

Los instrumentos armónicos, de pedal, cuerpo, color y transición reciben desarrollos propios en
lugar de heredar automáticamente la melodía principal. La IA conserva autoridad sobre el elenco:
si su reparto está cerrado, PULSO no agrega un arpegio genérico ni instrumentos de catálogo. El
arpegio sólo se exige cuando fue declarado como parte de la idea. La dirección de atención opera
por compás, protege únicamente al protagonista, mantiene un piso armónico de dos propietarios y
crea respiraciones periódicas del low-end sin cortar la memoria armónica.

Las métricas de propietarios anteriores/finales, pistas consolidadas y notas reasignadas se
persisten en el proyecto, aparecen en el registro y se exportan a Live y al manifiesto MIDI.

### 0.55.9 — continuidad armónica y dirección de atención

Un nuevo `AttentionDirector` trabaja sobre el material ya compuesto por la IA antes de la
publicación. Conserva protagonistas, mutaciones rítmicas explícitas y pulsos obligatorios, pero
evita que las capas se acumulen como un tutti permanente: asigna presupuestos dinámicos de partes
por sección, crea respiraciones de frase y barras de contraste en fronteras formales, y rota el
acompañamiento por ventanas de cuatro compases.

En música electrónica también sostiene un piso armónico continuo mediante relevos entre las
capas de fundamento existentes. Las notas auxiliares son tonos del acorde escritos como
`PlanDerived`; no inventan una segunda composición procedural. Después del pase se vuelven a
validar tonalidad, registro, métrica, solapamientos, duraciones, releases y expresión MIDI.
La auditoría queda persistida en el proyecto y viaja a Live con métricas de respiración,
congestión, notas retiradas y cobertura armónica creada.

### 0.55.8 — publicación compatible con main

Una canción de IA que supera el contrato estructural completo vuelve a publicarse como en
`main`/0.55.0. Las métricas de narrativa, densidad, soundscape y desarrollo permanecen visibles
como diagnóstico editorial, pero ya no pueden borrar el resultado ni iniciar reparaciones remotas
costosas. Después del render solo bloquean la publicación los defectos reales de integridad MIDI:
tiempos inválidos, duraciones inseguras, eventos huérfanos o colisiones tonales no resueltas.

El modelo continúa siendo `gpt-5.6-terra` con razonamiento `medium`; se conservan el reparto global
fragmentado, los checkpoints y la recuperación acotada de transporte.

### 0.55.7 — reparación editorial fragmentada

Las correcciones posteriores a la audición se escriben en shards de hasta dos instrumentos, con
un máximo de tres solicitudes Terra simultáneas. Cada shard válido se conserva por instrumento;
una respuesta incompleta o inválida reintenta únicamente su material pendiente dentro del mismo
presupuesto. La canción se vuelve a audicionar una sola vez con todas las mejoras recuperadas y
el registro diferencia transporte, estado de Responses, JSON inválido y omisiones de cobertura.

### 0.55.6 — Terra Medium

La composición completa usa ahora `gpt-5.6-terra` con `reasoning.effort=medium` en todas las
fases de Responses API. La interfaz, los metadatos MIDI y los contratos enviados a Live reflejan
el mismo modelo; no queda una selección silenciosa ni una etiqueta heredada de Sol Max.

### 0.55.5 — publicación editorial recuperable

Una reparación selectiva incompleta ya no descarta una composición íntegra. PULSO conserva por
instrumento cada reparación que cumple su contrato, reintenta únicamente las pistas omitidas y
vuelve a audicionar el mejor candidato. Si la mejora opcional no termina, la obra original solo
puede publicarse cuando producción, narrativa, viabilidad y los pisos estrictos de soundscape
demuestran que las observaciones restantes son editoriales. Los fallos críticos continúan
bloqueando la publicación sin excepción.

### 0.55.4 — reparto global eficiente

La IA decide primero un manifiesto compacto para todo el elenco: identidades, funciones,
relaciones y trayectoria seccional de cada pista quedan fijadas antes de escribir detalles.
Después, bloques de hasta diez instrumentos completan únicamente registros, articulaciones,
timbres, dispositivos y evolución espacial. Hasta tres bloques se procesan en paralelo y una
falla recupera solamente el bloque pendiente; nunca vuelve a pagar ni a rediseñar el reparto ya
aceptado. Así, una solicitud de 50 pistas conserva una única intención compositiva global sin
depender de un JSON monolítico propenso a vencer el plazo.

### 0.55.3 — gate convergente y auditoría persistente

El gate distingue ahora integridad crítica de observaciones editoriales. Tonalidad, MIDI
inválido, narrativa realmente rota, soundscape severamente degradado y una proporción baja de
pistas viables continúan bloqueando la publicación. Una composición segura que mejora de forma
medible tras la reparación puede publicarse aunque conserve observaciones menores: el crítico ya
no transforma cada recomendación en un rechazo absoluto.

La reparación dispone de hasta dos tandas de seis instrumentos bajo un único presupuesto global
de tres minutos. La segunda tanda prioriza pistas aún no editadas y solo conserva un candidato si
reduce el déficit audible sin degradar producción. Cada fase se registra en
`%APPDATA%\PULSO\pulso-operational.log`; un rechazo guarda además su auditoría estructurada en
`%APPDATA%\PULSO\Audits`, con métricas, causas y conteo de notas por instrumento. Ningún prompt
ni clave API se escribe en esos archivos.

### 0.55.2 — reparación musical selectiva

La ruta incremental ahora audiciona el MIDI ensamblado antes de publicarlo. El crítico
identifica voces nominales o subdesarrolladas, concentración excesiva en un único arpegio,
densidad estática y responsables concretos de un cierre sin resolución. Si encuentra uno de
estos defectos, realiza una sola petición acotada para un máximo de seis instrumentos: forma,
armonía, reparto y todo el MIDI aceptado de las demás pistas permanecen inmutables.

La reparación recibe también las frases originales de esas pistas para desarrollar su identidad
en vez de sustituirla por material inconexo. El candidato se vuelve a renderizar y debe superar
producción, narrativa, soundscape y viabilidad de pistas. Un fallo de red, JSON incompleto o una
revisión insuficiente conserva la composición anterior; ya no se publica silenciosamente un
reemplazo procedural. En Live, los timbres `featured` y `critical` por debajo de su contrato
obtienen un segundo intento determinista, aceptado únicamente cuando mejora la fidelidad audible.

### 0.55.1 — flujo creativo esencial

La interfaz principal queda reducida a idioma, prompt, duración, composición,
visualización MIDI y creación en Live. La exportación usa siempre orquestación
completa; los bloqueos, la audición interna y los modos alternativos heredados ya
no pueden modificar silenciosamente el resultado.

### 0.55.0 — composición Sol Max incremental

La canción larga ya no depende de una respuesta monolítica. `gpt-5.6-sol` con razonamiento
`max` dirige primero un blueprint estricto sin notas: forma, historia y armonía. Un plazo
acotado recupera esa fase con Sol `low` si `max` no termina, sin cambiar de modelo ni caer al
motor procedural. El reparto y el `performance_score` se materializan con Sol `low`, bajo ese
blueprint inmutable, en bloques de hasta
10 instrumentos agrupados por familia y línea musical. Un reparto de 50 instrumentos se
resuelve en cinco bloques acotados; el límite total es 64 instrumentos.

Cada bloque completado se conserva inmediatamente. Si una respuesta vence, no parsea o deja
instrumentos nominales, PULSO reintenta sólo los ids faltantes y divide el bloque hasta dos
niveles adicionales. El ensamblado rechaza notas asignadas a otro instrumento, ids inválidos,
líneas regulares sin al menos dos apariciones seccionales y celdas sin placements. Transiciones
y eventos únicos mantienen su excepción musical. El fallback local sólo se considera después
de agotar esta recuperación AI acotada.

Si después de tres intentos queda una instancia nominal sin una interpretación suficiente,
PULSO retira sólo esa pista y sus eventos huérfanos. La canción válida ya escrita continúa al
gate final; una pista decorativa incompleta nunca vuelve a descartar la obra entera.

La interfaz informa `BLUEPRINT`, bloque actual, cantidad total, `RECOVERING BLOCK`, intento y
validación final. Todas las fases usan Responses API en background, polling cancelable y un
deadline global, por lo que cerrar una conexión no congela la interfaz ni descarta bloques ya
aceptados.

### 0.54.0 — cierre musical convergente y Sol Max

El MIDI definitivo atraviesa hasta tres pasadas de cierre musical. PULSO reconstruye desde
el plan GPT un piso armónico de dos capas en al menos 85% de la obra, completa la presencia
del protagonista por ventanas de frase —sin penalizar sus silencios internos— y convierte la
última llegada de resolución en una consecuencia tonal sobre la tónica con menor registro y
densidad. Después vuelve a validar tonalidad, registro, métrica, duraciones, colisiones,
expresión y viabilidad instrumental.

Una pista que todavía quede incompleta en la frontera de publicación se releva hacia una voz
compatible cuando contiene autoría estructural o se elimina cuando sólo contiene relleno. El
resultado exportado nunca conserva `token tracks`; reducir un reparto inflado es una reparación
válida y no un motivo para fabricar más notas.

La composición GPT usa por defecto `gpt-5.6-sol` en Responses API con
`reasoning.effort = max`. Arquitectura y revisiones se ejecutan en background, permanecen
cancelables y comparten un límite total de 45 minutos. El fallback también comprende
expresiones como “no hace falta batería” y conserva tonalidades explícitas como `F#min`.

### 0.53.0 — pistas con función musical

`TrackViabilityContract` audita cada pista instrumental que realmente llegará a Live. Los
mínimos dependen de su responsabilidad: piso armónico, voz armónica, pulso, protagonista,
diálogo, ambiente, transición o evento puntual. Una transición o un impacto pueden ser breves;
una voz musical ya no puede existir como una pista nominal con una o dos notas.

Cuando GPT dejó al menos dos semillas autorales coherentes, PULSO desarrolla esa intención a
través de la forma usando el motivo y la armonía del propio plan. El relleno técnico sin una
trayectoria independiente se fusiona con una pista compatible o se poda. El gate final mide
pistas declaradas, retenidas, viables, desarrolladas, fusionadas y podadas, y nunca publica
`token tracks` para inflar artificialmente el arreglo.

### 0.52.0 — narrativa causal y resolución audible

PULSO incorpora `NarrativeSpine`: GPT debe declarar una premisa, una pregunta concreta,
la deuda armónica, el instrumento protagonista y una cadena de actos con causa y
consecuencia. El contrato se ejecuta sobre el MIDI: plantea el motivo, lo deja abierto,
lo transforma, lo lleva al clímax y reserva una llegada final a tónica con relajación de
registro y densidad. `NarrativeScoreGate` mide esa evidencia después de todos los pases;
los nombres de sección o la mera similitud temática ya no bastan para aprobar una obra.

También se corrigió la clasificación del protagonista: una voz `Lead` con función
contrapuntística sigue siendo el narrador principal salvo que esté declarada explícitamente
como `call_response`, respuesta o réplica.

### 0.51.1 — autoridad narrativa y funciones exclusivas

El tejido electrónico ya no confunde una articulación repetitiva con una función de
arpegio: bajos, cuerdas y acordes `ostinato` conservan su identidad, mientras que el MIDI
de arpegio sólo puede llegar a propietarios explícitos. Los arpegios rotan entre pistas
válidas según la forma y respetan el material que GPT ya desarrolló.

El protagonista emplea varias sentencias rítmicas y transformaciones del motivo del plan
para construir planteamiento, desarrollo y retorno. El piso armónico ahora completa huecos
objetivos sin superponer una composición procedural a un tejido de GPT ya suficiente.

Los presupuestos de densidad forman parte del gate, los pedidos de sólo armonía/melodía
eliminan percusión antes y después del render, y todas las métricas del tejido se recalculan
sobre el MIDI definitivo que se publica en Live.

### 0.51.0 — tejido compositivo aditivo

PULSO separa ahora `source_voice`, pista instrumental y línea musical. Cada instrumento
declara un `content_lane_id`; dos pistas sólo pueden compartirlo cuando existe una relación
explícita de doubling, relay, call-response, refuerzo de octava o relevo tímbrico. Aumentar
el cast ya no puede fingir profundidad repartiendo una misma frase entre muchas pistas.

`ElectronicCompositionFabric` conserva el plan armónico de GPT como sustrato protegido y le
suma material derivado del propio plan: piso armónico intercalado, protagonista por frases,
arpegio electrónico con variaciones y respiraciones, respuestas temáticas independientes y
gestos de soporte. El origen MIDI `plan_derived` distingue este trabajo de un fallback local.

El gate final mide líneas independientes y significativas, cobertura y mediana del piso
armónico, ventanas del protagonista, notas reales de arpegio y líneas de diálogo. El contrato
admite hasta 64 instrumentos, pero una pista sólo cuenta si contiene una responsabilidad
musical desarrollada.

### 0.50.0 — paisaje electrónico compuesto y auditable

PULSO pide ahora a GPT un `electronic_soundscape` causal para cada obra electrónica:
escenario perceptual, narrativa espacial y una trayectoria concreta por instrumento. Las
capas se clasifican como `voice`, `environment`, `transition` u `one_shot`; cada clase se
valida con mínimos musicales diferentes. Un arpegio o pad con cuatro notas ya no cuenta
como una voz desarrollada, mientras que un impacto singular legítimo no se penaliza.

El plan referencia los identificadores reales de `instruments[]`, declara relaciones entre
capas, escala temporal, evolución, frases, compases activos y máximo de repetición estática.
La auditoría mide el MIDI final que llega a Live: capas declaradas/materializadas,
cobertura significativa, repeticiones estáticas y densidad mediana del tejido. Los resultados
se publican tanto en LiveBridge como en el manifiesto `.pulso.json`.

Los casts escritos por GPT son ahora cerrados: el motor local no agrega sintetizadores para
alcanzar una cuota numérica. Si faltan arpegios, ambientes o transiciones desarrolladas, el
crítico devuelve el plan a GPT para que escriba música real o elimine la capa. Los pedidos
explícitos sin batería conservan `percussion_free` de extremo a extremo y se evalúan por
movimiento armónico, tímbrico y espacial, nunca por ausencia de kick o groove.

### 0.49.0 — densidad instrumental y autoría por pista

- `ArrangementDensityPlanner` convierte duración, profundidad armónica y escala del ensemble en
  un objetivo explícito de 14–24 partes para producción electrónica profunda.
- Cada nota y control de `performance_score` puede nombrar un `instrument_id` concreto. Esa
  propiedad sobrevive hasta el `partId` exportado, aunque varios instrumentos compartan voz.
- El arreglo publicado informa pistas propuestas y pobladas, balance por departamento, máximo
  simultáneo, concentración de notas e independencia entre partes.
- La escala instrumental se obtiene rotando pads, pulsos, texturas y hablantes melódicos; nunca
  agregando bajos duplicados ni haciendo sonar todas las pistas al mismo tiempo.

### 0.48.1 — autoridad creativa GPT

- En modo GPT, `Lead`, `Countermelody` y `MovementBass` son voces de identidad exclusivamente
  autorales. El motor local ya no completa sus huecos con frases genéricas: un hueco es silencio
  intencional o una carencia que el crítico devuelve al modelo.
- Cuando GPT reclama una voz armónica o rítmica mediante `performance_score`, también se elimina
  el relleno procedural de esa voz. Las anclas técnicas `local_repair` se conservan para proteger
  integridad, transporte y continuidad física sin apropiarse de la composición.
- El gate exige al resultado publicado —no al JSON declarado— al menos 85% de autoría AI en
  foreground, 75% en bajo de movimiento y 45% de cobertura GPT del groove electrónico.
- La selección vuelve a evaluar tonalidad, fraseo, densidad, memoria, desarrollo y forma después
  de aplicar la frontera de autoría. Más libertad no permite publicar material incoherente.
- Los destinos de `voice_map` participan ahora realmente en expresión y orquestación. Además, la
  compactación MIDI conserva inflexiones breves de pitch bend y aftertouch.

### 0.48 — autoría audible y criterio creativo

- GPT deja de ser sólo un planificador: el gate exige autoría audible suficiente en foreground,
  bajo de movimiento y estructura de groove antes de considerar resuelta la composición.
- El análisis detecta melodías escalares sin fraseo, exceso de relleno procedural, bajo
  fragmentado y ausencias demasiado largas de pulso o low-end en música de club.
- La publicación final repara únicamente continuidad estructural conservadora; no sustituye las
  decisiones creativas de GPT. Todas las reparaciones quedan marcadas como `local_repair`.
- El gate creativo es independiente de la integridad MIDI. Una canción técnicamente válida nunca
  desaparece: Live la recibe y muestra las causas concretas que requieren revisión.
- En mundos electrónicos se excluyen del foreground los mallets y presets con carácter de juguete,
  chiptune o videojuego, salvo que el músico los solicite explícitamente.

### 0.47 — identidad tímbrica y variación real

- GPT compone una firma tímbrica por pista: fuente, envolvente, espectro, movimiento,
  espacio, textura y grado de exploración. Ya no entrega solamente la palabra `lead`.
- Live elige de un top-K de presets compatibles mediante la semilla de la canción. La
  identidad instrumental y el piso de fidelidad siguen siendo restricciones estrictas.
- El historial persistente evita repetir los mismos presets entre canciones; la elección
  sigue siendo reproducible y un sonido bloqueado se conserva en despliegues posteriores.
- En el menú de cada pista, `BLOQUEAR SONIDO` conserva la elección y `BUSCAR OTRA VARIANTE`
  incrementa únicamente su variación para el siguiente `CREATE IN LIVE`.
- Drift, Wavetable, Operator, Analog y Meld reciben ajustes conservadores de envolvente,
  filtro, movimiento y anchura derivados de la firma; los presets acústicos se preservan.
- El inventario nativo sube de 6.000 a 16.000 sonidos sin escanear plug-ins externos.

### 0.46 — propiedad musical y pocket de bajo

- La selección de revisiones es lexicográfica: primero reduce déficit de autoría principal,
  fraseo de bajo, memoria, densidad y groove; el score agregado solo desempata candidatos con
  cumplimiento equivalente.
- Hasta dos reparaciones focalizadas adicionales pueden sustituir relleno procedural de lead,
  respuesta, bajo y armonía por células y placements explícitos de GPT.
- Una mejora real de cobertura gana aunque reduzca levemente un score cosmético; una canción
  tonalmente limpia ya no puede ocultar que el foreground fue compuesto por fallback.
- La auditoría de bajo reconoce una nota corta por compás como parte de un pocket de ocho
  compases. Solo una pausa estructural superior a un compás más pickup abre otra frase.
- El warning electrónico de memoria exacta se elimina cuando la auditoría audible confirma
  retorno temático reconocible y desarrollado, evitando diagnósticos contradictorios.

### 0.45 — intención limpia y fallback observable

- La clasificación musical usa exclusivamente el texto escrito por el músico. El inventario
  de Live y el feedback técnico siguen llegando a GPT, pero ya no pueden convertir techno o
  progressive house en una producción híbrida por contener nombres orquestales.
- Los planes largos disponen de hasta 100k tokens estructurados. Si una respuesta termina
  incompleta, PULSO muestra su estado y `incomplete_details` en lugar de sustituirla en silencio.
- Un fallback local publica `production_mode_source=local_fallback`, registra el error exacto y
  lo incorpora a `production_issues` en el request de Live.
- La autenticación y el acceso al modelo configurado se verifican contra Responses API; el motor
  local deja de confundirse con una composición AI en las auditorías posteriores.

### 0.44 — desarrollo musical humano

- La autoría de GPT conserva procedencia, pero ya no queda exenta del crítico musical. Una
  pista AI con barras copiadas recibe sustracción, respiración o puntuación de frase sin salir
  de la grilla ni de la tonalidad.
- El kick mantiene el contrato solicitado de negras y desarrolla finales de frase con gestos
  escasos; clap, hats y percusión ya no pueden repetir indefinidamente el mismo compás.
- El bajo se edita antes de que existan las partes orquestadas y forma frases de ocho compases
  con retornos desarrollados, interlock con el kick y respiraciones audibles.
- La memoria temática distingue identidad de copia. Aproximadamente un retorno de cada tres
  puede ser literal; los demás deben fragmentar, desplazar, responder o cambiar su cadencia.
- La selección de candidatos pondera narrativa, producción electrónica, calidad simbólica e
  integridad MIDI. Para club, un candidato con loop rítmico extenso ya no gana por tener buena
  tonalidad o densidad.
- Los defectos musicales quedan visibles como diagnóstico y provocan revisión/selección, pero
  nunca hacen desaparecer un MIDI técnicamente válido ni restauran silenciosamente la idea anterior.
- Live recibe `literal_thematic_return_ratio` y `thematic_development`, además de las métricas
  de memoria, bajo, densidad y adecuación electrónica existentes.

### 0.43 — memoria musical audible

- La recurrencia temática se calcula sobre el MIDI final mediante ritmo de ataques,
  duraciones y contorno interválico. Compartir `theme_id` ya no alcanza para aprobar.
- El crítico dejó de premiar la novedad melódica permanente: busca una zona equilibrada
  entre recuerdo reconocible, transformación y silencio.
- GPT debe cubrir al menos el 60% de la línea temporal principal y recibe una segunda
  reparación dirigida si fallan memoria, fraseo de bajo o control de densidad.
- El bajo de movimiento se audita como frases completas; las sucesiones de gestos aislados
  ya no pueden aprobar como desarrollo.
- En producción electrónica de club se limita la atención a nueve voces sonoras por compás
  y dos propietarios de foreground. Las capas auxiliares rotan en vez de acumularse.
- Las referencias a artistas se traducen a atributos musicales abstractos. Para progressive
  hipnótico se priorizan pocket kick–bass, desarrollo sustractivo, hook económico y retornos
  demorados, sin copiar ninguna grabación.
- `request.json` y el manifiesto MIDI publican `audible_thematic_similarity`,
  `bass_phrase_continuity`, `density_control` y `peak_active_voices`.
- El resolver tímbrico entiende negaciones y separa instrucciones técnicas del carácter:
  `no reverb`, `sin reverb` o `hard 180 ms gate` ya no generan falsos rechazos del kick.

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

PULSO usa `gpt-5.6-terra` con razonamiento `medium` mediante OpenAI Responses API y Structured Outputs. La clave
no está incluida en el plugin ni en el repositorio. Configúrala para tu usuario:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/configure-openai.ps1
```

Cierra Ableton por completo y vuelve a abrirlo. El indicador mostrará
`GPT-5.6 TERRA MID · VALIDATED` tras una composición válida. Si falta la clave, la red
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

### 0.58.43 — convergencia temprana de la coda

- Un protagonista completo que solo omite su colocación terminal ya no dispara otra llamada remota ni rechaza la canción.
- Antes de recuperar por red, PULSO reutiliza una frase conectada del mismo protagonista generado por la IA, crea una recapitulación transformada en el final absoluto y la valida mediante el renderizador independiente.
- La operación es transaccional: no inventa notas, no toma material de otro instrumento y restaura el bloque original si la coda no satisface el contrato.
- Los protagonistas vacíos, los marcadores aislados y las líneas sin frases conectadas continúan fallando temprano.

### 0.58.42 — protagonista primero y arco armónico completo

- El protagonista se escribe y valida en un bloque exclusivo antes de orquestar el resto: una respuesta vacía, marcadores aislados o una coda desconectada abortan temprano, sin gastar el presupuesto completo en un ensemble que no tiene voz principal.
- Ninguna promoción de propiedad acepta ahora eventos sueltos como frase. El material donante debe contener al menos cuatro ataques conectados y la auditoría final aplica exactamente el mismo criterio.
- La coda se mide contra el final absoluto de la canción, incluso cuando el acto `resolution` está seguido por un `aftermath`; el último ataque debe caer en los dos compases finales sobre una nota estable de la armonía terminal.
- `Primary Chord Bed` participa en los hitos esenciales de premisa, desarrollo, clímax y final con revoicings independientes, conservando respiraciones intencionales en vez de convertirse en un pad permanente.
- Se añadieron regresiones para marcadores falsos, escalado normalizado, finales con aftermath y colchones confinados indebidamente a escenas intermedias.

### 0.58.41 — narrativa audible y recapitulación garantizada

- La recapitulación de la protagonista se verifica siempre sobre la partitura ensamblada, aunque ninguna otra cobertura active la recuperación. La operación reutiliza notas escritas por la IA, es transaccional e idempotente.
- El criterio melódico exige entre 25 % y 68 % de movimiento conjunto dentro de las frases: conserva saltos característicos, pero rechaza tanto la “ruleta de acordes” como las escalas mecánicas.
- El bajo ya no obtiene continuidad mediante una nota larga aislada por ventana. Reutiliza un pocket completo escrito por la IA, lo adapta a la armonía de destino y conserva aproximadamente un 35 % de respiración en el bajo móvil.
- Una obra larga debe proponer al menos tres voces conversacionales: una respuesta derivada y dos contralíneas independientes. El auditor reconoce contrapuntos reales aunque no lleven literalmente la etiqueta `reply`.
- La selección de revisiones pondera explícitamente resolución, fraseo conectado y cantidad de diálogos. Una revisión fallida sigue conservando la composición completa en lugar de provocar un rechazo destructivo.

### 0.58.40 — coda estable tras normalización

- La recuperación de una protagonista vacía selecciona desde el origen una escala temporal admitida por el motor MIDI; la normalización ya no puede desplazar su ataque terminal fuera de la ventana de resolución.
- Una prueba de regresión atraviesa promoción, coda, normalización completa y auditoría independiente antes de aceptar el arreglo.
- La presencia narrativa y el fraseo todavía insuficientes continúan hacia la reparación selectiva; sólo una identidad realmente vacía o una coda todavía ausente bloquean antes de la audición.

### 0.58.39 — publicación transaccional y reparación causal

- Una composición completa y técnicamente válida ya no se descarta solamente porque su cierre sea editorialmente débil; si la reparación opcional empeora la obra, se publica intacto el checkpoint original con observaciones.
- Los fallos genuinos de producción, material críticamente incompleto y narrativa global por debajo del piso de seguridad continúan bloqueando la publicación.
- El diagnóstico dirige problemas de groove al pulso estructural, fragmentación grave al bajo móvil y deuda terminal al protagonista y al colchón armónico, en vez de reescribir colores instrumentales no relacionados.
- Las pruebas reproducen el perfil exacto del rechazo de 192 compases y verifican que una reparación regresiva nunca destruya una composición completa.

### 0.58.38 — autoría audible y continuidad exacta

- El colchón armónico principal queda bajo propiedad exclusiva de la partitura de IA: los pases locales ya no rellenan sus silencios, cambian sus inversiones ni reescriben su coda.
- La voz protagonista sólo acredita una frase cuando encadena al menos cuatro ataques; marcadores aislados ya no pueden simular desarrollo melódico.
- La recuperación terminal conserva hasta ocho ataques del material protagonista y rechaza un resultado críticamente incompleto en lugar de publicarlo como canción terminada.
- La continuidad de bombo y grave se audita también en música electrónica híbrida cuando el reparto declara pulso; las métricas ahora describen el MIDI realmente exportado.
- Las pistas independientes que continúan siendo meros fragmentos tras su oportunidad de reparación se integran en una voz compatible o se eliminan, salvo identidades esenciales del tema.

### 0.58.37 — cierre armónico transaccional

- Una deuda terminal localizada del `primary_chord_bed` ya no descarta una composición completa: se repara exclusivamente su ventana final de cuatro compases usando la tónica y la paleta que GPT ya había elegido.
- La transacción aísla las celdas compartidas, conserva exactamente el MIDI anterior a la coda y no modifica ningún instrumento ajeno al colchón.
- El evento armónico terminal queda ligado a la tónica y la misma auditoría independiente que detectó la deuda verifica el resultado; una segunda pasada es idempotente.
- Si la microcirugía no resulta posible, el cierre queda como objetivo editorial y se conserva el checkpoint completo en vez de destruir veinte pistas válidas.
- Los rechazos tempranos de contratos finales y convergencia escriben ahora un JSON de auditoría con etapa, métricas e inventario de instrumentos.

### 0.58.36 — autoría cerrada y resolución armónica

- Las reparaciones editoriales se solicitan por instrumento, con un resumen compacto de forma y armonía. Esto evita respuestas truncadas por `max_output_tokens` y permite que el protagonista sea reescrito como una voz narrativa propia.
- Un reparto creado por GPT ya no recibe pistas genéricas `density_*` por tratarse de una obra larga. Las pistas exportadas deben corresponder a responsabilidades musicales realmente escritas.
- El director de atención preserva los silencios deliberados del `primary_chord_bed`; otros colchones pueden sostener el contexto sin rellenar proceduralmente su respiración.
- El cierre armónico exige llegada a la tónica o un final abierto/modal explícito, estable y de baja tensión. Un acorde de loop no tónico ya no cuenta automáticamente como resolución.
- Las pistas testimoniales de pocas notas no se expanden artificialmente: sus gestos se conservan mediante relay o se compactan antes de publicación.

### 0.58.35 — reconciliación del motor musical escrito

- La elección provisional de `primary_motion_owner` ahora se contrasta con el MIDI realmente escrito por GPT después de ensamblar todos los bloques.
- Si el dueño provisional está vacío y otra pista compatible ya contiene el movimiento, PULSO transfiere solamente la responsabilidad semántica, sin copiar, duplicar ni generar notas.
- Una pista concreta solicitada explícitamente por el usuario nunca se sustituye mediante esta reconciliación.
- La pista nominal vacía queda disponible para el retiro seguro existente, evitando rechazar una composición completa por una etiqueta de arquitectura desactualizada.

### 0.58.34 — narrativa selectiva y colchón armónico central

- La protagonista, el bajo conductor y el `primary_chord_bed` son responsabilidades
  exclusivamente AI cuando existe una partitura GPT. El renderer local puede validar,
  cuantizar y corregir seguridad MIDI, pero ya no compone relleno para esas voces.
- Una sola pista `harmonic_foundation` es elegida como colchón central. Debe contener
  acordes polifónicos completos de tres a cinco notas, inversiones y conducción de voces;
  repartir una nota del acorde entre varias pistas ya no satisface el contrato.
- El colchón central debe respirar y regresar. La arquitectura global conserva memoria
  tonal mediante capas complementarias, pero la densidad debe mostrar reducción,
  acumulación, clímax y liberación en vez de un tutti constante.
- El auditor puede reescribir únicamente las voces deficientes en hasta dos pasadas
  transaccionales. Sólo se acepta un reemplazo que reduzca deuda musical o aumente el
  contraste; cualquier fallo conserva intacto el último checkpoint completo.
- Las pistas testimoniales se desarrollan como frases reales o permanecen como objetivos
  editoriales explícitos. Una revisión fallida nunca rechaza la canción completa.

Lee [docs/ROADMAP.md](docs/ROADMAP.md) para las siguientes etapas y
[docs/LICENSING.md](docs/LICENSING.md) antes de distribuir binarios.
