# PULSO

PULSO es una suite de composición MIDI. GPT propone una obra completa
con armonía, melodía, bajo y batería; un motor local valida y reproduce el resultado
sin bloquear el audio. Sin credencial o red, PULSO continúa con un compositor local
y lo identifica explícitamente como fallback.

Este repositorio contiene un MVP funcional para Ableton Live en Windows:

- Plugin VST3 y aplicación standalone construidos con JUCE.
- Generador musical C++20 desacoplado y cubierto por pruebas.
- Orquestación dinámica de hasta doce voces: batería central, dos percusiones, dos bajos,
  tres capas armónicas, lead, contramelodía, atmósfera y transiciones.
- Un `CompositionPlan` global con motivo, contorno, secciones, función armónica y tensión.
- Modo `IDEA` para frases y modo canción de 30 segundos a 30 minutos.
- Arquitectura jerárquica con secciones, ADN temático, curva dramática y cadencia final.
- Modos `Loop` y `Evolve`, con evolución gradual en cada vuelta de la frase.
- Flujo sin perillas: describir, generar, bloquear capas, regenerar, avanzar y deshacer.
- Variaciones reproducibles y estado persistente en el proyecto del DAW.
- Motor de tiempo real: generación en worker, panic MIDI y recuperación de seek/loop.
- Preescucha con cuatro drum racks sintetizados, polifonía rítmica reservada, ganancia
  suavizada y limitador a -0.5 dBFS.
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
diseña una partitura estructural compacta; el motor local la desarrolla y valida sin
enviar audio. El uso de la API puede tener coste según tu cuenta.

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

El selector inferior ofrece `DEEP CIRCUIT`, `ORGANIC ROOM`, `ANALOG PULSE` y
`CINEMATIC`. Son drum racks internos sin samples externos: cambian únicamente la
monitorización y nunca alteran las notas ni el archivo MIDI exportado.

También puedes arrastrar desde la franja inferior de la partitura: `FULL SONG` crea el
archivo multitrack completo; `RHYTHM`, `BASS`, `HARMONY` y `LEADS+FX` crean familias independientes.
Selecciona un bloque de la forma y arrastra `SECTION` para exportar sólo esa parte.
También puedes arrastrar directamente cualquiera de las doce filas. PULSO escribe una
pista MIDI por voz, tempo, compás, marcadores de sección, CC expresivos, nombres,
canales y velocidades en el archivo.

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

La versión 0.6.1 integra un arquitecto GPT de forma larga, Structured Outputs, renderizado
jerárquico de hasta 512 compases, doce voces con entrada y salida dinámica, expresión
CC, drum racks de preescucha, timeline de secciones, exportación por voz y fallback
algorítmico. La red nunca se usa desde el callback de audio.

Lee [docs/ROADMAP.md](docs/ROADMAP.md) para las siguientes etapas y
[docs/LICENSING.md](docs/LICENSING.md) antes de distribuir binarios.
