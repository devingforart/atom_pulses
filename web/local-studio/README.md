# PULSO Local Studio

## Compositor AI soberano (experimental)

La suite local usa el mismo `SongGenerationPipeline` que el worker Cloud. En el
modo `ai_sovereign`, la IA define la forma, los acordes, el reparto instrumental
y **cada nota MIDI**. Para repartos de hasta ocho pistas y obras de al menos
24 compases, escribe pasajes por ventanas de seccion: primero colchon/bajo y
luego las voces expresivas sobre el MIDI ya aceptado. El renderizador conserva
las alturas escritas; no completa huecos ni corrige frases con notas
procedurales. Una revision rechazada pide una nueva respuesta de IA para el
grupo responsable, con el acorde, instrumento y beat local del conflicto.

Los archivos `partial-XX.mid` son checkpoints de escucha, **no obras
terminadas**. Una ventana rechazada no publica una cancion completa. El
mejor candidato rechazado por una ventana se conserva en un MIDI
`diagnostic-XX.mid` separado de los checkpoints aceptados, para poder
auditarlo sin volver a pagar la misma composicion. El diagnostico no es una
aprobacion musical ni se presenta como obra final.
El
validador musical es una ayuda editorial: pasar sus pruebas no certifica que
el resultado tenga interes artistico. La prueba de un minuto con seis pistas
sirve para verificar coherencia e integridad; las obras de 10-15 minutos aun
requieren medicion especifica de duracion, costo, continuidad y narrativa.

Cada composicion nueva puede generar cargos de API. Para evitar sorpresas,
consulta `api-events.jsonl` y `progress-events.jsonl` en el directorio del
trabajo antes de repetir una generacion fallida.

En obras largas, el escritor editorial y sus recuperaciones permiten hasta
18 000 tokens de salida por bloque y 24 000 en sus reintentos acotados. Es un
limite maximo, no un gasto fijo: una respuesta breve conserva su consumo real. Esto evita repetir
el antiguo limite fijo de 10 000 tokens cuando un JSON valido necesita mas
espacio para completar una pista pedida explicitamente.

El estudio guarda en `build/local-studio-data/validated-response-cache/` solo
respuestas que pasaron su validacion: plano macro, reparto y ventanas MIDI
aceptadas. Un nuevo trabajo con solicitud identica puede reutilizarlas; una
ventana rechazada nunca queda cacheada. Cada entrada contiene el pedido exacto
y se comprueba antes de usarla, de modo que una colision del nombre de archivo
no puede cambiar la musica. La telemetria marca estas lecturas como
`reused_locally` y les asigna cero tokens/costo de API. Cambiar prompt,
semilla, modelo o contrato invalida naturalmente la coincidencia. El cache es
local, esta dentro de `build/` y no se distribuye con el producto.

Suite local de composición y escucha en `http://127.0.0.1:4177`, sin abrir Ableton, compilar el VST, ejecutar CMake, instalar Docker/PostgreSQL o desplegar Cloud. La interfaz invoca el **ejecutable headless existente** `build-cloud/Release/pulso_cloud_worker.exe`; ese ejecutable llama a `SongGenerationPipeline`, el mismo núcleo musical que usa la versión Cloud. No existe un segundo compositor web.

## Iniciar

Desde la raíz del repositorio, en PowerShell:

```powershell
./scripts/start-local-studio.ps1
```

La clave `OPENAI_API_KEY` debe estar configurada como variable de entorno del usuario o del proceso. El script nunca imprime su valor. Abrí `http://127.0.0.1:4177`. La página indica si el ejecutable y la clave están disponibles. **Abrir la página no envía solicitudes a OpenAI**: solo el botón «Comenzar composición» lo hace y puede generar un cargo.

