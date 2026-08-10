# Definición del producto

## Promesa

PULSO convierte contexto musical en una composición MIDI coordinada y editable. Puede
interpretar el ensemble completo o aislar un rol; las decisiones finales permanecen en
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
8. Explicar cada decisión en contexto: ningún control depende de consultar un manual externo.

## Alcance de 0.4

Incluye composición simbólica mediante GPT, salida estructurada validada, fallback
local, armonía exportable, locks exactos por capa, regeneración selectiva, Undo,
persistencia de la partitura completa, arrastre MIDI, transporte y preescucha segura.
No incluye todavía sidechain de audio ni conocimiento general de otras pistas.

## Contrato de coherencia

Una salida no se considera frase solo porque dura varios compases. PULSO aplica siete
reglas observables:

1. **Identidad:** un motivo base reconocible reaparece durante la frase.
2. **Armonía:** los tiempos fuertes reflejan el acorde capturado para ese compás.
3. **Conducción:** el registro se mantiene estable y evita saltos arbitrarios.
4. **Desarrollo:** la densidad y las alteraciones crecen de forma acotada hacia el final.
5. **Resolución:** bajo y contramelodía preparan el retorno; batería construye un fill.
6. **Interacción:** bajo, batería y melodía derivan del mismo plan y ocupan funciones distintas.
7. **Linaje:** las capas bloqueadas permanecen exactas; únicamente las desbloqueadas cambian.

La reproducción repite el resultado de manera exacta hasta que el usuario solicita
otra idea o regenera las capas desbloqueadas.

## Criterios para validar el MVP

- Un productor obtiene un patrón útil en menos de treinta segundos.
- Regenerar con melodía bloqueada conserva cada evento melódico exactamente.
- El patrón permanece estable mientras se edita o graba.
- La frase permanece idéntica mientras se escucha, graba o exporta.
- La sesión vuelve a abrir con los mismos parámetros.
- El audio nunca se interrumpe al generar una variante.
