# PULSO Local Studio

Suite local de composición y escucha en `http://127.0.0.1:4177`, sin abrir Ableton, compilar el VST, ejecutar CMake, instalar Docker/PostgreSQL o desplegar Cloud. La interfaz invoca el **ejecutable headless existente** `build-cloud/Release/pulso_cloud_worker.exe`; ese ejecutable llama a `SongGenerationPipeline`, el mismo núcleo musical que usa la versión Cloud. No existe un segundo compositor web.

## Iniciar

Desde la raíz del repositorio, en PowerShell:

```powershell
./scripts/start-local-studio.ps1
```

La clave `OPENAI_API_KEY` debe estar configurada como variable de entorno del usuario o del proceso. El script nunca imprime su valor. Abrí `http://127.0.0.1:4177`. La página indica si el ejecutable y la clave están disponibles. **Abrir la página no envía solicitudes a OpenAI**: solo el botón «Comenzar composición» lo hace y puede generar un cargo.

Opcional: `./scripts/start-local-studio.ps1 -Port 4180`. Para usar otro worker existente, definí `PULSO_LOCAL_WORKER_PATH` antes de iniciar. El script no compila nada.

## Ciclo de trabajo

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

Una falta marginal de exactamente un intervalo melódico de paso ya no provoca
varias reescrituras de la pista protagonista. El compositor intenta una única
edición de altura acotada y verifica de nuevo el contrato. Si no converge, el
material pasa al control global; los déficits estructurales o mayores continúan
siendo bloqueantes. Este cambio reduce el trabajo repetido, pero **no garantiza
un costo, una duración o una calidad musical concretos**. Para validarlo con
audio y medir el ahorro hay que lanzar una composición nueva, que sí consume API.

1. Escribí el prompt, elegí duración, tempo y enfoque. Dejando la semilla vacía se genera una nueva; conservándola se facilita comparar versiones.
2. Componé una sola obra a la vez. La interfaz muestra etapa y progreso. No hay reintentos automáticos desde la web.
3. Al terminar, la suite abre la partitura de las pistas **directamente desde los archivos MIDI**. Reproducí, pausá, detené o desplazate en la regla; usá **M** y **S** para silenciar o escuchar una pista sola. Web Audio usa **una misma onda senoidal polifónica para todas las notas tonales**, sin presets por instrumento, efectos ni desafinación; la percusión se representa con pulsos senoidales breves. Esto facilita juzgar alturas, armonía, ritmo y simultaneidad sin que el timbre maquille la obra. **No es un render final ni reemplaza tus instrumentos de producción**. Podés descargar `full-song.mid` o los MIDI individuales para continuar en Ableton. También podés descargar `job.json`, `manifest.json` y, para obras nuevas, `composition-plan.json`.
4. Descargá «Traza JSON» para ver cada llamada a OpenAI y cada cambio de fase. El resumen en pantalla muestra tiempos, tokens, respuestas incompletas y costo orientativo. Las llamadas que no devuelven `usage` se identifican explícitamente: sus tokens y costo no pueden inferirse con exactitud.
5. El historial se conserva en `%LOCALAPPDATA%\Pulso\LocalStudio\jobs` y no se sube al repositorio ni al servidor. Escuchar obras guardadas no hace solicitudes a OpenAI. Los MIDI provisionales de trabajos fallidos también pueden audicionarse, etiquetados como no aprobados.

## Cómo escuchar y probar la suite

1. Iniciá el estudio y abrí la URL local. La última obra completa con pistas se selecciona automáticamente; **Historial** permite abrir otra, incluso una provisional.
2. Presioná **Reproducir** para habilitar el audio del navegador. El navegador requiere ese gesto antes de emitir sonido. Hacé clic en la regla o en una pista para buscar un momento; el cabezal sigue el tiempo real del MIDI. **M** silencia y **S** aísla una pista; el control de volumen afecta solo la escucha local.
3. La regla, el número de compases y las notas se leen del MIDI exportado; no son clips de ejemplo ni una cuadrícula fija de ocho compases. Las pistas sin notas no aparecen. Los metadatos del exportador se usan para reconocer la percusión y colorear las pistas, **no para cambiar el timbre de las notas tonales**.
4. Para comprobar el lector MIDI sin gastar API: `node --test tests/local_studio_midi.test.mjs`. Con el servidor local abierto y al menos una obra completa guardada, `node tools/smoke-local-suite.mjs` abre Edge en modo invisible y verifica que se dibujan pistas, avanza el transporte, funciona Mute y Stop vuelve al inicio. Para juzgar la composición, compará la escucha de la suite con el MIDI abierto en Live.

El monitor senoidal está deliberadamente desacoplado de la composición: no cambia notas, duración, dinámica MIDI ni archivos exportados. Usa ataques y finales de pocos milisegundos para evitar clics, y un limitador de seguridad para acumulaciones extremas. No reproduce todavía todas las automatizaciones CC, pitch bend o plugins externos; usá Ableton para juzgar timbre y mezcla finales.

La traza contiene identificadores de respuesta, estados y uso; **no guarda la clave ni el contenido de las solicitudes internas al modelo**. `job.json` sí conserva el prompt musical que escribiste. El costo mostrado aplica la tarifa publicada para `gpt-5.6-terra` el 29/09/2026 (`$2` entrada, `$0.20` entrada en caché, `$2.50` escrituras de caché y `$12` salida por millón de tokens, con multiplicador de contexto largo cuando corresponda); es una estimación, no una factura. Fuente: https://developers.openai.com/api/docs/models/gpt-5.6-terra

Si OpenAI indica que el proyecto no tiene créditos, el estudio muestra esa causa **en la obra fallida**. El estado superior solo indica que hay una clave configurada; no comprueba el saldo ni mantiene un aviso antiguo como si fuera actual. El worker local usa exactamente la clave de entorno que comprobó el estudio, aunque el VST tenga otra guardada en el Administrador de credenciales de Windows. Los errores internos inesperados llevan un ID de diagnóstico; su detalle técnico queda solo en `%LOCALAPPDATA%\Pulso\LocalStudio\server-errors.jsonl`, sin guardar la clave ni el cuerpo de la solicitud.

El ejecutable actualmente acepta `prompt`, `duration_seconds`, `bpm`, `behavior` y `seed`. Usa compás 4/4, orquestación adaptativa, variación inicial y sin contexto específico de Live, igual que el worker Cloud para esos parámetros. **La misma semilla no garantiza notas idénticas** si la IA responde distinto: guardamos los MIDI de cada ejecución como evidencia exacta. El VST usa el mismo `SongGenerationPipeline`, pero su contexto de Live y sus opciones de solicitud pueden diferir; una prueba local no es prueba de igualdad binaria con toda sesión VST. Cualquier cambio en C++ requiere recompilar el worker antes de probarlo desde esta página.

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
- La escucha senoidal es local al navegador y no requiere descargar librerías ni muestras de terceros. Es una referencia musical, no un render de producción.
- No dispara GitHub Actions ni modifica el despliegue del servidor.