Para medir latencia sin tocar el MIDI ni interrumpir una generación, ejecutá
`node tools/analyze-local-latency.mjs <id-del-trabajo>`. El informe separa
escritura, revisión, cantidad de llamadas, tokens, divisiones de bloques y
correcciones puntuales. En pruebas A/B de obras largas puede activarse
`PULSO_AI_SINGLE_OWNER_WRITING=1` **antes de iniciar el servidor local**: el
worker escribirá cada identidad en una transacción propia, conservando su
orden, la autoría de notas por IA y todas las puertas de aceptación. No se
activa en producción sin comparar tiempo y calidad con el modo establecido.
Para comparar dos obras terminadas con el mismo encargo, ejecutá
`node tools/compare-local-compositions.mjs <directorio-base> <directorio-candidato>`.
Además de tiempo y llamadas, informa pistas con MIDI, densidad por pista,
continuidad armónica y problemas de la auditoría. La misma semilla no obliga
a la IA a devolver notas idénticas; una comparación objetiva nunca sustituye
la escucha humana del MIDI completo.
Para una comparación más estricta que conservar solo semilla y prompt, la API
local admite `blueprint_from_job` con el UUID de una obra terminada. Exige
idénticos prompt, duración, tempo, semilla y modo; copia únicamente el plan
aceptado y obliga a escribir nuevamente todas las notas. Es una herramienta
de ensayo aislada, no una opción de publicación ni un atajo para recuperar
notas de otra obra.

Opcional: `./scripts/start-local-studio.ps1 -Port 4180`. Para usar otro worker existente, definí `PULSO_LOCAL_WORKER_PATH` antes de iniciar. El script no compila nada.

## Ciclo de trabajo

### Perfil editorial local (experimental)

**Prueba de coherencia (hasta 60 s).** Cuando esta opcion esta activa, el
protagonista, el bajo y el colchon de acordes se escriben juntos en una unica
partitura de tiempos absolutos. La suite valida las tres identidades, los
registros, los limites de seccion y la monofonia del protagonista. La cobertura
de acordes polifonicos en apertura/desarrollo/cierre y las funciones poco
desarrolladas quedan como observaciones musicales, no como errores de archivo.
La publicacion exige todas las notas
escritas por la IA y una integridad MIDI objetiva: tiempos, duraciones, alturas
validas y pistas existentes. Un posible choque armonico, cromatismo o sostenido
musicalmente dudoso no se confunde con un archivo corrupto. Un revisor
contextual pondera duracion, registro, funcion armonica, propietarios de las
voces y liberacion de la tension. Solo ante pasajes prioritarios solicita una
unica respuesta adicional de la IA, limitada a dos ventanas de partitura. El
sistema no inventa ni mueve notas: inserta exclusivamente las notas que la IA
devuelve dentro de las ventanas pedidas. Todo lo demas queda intacto. Se
acepta la revision solo si mejora el riesgo contextual del MIDI completo sin
perder integridad, cobertura armonica ni resolucion narrativa. Si la llamada
falla, se excede el tiempo o no mejora, se conserva la primera partitura y se
publica como **revision musical pendiente**, con acorde, registro, pistas,
duracion y posible resolucion para que el musico la escuche. Esto no certifica
calidad artistica. La revision puede aumentar algo el tiempo y costo, pero
nunca se repite en un bucle. Una segunda partitura completa solo se pide cuando
falla el contrato de datos o la integridad MIDI, no para satisfacer a ciegas
una regla de intervalos. La uniformidad de ataques sigue siendo una advertencia
de escucha, no un veto a la musica hipnotica.

Cada candidato rechazado conserva su MIDI diagnostico, su plan armonico
(`diagnostic-XX-plan.json`) y su auditoria contextual
(`diagnostic-XX-audit.json`). La obra aceptada conserva
`composition-plan.json` y `coherence-audit.json`. Los archivos nuevos se
descargan desde la suite. Las obras viejas no tenian todo ese contexto y no
pueden reetiquetarse con certeza retrospectivamente.

Para examinar las obras guardadas sin API, desde la raiz del proyecto:

```powershell
node tools/audit-local-jobs.mjs
node tools/audit-local-jobs.mjs 0e669714-e495-44ea-9255-d20e13af3506
```

El comando es de solo lectura: no recompila, no compone y no gasta creditos.
Abrir la suite tampoco genera cargos; pulsar Componer si puede hacerlo.

