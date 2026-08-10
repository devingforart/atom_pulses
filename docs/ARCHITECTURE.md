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
└── sintetizador de preescucha
        │
        ▼
pulso_core
├── MusicTypes
├── Scale
├── Random determinista
└── CompositionPlan + intérpretes por rol
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

La preescucha usa ganancia suavizada y un limitador a -0.5 dBFS. Al desactivarla o
detener el transporte, las voces completan una liberación corta para evitar clics.

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
4. groove, microtiming, dinámica y función final de retorno.

`variationIndex` transforma el plan estable; `compositionSeed` solo cambia al pedir
`New DNA`. Esto diferencia una variación reconocible de una composición nueva.

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

## Extensión neuronal prevista

El contrato futuro será un `PatternProvider` asíncrono:

```cpp
struct PatternProvider {
    virtual void submit(GenerationContext) = 0;
    virtual std::shared_ptr<const Pattern> latest() const = 0;
};
```

El proveedor algorítmico actual y uno ONNX/MLX podrán intercambiarse sin modificar el
scheduler ni la interfaz del plugin.
