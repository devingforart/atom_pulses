# Definición del producto

## Promesa

PULSO convierte contexto musical en interpretaciones editables. No genera una canción
terminada: ocupa un rol limitado dentro de la sesión y deja las decisiones finales en
manos del productor.

## Usuario inicial

Productores de electrónica, pop y hip-hop que trabajan en Ableton Live y necesitan
obtener rápidamente una base, percusión complementaria o contramelodía sin abandonar
el flujo de la sesión.

## Trabajo principal

> Mientras reproduzco acordes o una idea rítmica, quiero escuchar alternativas que
> encajen con la sesión y poder grabarlas como MIDI para editarlas.

## Principios

1. Escuchar antes de generar.
2. Mantener un rol musical explícito.
3. Ofrecer restricciones comprensibles, no parámetros de modelo.
4. Entregar material editable.
5. Ser seguro para directo: ningún trabajo bloqueante en el hilo de audio.
6. Funcionar localmente por defecto.
7. Mantener resultados reproducibles dentro de una sesión.

## Alcance de 0.1

Incluye sincronización al transporte, análisis de acorde por notas sostenidas,
generación por compás, salida MIDI, preescucha y tres roles. No incluye todavía
sidechain de audio, modelos neuronales, conocimiento de otras pistas ni escritura
automática de clips.

## Criterios para validar el MVP

- Un productor obtiene un patrón útil en menos de treinta segundos.
- Cambiar `Risk` o `Space` produce una diferencia musical reconocible.
- El patrón permanece estable mientras se edita o graba.
- La variación ocurre únicamente al pedirla o al entrar en un compás nuevo.
- La sesión vuelve a abrir con los mismos parámetros.
- El audio nunca se interrumpe al generar una variante.

