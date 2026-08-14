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

## Alcance de 0.25

Incluye composición simbólica mediante GPT, salida estructurada validada, fallback
local, armonía exportable, locks exactos por capa, regeneración selectiva, Undo,
persistencia de la partitura completa, canciones de hasta 30 minutos, forma jerárquica,
quince roles compositivos realizados por una orquesta dinámica de 12–36 instrumentos,
preview multitimbral con ocho mundos sonoros,
asignación instrumental automática por IA y reemplazo manual independiente de cada parte,
automatización expresiva, timeline de secciones,
arrastre MIDI por voz o familia, SOLO/MUTE no destructivo, transporte y preescucha segura.
La tonalidad declarada es un contrato: metadatos, motivo, armonía, bajos y melodías se
reconcilian antes de publicar o exportar cualquier composición.
No incluye todavía sidechain de audio ni conocimiento general de otras pistas.

## Contrato de coherencia

Una salida no se considera canción sólo porque dura muchos compases. PULSO aplica
reglas observables:

1. **Identidad:** un motivo base reconocible reaparece durante la frase.
2. **Armonía:** los tiempos fuertes reflejan el acorde capturado para ese compás.
3. **Conducción:** el registro se mantiene estable y evita saltos arbitrarios.
4. **Desarrollo:** la densidad y las alteraciones crecen de forma acotada hacia el final.
5. **Resolución:** bajo y contramelodía preparan el retorno; batería construye un fill.
6. **Interacción:** cada voz declara una función, registro e interacción; bajo, ritmo,
   armonía y melodía derivan del mismo plan sin duplicarse mecánicamente.
7. **Linaje:** las capas bloqueadas permanecen exactas; únicamente las desbloqueadas cambian.
8. **Forma:** cada sección presenta, contrasta, desarrolla, culmina o resuelve el mismo ADN.
9. **Orquestación:** las partes entran y salen según la función de la sección; la melodía
   rota de hablante y densidad nunca significa que toda la plantilla deba sonar simultáneamente.
10. **Respiración:** lead, acompañamiento, bajo y ritmo contienen ausencias coordinadas;
    cada frase reserva espacio antes de volver a afirmar el motivo.
11. **Contrato tonal:** los apoyos estructurales pertenecen al acorde y a la escala; sólo
    sobrevive el cromatismo breve, débil, preparado y resuelto por semitono.
12. **Compatibilidad vertical:** se corrigen choques no intencionales y las notas de soporte
    incompatibles terminan antes del siguiente cambio armónico.
13. **Identidad rítmica:** cada sección declara un estado del kick y las excepciones son
    gestos con posición y propósito, nunca bajas accidentales producidas por aleatoriedad.
14. **MIDI productivo:** kick, clap/snare, closed hats, open hats/shaker y ambas percusiones
    conservan identidades independientes para edición y arrastre en Ableton.
15. **Desarrollo rítmico:** las secciones heredan celdas reconocibles y las transforman
    con cambios escasos y motivados, en vez de sustituirlas por patrones inconexos.
16. **Profundidad orquestal:** las familias armónicas no son copias de una única pista;
    cada instrumento declara función, tesitura, articulación, divisi, entradas y retiradas.
17. **Crítica instrumental:** el MIDI realizado debe respetar polifonía ejecutable,
    separación grave, balance de familias y expresión independiente antes de publicarse.

18. **Producción electrónica:** cuando la intención sea de club, la complejidad proviene de
    groove, timbre, automatización, tensión y sustracción. Kick y bajo comparten un contrato
    temporal, solamente un hook posee el primer plano y la orquesta acústica deja de ser el
    comportamiento predeterminado.
19. **Correspondencia de intención:** el crítico compara el dominio solicitado con las partes
    realizadas; una producción electrónica no puede aprobarse silenciosamente como concierto
    de cuerdas y vientos.

La reproducción repite el resultado de manera exacta hasta que el usuario solicita
otra idea o regenera las capas desbloqueadas.

## Criterios para validar el MVP

- Un productor obtiene un patrón útil en menos de treinta segundos.
- Regenerar con melodía bloqueada conserva cada evento melódico exactamente.
- El patrón permanece estable mientras se edita o graba.
- La frase permanece idéntica mientras se escucha, graba o exporta.
- La sesión vuelve a abrir con los mismos parámetros.
- El audio nunca se interrumpe al generar una variante.