Los parrafos siguientes describen el perfil editorial para obras largas o
pruebas cortas sin la opcion de coherencia, que mantienen el flujo por bloques.

Los trabajos nuevos escriben primero la voz protagonista y luego, por separado,
el colchon central de acordes, el sub y el bajo movil. Cada bloque recibe un
registro compacto de las notas MIDI reales ya aceptadas de esas voces: cada
ataque incluye todas sus alturas simultaneas y su inicio y final absolutos.
Esto evita que una muestra de notas aisladas oculte una voz del acorde. La IA
sigue eligiendo todas las notas; el registro no genera ni modifica musica.

Antes de seguir al siguiente bloque, el perfil mide los choques nuevos. Puede
pedir una reescritura focalizada de un solo instrumento; solo acepta la version
que reduce el conflicto sin perder la pista. Si persiste una segunda sostenida
en el grave del propio colchon, o un bloque conserva doce o mas choques nuevos,
detiene esa obra y conserva el checkpoint anterior. Es una proteccion para no
gastar en el resto de una obra construida sobre una armonia ya fallida; no
garantiza que la siguiente solicitud sea aprobada ni elimina el costo de los
bloques anteriores.

La revision del colchon ya no solicita una interpretacion completa de 88
compases dentro de una unica respuesta limitada. Envia hasta doce ataques
conflictivos por solicitud y la IA devuelve solo las nuevas alturas MIDI de
esos acordes; los demas ataques, controles y pistas quedan intactos. Cada
edicion se acepta solo si baja la deuda tonal medida en la obra completa, sin
introducir notas cromaticas no admitidas ni empeorar las segundas graves. El
numero de solicitudes esta acotado a cuatro por obra; no hay un bucle sin fin.
Si la obra sigue fallando, el ultimo candidato se guarda como MIDI de
diagnostico separado del checkpoint aceptado. La suite permite descargarlo y
escucharlo con una advertencia explicita de que fue rechazado.

La suite local propone por defecto una prueba corta de 32 segundos, equivalente
aproximadamente a 16 compases a 120 BPM. No se envia ninguna solicitud hasta
que el usuario pulse Componer. La opcion visible Prueba de coherencia pide a la
misma IA un reparto acotado de exactamente tres pistas: acordes, bajo y melodia;
se puede desactivar para probar cualquier instrumentacion. Solo se permite en
obras de hasta 60 segundos, y el prompt original se conserva en el historial.
En este perfil cada bloque se audita contra el
MIDI completo ya acumulado: un conflicto tonal no resuelto no puede quedar
aceptado solo porque ese bloque introdujo menos de doce eventos. Se conserva
el ultimo checkpoint anterior y se detiene antes de gastar en los siguientes
bloques. Si existe un lecho de acordes central, debe aportar ataques
polifonicos en apertura, desarrollo y cierre; puede respirar entre ellos. La
IA recibe los tramos ausentes para repararlos, y si no converge, el bloque se
rechaza temprano con MIDI diagnostico.

La auditoria tonal local conserva hasta 4096 eventos medidos, con las dos pistas,
el compas y la duracion exacta de cada superposicion. Los eventos se agrupan por
pareja de instrumentos y compas para priorizar las reparaciones. Una cifra de
superposiciones cuenta pares simultaneos, no notas individuales equivocadas.
El reparador recibe los grupos que involucran su pista y debe respetar las otras
pistas; las mejoras parciales se conservan solo si la auditoria global las valida.

