# PULSO Local Studio

Interfaz de desarrollo para probar composiciones en `http://127.0.0.1:4177` sin abrir Ableton, compilar el VST, ejecutar CMake, instalar Docker/PostgreSQL o desplegar Cloud. La interfaz invoca el **ejecutable headless existente** `build-cloud/Release/pulso_cloud_worker.exe`; ese ejecutable llama a `SongGenerationPipeline`, el mismo núcleo musical que usa la versión Cloud. No existe un segundo compositor web.

## Iniciar

Desde la raíz del repositorio, en PowerShell:

```powershell
./scripts/start-local-studio.ps1
```

La clave `OPENAI_API_KEY` debe estar configurada como variable de entorno del usuario o del proceso. El script nunca imprime su valor. Abrí `http://127.0.0.1:4177`. La página indica si el ejecutable y la clave están disponibles. **Abrir la página no envía solicitudes a OpenAI**: solo el botón «Comenzar composición» lo hace y puede generar un cargo.

Opcional: `./scripts/start-local-studio.ps1 -Port 4180`. Para usar otro worker existente, definí `PULSO_LOCAL_WORKER_PATH` antes de iniciar. El script no compila nada.

## Ciclo de trabajo

1. Escribí el prompt, elegí duración, tempo y enfoque. Dejando la semilla vacía se genera una nueva; conservándola se facilita comparar versiones.
2. Componé una sola obra a la vez. La interfaz muestra etapa y progreso. No hay reintentos automáticos desde la web.
3. Descargá `full-song.mid` o los MIDI individuales y escuchalos en Ableton. También descargá `job.json` y `manifest.json` para auditar la solicitud y el resultado.
4. Descargá «Traza JSON» para ver cada llamada a OpenAI y cada cambio de fase. El resumen en pantalla muestra tiempos, tokens, respuestas incompletas y costo orientativo. Las llamadas que no devuelven `usage` se identifican explícitamente: sus tokens y costo no pueden inferirse con exactitud.
5. El historial se conserva en `%LOCALAPPDATA%\Pulso\LocalStudio\jobs` y no se sube al repositorio ni al servidor.

La traza contiene identificadores de respuesta, estados y uso; **no guarda la clave ni el contenido de las solicitudes internas al modelo**. `job.json` sí conserva el prompt musical que escribiste. El costo mostrado aplica la tarifa publicada para `gpt-5.6-terra` el 29/09/2026 (`$2` entrada, `$0.20` entrada en caché, `$2.50` escrituras de caché y `$12` salida por millón de tokens, con multiplicador de contexto largo cuando corresponda); es una estimación, no una factura. Fuente: https://developers.openai.com/api/docs/models/gpt-5.6-terra

El ejecutable actualmente acepta `prompt`, `duration_seconds`, `bpm`, `behavior` y `seed`. Usa compás 4/4, orquestación adaptativa, variación inicial y sin contexto específico de Live, igual que el worker Cloud para esos parámetros. **La misma semilla no garantiza notas idénticas** si la IA responde distinto: guardamos los MIDI de cada ejecución como evidencia exacta. El VST usa el mismo `SongGenerationPipeline`, pero su contexto de Live y sus opciones de solicitud pueden diferir; una prueba local no es prueba de igualdad binaria con toda sesión VST. Cualquier cambio en C++ requerirá recompilar el worker para que esta página use el código nuevo; eso se hará cuando acordemos retomar compilaciones.

## Límites deliberados

- Solo escucha en `127.0.0.1`. No publica una API de composición en la red ni expone la clave al navegador.
- No necesita cuenta, pagos ni PostgreSQL: es un banco de pruebas local, separado de la web pública.
- No hay reproducción de audio dentro de la página: los resultados son MIDI para escuchar con los instrumentos de Ableton.
- No dispara GitHub Actions ni modifica el despliegue del servidor.
