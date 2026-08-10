# PULSO

PULSO es un instrumento MIDI generativo que escucha las notas que recibe, entiende
el contexto armónico y produce patrones sincronizados de bajo, percusión o
contramelodía. La salida es MIDI editable; el sintetizador interno existe únicamente
como preescucha inmediata.

Este repositorio contiene un MVP funcional para Ableton Live en Windows:

- Plugin VST3 y aplicación standalone construidos con JUCE.
- Generador musical C++20 desacoplado y cubierto por pruebas.
- Tres roles: `Bass`, `Percussion` y `Countermelody`.
- Frases de 1, 2, 4, 8 o 16 compases con memoria motívica y recorrido armónico.
- Modos `Loop` y `Evolve`, con evolución gradual en cada vuelta de la frase.
- Controles musicales de repetición, complejidad, desarrollo, seguimiento, riesgo y espacio.
- Variaciones reproducibles y estado persistente en el proyecto del DAW.
- Motor de tiempo real 0.2.1: generación en worker, panic MIDI y recuperación de seek/loop.
- Preescucha con ganancia suavizada y limitador a -0.5 dBFS.
- Ayuda contextual en toda la interfaz: deja el cursor sobre cualquier elemento durante 0.35 s.
- Salida MIDI para grabar o alimentar cualquier instrumento.
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

## Primera prueba sin Ableton

El ejecutable `pulso_cli` permite comprobar el motor sin cargar un DAW:

```powershell
./build/windows-release/Release/pulso_cli.exe bass 42 0 4
./build/windows-release/Release/pulso_cli.exe drums 42 0 8
./build/windows-release/Release/pulso_cli.exe counter 42 0 4 2
```

Los argumentos son `rol semilla raíz compases evolución`. La utilidad usa una
progresión menor i–VI–iv–V para que se pueda inspeccionar la conducción armónica sin
abrir el plugin.

La aplicación standalone permite probar la interfaz y el sintetizador de preescucha.
Consulta [docs/ABLETON.md](docs/ABLETON.md) para el enrutamiento MIDI completo.

## Controles

- **Role:** tipo de interpretación generada.
- **Root / Scale:** marco tonal.
- **Phrase:** longitud completa de 1, 2, 4, 8 o 16 compases.
- **Mode:** `Loop` conserva exactamente la frase; `Evolve` introduce cambios acotados
  al empezar una nueva vuelta.
- **Repeat:** identidad rítmica y melódica que se conserva entre compases.
- **Complex:** densidad de síncopas, notas auxiliares y detalle.
- **Develop:** intensidad del arco desde presentación hasta resolución.
- **Follow:** cuánto se adapta el ritmo a las notas recibidas.
- **Risk:** probabilidad de usar movimientos menos previsibles.
- **Space:** cantidad de silencio y respiración del patrón.
- **Preview:** activa el sintetizador interno.
- **MIDI Thru:** conserva también el MIDI de entrada.
- **New Variation:** genera un patrón nuevo conservando el contexto.
- **Output:** volumen exclusivo de la preescucha; no altera la velocidad MIDI.

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

La versión 0.2 genera frases algorítmicas coherentes y locales: construye primero un
motivo, lo reinterpreta sobre la armonía de cada compás y reserva el último compás para
la resolución o el fill. No descarga modelos, no requiere cuenta y no envía audio.
Esto permite validar musicalidad e integración antes de añadir inferencia neuronal,
sidechain y escritura directa de clips.

Lee [docs/ROADMAP.md](docs/ROADMAP.md) para las siguientes etapas y
[docs/LICENSING.md](docs/LICENSING.md) antes de distribuir binarios.