Una melodia protagonista ya compuesta llega a la audicion de la obra completa
cuando tiene notas propias, cobertura y frases suficientes, aunque queden
observaciones de ventanas narrativas o pasos melodicos. Este perfil no
reescribe una pista entera solo para perseguir esos porcentajes. Una identidad
vacia, una coda ausente o una frase sin desarrollo siguen requiriendo correccion.
El bloque protagonista se diagnostica contra el MIDI realmente realizable: el
registro distingue notas sin colocacion, recortadas por fragmento, fuera de la
seccion o desviadas por un voice_map incompatible. Si una primera recuperacion
vuelve audible la pista pero deja incompleta la historia, el perfil pide una
adicion focalizada de hasta tres ventanas narrativas ausentes por solicitud,
con inicio de seccion exacto y fragmentos completos. Solo conserva las frases
si aumentan las notas MIDI realizables y resuelven al menos una ventana pedida.
La coda se pide por separado si falta, sin depender de que las otras frases ya
pasen el umbral. Una respuesta que completa todos los deficit se acepta; una
respuesta inutil se guarda como MIDI diagnostico independiente. La traza local
incluye el conteo fuente/colocacion/fragmento/seccion/voz del protagonista.

El estudio local activa `PULSO_LOCAL_EDITORIAL_V2=1` en su worker. Cloud activa
el mismo perfil mediante `PULSO_AI_EDITORIAL_V2=1` en produccion; el VST no lo
activa y conserva su flujo anterior. En modo
`ai_sovereign`, el perfil encarga como máximo dos instrumentos por bloque, presenta
a la IA el MIDI ya aceptado y deja que escriba todas las notas sin cuotas numéricas
de densidad por pista. Una pista poblada pero escasa pasa a la auditoría global como
observación musical; una identidad completamente vacía sigue requiriendo autoría.

Después de cada bloque, el auditor compara choques tonales contra el checkpoint
anterior. Si el bloque añadió al menos doce conflictos, puede pedir **una** reescritura
limitada a un instrumento implicado (máximo dos por obra) y acepta el cambio únicamente si mejora la integridad sin
alterar el MIDI previo. La reparación final recibe compases y alturas conflictivas;
puede guardar una mejora parcial y continuar, pero jamás publica una obra con
errores técnicos críticos solo porque mejoró el marcador. Si el MIDI es técnicamente
seguro, las observaciones de narrativa, textura o densidad quedan para la escucha
humana y no eliminan el resultado local. El código no añade notas ni
acompañamientos: todas las alturas y silencios aceptados son decisiones de la IA.

Este perfil aún necesita una prueba musical pagada y escuchada antes de migrarse a
Cloud o VST. Para repetir el rechazo del 30/09, usá el prompt, la duración de 180 s,
120 BPM, enfoque adaptativo y semilla `3850942983080585156` del trabajo
`1215d430-5456-49f3-86f6-3e21fbd1284c`. Abrir la suite no lanza esa prueba.

Las solicitudes **nuevas** del estudio local usan `ai_sovereign: true` por defecto;
el selector permite volver al render anterior para una comparación A/B. Este modo
convierte únicamente las células MIDI y colocaciones que escribió la IA en pistas
MIDI; no ejecuta el generador de notas de respaldo, ni añade capas, respiraciones,
continuidad o coda en el render final. Las auditorías informan problemas pero no
modifican notas. Los trabajos anteriores conservan su modo original. El worker
Cloud/VST sigue con el render estándar hasta completar la comparación A/B; el
modo se selecciona en la solicitud compartida, no mediante un compositor web aparte.
El manifiesto de una obra terminada indica `render_mode`, `all_midi_notes` y
`ai_authored_or_declared_transform_notes`. Una transformación de una célula
declarada por la IA se clasifica como autoría transformada, no como relleno local.
Cuando una obra soberana termina, el worker también exporta
`reference-standard.mid` desde **el mismo plan de IA**, sin otra llamada a la API.
Ese archivo es solo una referencia A/B de lo que habría hecho el render anterior;
`full-song.mid` continúa siendo la versión soberana y las pistas individuales
corresponden únicamente a ella.
Una partitura inválida se rechaza o queda como MIDI provisional: el modo soberano
no promete aceptar cualquier salida de la IA.
Si la protagonista ya está escrita pero carece del cierre absoluto, el worker
solicita a la IA **una coda focalizada** y la añade solo si la auditoría de esa
pista pasa sin reemplazar las notas aceptadas. En este modo no reutiliza ni
traslada automáticamente una frase previa para fingir la resolución. El intento
está acotado en tiempo y tokens; si falla, el MIDI provisional se conserva y
la solicitud termina con una causa explícita, sin una cadena de reescrituras.

