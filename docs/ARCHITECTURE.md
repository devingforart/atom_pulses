# Arquitectura

## Capas

```text
Host VST3 / Standalone
        │
        ▼
PulsoAudioProcessor
├── transporte y scheduling sample-accurate
├── captura de contexto MIDI
├── publicación lock-free del patrón
└── sintetizador de preescucha
        │
        ▼
pulso_core
├── MusicTypes
├── Scale
├── Random determinista
└── Generator por roles
```

`pulso_core` no depende de JUCE ni del sistema operativo. Puede reutilizarse en un
servicio neuronal, una aplicación móvil, Max o futuras versiones AU/AAX.

## Tiempo real

El callback de audio:

- No accede a red o disco.
- No espera mutexes.
- Publica patrones mediante intercambio atómico de `shared_ptr`, sin mutex explícito.
- Programa note-on y note-off en offsets de muestra calculados desde PPQ/BPM.
- Utiliza una copia acotada del MIDI de entrada.

La versión 0.1 todavía crea un nuevo patrón en el callback al cambiar de compás. El
trabajo es pequeño y determinista, pero la reserva de memoria y la especialización
estándar de `atomic<shared_ptr>` no ofrecen una garantía universal de ejecución
lock-free. Antes de incorporar redes neuronales, la generación se trasladará a un
worker con buffers preasignados.

## Modelo de sincronización

El host proporciona BPM, posición PPQ, estado de reproducción y compás. Cada patrón se
expresa en beats, no en muestras. El scheduler transforma el intervalo del bloque
actual a muestras y añade únicamente los eventos que caen dentro de ese intervalo.

Si no existe transporte —por ejemplo, en standalone— se usa un reloj interno de 120
BPM para poder probar el resultado.

## Estado

Los parámetros pertenecen a `AudioProcessorValueTreeState`. JUCE serializa ese árbol
en el estado del plugin. La semilla de variación se deriva de un contador monotónico;
una futura migración persistirá también la semilla y el patrón capturado.

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
