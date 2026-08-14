# Desarrollo

## Configuración rápida

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

La primera configuración descarga JUCE 9.0.0 mediante CMake FetchContent. La versión
queda fijada en `CMakeLists.txt`; no se sigue una rama móvil.

Si PowerShell aplica una política que bloquea scripts locales, ejecútalos con
`powershell -ExecutionPolicy Bypass -File scripts/build.ps1`; la excepción solo afecta
ese proceso y no cambia la política del sistema.

Para trabajar únicamente en el motor, sin descargar JUCE:

```powershell
cmake -S . -B build/core -DPULSO_BUILD_PLUGIN=OFF
cmake --build build/core --config Debug
ctest --test-dir build/core -C Debug --output-on-failure
```

## Objetivos

- `pulso_core`: biblioteca musical portable.
- `pulso_tests`: pruebas sin frameworks externos.
- `pulso_cli`: inspección textual de patrones.
- `Pulso_VST3`: plugin para el DAW.
- `Pulso_Standalone`: host de prueba.

## Convenciones

- C++20 y formato definido en `.clang-format`.
- Todo dato musical del core se expresa en beats y números MIDI.
- Los parámetros públicos conservan su identificador para no romper proyectos.
- El hilo de audio no puede realizar I/O, bloquearse ni invocar inferencia pesada.
- La aleatoriedad siempre recibe una semilla explícita.

## Añadir un rol

1. Agrega el valor a `Role` y `roleNames`.
2. Implementa el generador en `Generator`.
3. Añade la opción al parámetro `role` y a la interfaz.
4. Añade invariantes y determinismo a `GeneratorTests.cpp`.
5. Documenta registro, canal y significado musical.

## Validación manual

Además de las pruebas unitarias:

1. Ejecuta `pulso_cli` dos veces con la misma semilla y compara la salida.
2. Abre standalone y comprueba cada rol.
3. Ejecuta el validador VST3 incluido por Steinberg/JUCE si está disponible.
4. Prueba Live a 44.1/48/96 kHz y buffers de 64, 256 y 1024 muestras.
5. Guarda, cierra y vuelve a abrir un Live Set.

## Diagnóstico de OpenAI

La credencial debe existir como `OPENAI_API_KEY` en el entorno de usuario de Windows; una
variable disponible solamente en la consola de desarrollo no llega a Ableton. Configúrala con
`scripts/configure-openai.ps1` y reinicia el DAW. Las pruebas opcionales de integración real son:

```powershell
./build/windows-release/Release/pulso_processor_tests.exe --live-cancel
./build/windows-release/Release/pulso_processor_tests.exe --live-ai
```

La primera exige cancelación en menos de cuatro segundos. La segunda valida autenticación,
Responses API, Structured Outputs, parseo y contrato de orquestación dinámica.

La arquitectura y la crítica de una canción completa se ejecutan con Responses API en
background. PULSO consulta el ID cada dos segundos mediante conexiones breves, permite
cancelación inmediata también en el servidor y reserva hasta doce minutos para arquitectura
más cuatro minutos para crítica. El hilo de audio y la interfaz nunca esperan esas conexiones.

## Invariantes de producción

Las pruebas de core deben conservar simultáneamente:

- cero defectos después de `TonalContract` en política consolidada;
- ataques en rejilla de semicorchea salvo `MetricIntent` explícito;
- note-offs en rejilla fina y sin retriggers solapados por parte/canal/pitch;
- rachas literales y expresión densa informadas como advertencias, no como corrupción;
- menos de doce eventos expresivos por nota como objetivo después de compactación;
- `Pattern::productionReady == true` para toda canción publicable.
- para `ClubElectronic`, el cast final usa roles electrónicos, las colisiones kick-bajo no
  aumentan después del director y el informe incluye automatización estructural;
- cada sección electrónica tiene como máximo un propietario melódico de primer plano.

`LiveDeployer` permite patrones legacy sin auditoría para compatibilidad de estados
anteriores, pero rechaza cualquier patrón nuevo que haya sido auditado y no esté listo.