Cuando la IA ya entregó material aceptado, el worker guarda `checkpoint.json` y un
`partial-NN.mid` provisional. Si una fase posterior falla, el estudio permite
descargar el último MIDI parcial con una advertencia explícita: **no es una obra
terminada ni aprobada**. La versión completa sigue publicándose solo si la
auditoría final y la exportación concluyen correctamente. Estos checkpoints no
inician solicitudes nuevas ni añaden notas procedurales; son una representación
MIDI de las células de interpretación ya recibidas.

En obras nuevas, `accepted-blueprint.json` conserva el plan IA exacto que dio
origen al checkpoint. Si una corrida falla **después** de guardar MIDI aceptado,
el estudio inicia una única recuperación automática: restaura ese plan y esas
notas, pide solo el material pendiente y vuelve a auditar. La recuperación se
muestra como una obra vinculada a la original y se limita a 10 minutos y USD
20 estimados adicionales. El importe es un límite blando: una respuesta ya
iniciada puede excederlo. Una falla antes del primer checkpoint, una clave sin
créditos o una cancelación deliberada no disparan otra composición. Los trabajos
anteriores sin blueprint exacto conservan su MIDI, pero no se reanudan
automáticamente para evitar mezclar planes distintos.

Una falta marginal de exactamente un intervalo melódico de paso ya no provoca
varias reescrituras de la pista protagonista. El compositor intenta una única
edición de altura acotada y verifica de nuevo el contrato. Si no converge, el
material pasa al control global; los déficits estructurales o mayores continúan
siendo bloqueantes. Este cambio reduce el trabajo repetido, pero **no garantiza
un costo, una duración o una calidad musical concretos**. Para validarlo con
audio y medir el ahorro hay que lanzar una composición nueva, que sí consume API.

1. Escribí el prompt, elegí duración, tempo y enfoque. Dejando la semilla vacía se genera una nueva; conservándola se facilita comparar versiones.
2. Componé una sola obra a la vez. La interfaz muestra etapa y progreso. Si existe un checkpoint exacto y ocurre una falla recuperable, se verá un único intento automático acotado; una falla anterior al primer MIDI queda informada sin gastar en un nuevo intento.
3. Al terminar, la suite abre la partitura **directamente desde los archivos MIDI**. Reproducí, pausá, detené o desplazate en la regla; usá **M** y **S** para silenciar o escuchar una pista sola. **Piano MIDI** es la escucha inicial: todas las pistas usan el mismo piano muestreado para juzgar las notas sin una orquestación de prueba. **MIDI neutro** conserva la escucha senoidal. **WAV 30 s** renderiza desde la posición actual un pasaje; **WAV completo** exporta toda la obra desde el inicio, incluidos 2,5 segundos de cola final para los releases más largos, en PCM estéreo de 44,1 kHz y 16 bits. Ambos usan el modo, volumen y Mute/Solo actuales y no llaman a la API. Podés descargar `full-song.mid` o los MIDI individuales para continuar en Ableton; también `job.json`, `manifest.json` y, para obras nuevas, `composition-plan.json`.

**Todas las notas · 1 pista MIDI** descarga un archivo MIDI tipo 0 con cada nota de la obra, su posición, duración y velocidad, más los cambios de tempo. Usa un único canal melódico para que una pista de Live pueda tocarlo con un solo instrumento; también convierte las notas que originalmente eran de batería en notas de ese instrumento. Está disponible para obras anteriores sin recomponerlas y no consume IA. Es una versión de audición: los CC, pitch bend y demás expresiones independientes de cada instrumento no pueden trasladarse fielmente a un único canal. `full-song.mid` sigue siendo el original multipista y conserva esas expresiones; agregarle una pista duplicada haría que los reproductores toquen todas las notas dos veces.
En la cabecera de ambas suites, **«1 pista MIDI»** descarga la versión para un instrumento y **«Multipista»** descarga el `full-song.mid` original. Los dos enlaces también aparecen en las acciones de una obra terminada; la versión de una pista no está disponible para un MIDI provisional o rechazado.
4. Descargá «Traza JSON» para ver cada llamada a OpenAI y cada cambio de fase. El resumen en pantalla muestra tiempos, tokens, respuestas incompletas y costo orientativo. Las llamadas que no devuelven `usage` se identifican explícitamente: sus tokens y costo no pueden inferirse con exactitud.
5. Las obras nuevas se guardan en `build/local-studio-data/jobs`, una carpeta ignorada por Git dentro del proyecto. El historial anterior de `%LOCALAPPDATA%\Pulso\LocalStudio\jobs` permanece visible en modo lectura; no se mueve ni se borra. Para usar otra ubicación escribible, definí `PULSO_LOCAL_DATA_DIR` antes de iniciar el estudio. Escuchar obras guardadas no hace solicitudes a OpenAI. Los MIDI provisionales de trabajos fallidos también pueden audicionarse, etiquetados como no aprobados.

## Cómo escuchar y probar la suite

1. Iniciá el estudio y abrí la URL local. La última obra completa con pistas se selecciona automáticamente; **Historial** permite abrir otra, incluso una provisional.
2. Presioná **Reproducir** para habilitar el audio del navegador. El navegador requiere ese gesto antes de emitir sonido. Hacé clic en la regla o en una pista para buscar un momento; el cabezal sigue el tiempo real del MIDI. **M** silencia y **S** aísla una pista; el control de volumen afecta solo la escucha local.
3. La regla, el número de compases y las notas se leen del MIDI exportado; no son clips de ejemplo ni una cuadrícula fija de ocho compases. Las pistas sin notas no aparecen. Podés comparar **Piano MIDI** con **MIDI neutro** sin alterar el archivo.
4. Para comprobar el lector MIDI sin gastar API: `node --test tests/local_studio_midi.test.mjs`. Con el servidor local abierto y al menos una obra completa guardada, `node tools/smoke-local-suite.mjs` abre Edge en modo invisible y verifica que se dibujan pistas, avanza el transporte, funciona Mute y Stop vuelve al inicio. Para juzgar la composición, compará la escucha de la suite con el MIDI abierto en Live.

Ambos modos están desacoplados de la composición: no cambian notas, duración, dinámica MIDI ni archivos exportados. Piano MIDI carga 16 muestras CC0 desde el propio sitio y transpone la más cercana para cada nota; no pretende reproducir la síntesis General MIDI exacta de Windows. El WAV se renderiza en `OfflineAudioContext`; la exportación completa trabaja en tramos de 20 segundos con 4 segundos de preroll para conservar las notas sostenidas y la reverberación en los límites, y muestra progreso. No interpreta todas las automatizaciones CC, pitch bend ni plugins externos; para una producción instrumental final, usá Ableton.

La traza contiene identificadores de respuesta, estados y uso; **no guarda la clave ni el contenido de las solicitudes internas al modelo**. `job.json` sí conserva el prompt musical que escribiste. El costo mostrado aplica la tarifa publicada para `gpt-5.6-terra` el 29/09/2026 (`$2` entrada, `$0.20` entrada en caché, `$2.50` escrituras de caché y `$12` salida por millón de tokens, con multiplicador de contexto largo cuando corresponda); es una estimación, no una factura. Fuente: https://developers.openai.com/api/docs/models/gpt-5.6-terra

Si OpenAI indica que el proyecto no tiene créditos, el estudio muestra esa causa **en la obra fallida**. El estado superior solo indica que hay una clave configurada; no comprueba el saldo ni mantiene un aviso antiguo como si fuera actual. El worker local usa exactamente la clave de entorno que comprobó el estudio, aunque el VST tenga otra guardada en el Administrador de credenciales de Windows. Los errores internos inesperados llevan un ID de diagnóstico; su detalle técnico queda en `build/local-studio-data/server-errors.jsonl` (o en `PULSO_LOCAL_DATA_DIR` si está definido), sin guardar la clave ni el cuerpo de la solicitud.

El estudio local y Cloud usan el mismo `pulso_cloud_worker`, el mismo perfil musical versionado en `web/shared/composer-profile.json` y el mismo formato de checkpoint AI con notas. En ambos, el pedido predeterminado es `ai_sovereign` + `adaptive`; Cloud acepta `seed` opcional en la API para comparaciones controladas, aunque la interfaz genera una semilla automática. El editor narrativo acotado (`PULSO_AI_GLOBAL_NARRATIVE_EDITOR`) forma parte de ese perfil compartido: revisa pasajes de las voces protagonistas con la IA, conserva la partitura aceptada si una revisión empeora la auditoría y no activa la reescritura experimental de escenas completas. Local conserva los controles avanzados para experimentar; si se elige otro enfoque o modo, ya no es la misma solicitud. Las variables experimentales locales solo se transmiten al worker cuando `PULSO_LOCAL_EXPERIMENTAL=1`; el estado `/api/status` lo declara. Sin ese modo, Local no reutiliza respuestas guardadas que Cloud no pueda reutilizar. Las dos superficies permiten hasta tres reanudaciones automáticas del checkpoint aceptado, con el mismo límite de diez minutos y USD 20 estimados por reanudación; no se reintentan solicitudes manualmente reanudadas ni errores permanentes. **La misma semilla no garantiza notas idénticas** si la IA responde distinto: se garantiza la misma ruta y el mismo contrato, no determinismo de la API. Cualquier cambio en C++ requiere recompilar el worker local y desplegar el mismo commit en Cloud antes de comparar.

## Comparación musical A/B

La obra anterior `0472bbb4-6bac-43f5-9fc7-f4617bd5bff1` sirve como referencia local. Tocá **Repetir ajustes** en su tarjeta: rellena el formulario sin enviar nada. Para evaluar el cambio de coherencia, conservá **exactamente** su prompt, duración de 390 s, 120 BPM, enfoque adaptativo y semilla `4403862266792290272`. La generación implica gasto real; no se inicia al abrir la web ni al copiar los ajustes. Al terminar, compará ambos jobs desde la raíz del repositorio:

```powershell
node tools/compare-local-jobs.mjs 0472bbb4-6bac-43f5-9fc7-f4617bd5bff1 ID_DEL_JOB_NUEVO
```

Escuchá los dos MIDI en Live con el mismo sonido y nivel, sin efectos ni instrumentos que alteren la comparación. Escuchá especialmente: si los motivos se reconocen al regresar; si el protagonista responde a la armonía y no solo se superpone; si las capas acompañan sin duplicarse; si la tensión cambia realmente y la resolución está preparada; si los pasajes escasos son respiraciones intencionales. El resumen numérico orienta la auditoría, **no certifica valor musical**. Una mejora de los indicadores sin una mejora audible no cuenta como éxito.

El nuevo generador entrega a cada bloque de escritura una muestra acotada de las notas ya aceptadas de otras pistas, con beat absoluto, altura y duración; así puede escribir relaciones musicales contra material real. El plan macro también debe justificar una pregunta armónica y su respuesta. Para pedidos sin percusión, esa exclusión se aplica desde el plan y el elenco, incluso si el prompt menciona a un artista asociado a música de club. Estas reglas influyen en la composición, pero ningún prompt puede garantizar por sí solo una obra humana: la prueba A/B sigue siendo necesaria.

## Límites deliberados

- Solo escucha en `127.0.0.1`. No publica una API de composición en la red ni expone la clave al navegador.
- No necesita cuenta, pagos ni PostgreSQL: es un banco de pruebas local, separado de la web pública.
- La escucha y el render WAV son locales al navegador. El modo piano descarga una sola vez 2,2 MB de muestras CC0 desde el mismo sitio y el navegador puede conservarlas en caché. Hay una vista previa de 30 segundos y una exportación WAV completa; esta última puede generar un archivo grande (aproximadamente 10,6 MB por minuto) y requiere que la pestaña permanezca abierta mientras renderiza.
- No dispara GitHub Actions ni modifica el despliegue del servidor.
