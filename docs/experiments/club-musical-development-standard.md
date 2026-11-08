# Club: desarrollo musical con propósito

## Referencia que se conserva

`1d82fc8e-6645-4876-9c68-aa50ee2d39e8`, «Étude Dmaj: Retained Pulse».
El usuario aprobó su cierre. Se conserva la obra completa y el criterio de
frase terminal conectada seguido por una liberación instrumental. La siguiente
prueba será una obra nueva: no garantiza reproducir literalmente ese cierre.

En la referencia hay 8 voces y 1.402 notas. El bajo de movimiento ataca dos
veces por compás prácticamente hasta la coda; las dos voces melódicas suman
33 y 24 notas. Esto no demuestra por sí mismo pobreza musical, pero el MIDI
permite investigar si la estabilidad de ambas capas graves deja toda la
responsabilidad de desarrollo en intervenciones superiores demasiado breves.

## Qué entendemos aquí por buena música

Es un criterio editorial para este encargo, no una definición universal ni
una garantía que pueda comprobar un contador de notas.

1. **Identidad perceptible.** Una figura tiene ritmo, contorno y articulación
   reconocibles. Su repetición construye memoria; no se castiga la hipnosis.
2. **Dirección de frase.** El comienzo conduce hacia una llegada que modifica
   la expectativa. Una sucesión de alturas correctas no basta. Duraciones,
   acentos y silencios deben contribuir a esa dirección.
3. **Diálogo real.** La segunda voz desarrolla un pensamiento identificable
   que responde a algo concreto: intervalo, acento, suspensión o destino.
   No es la protagonista troceada entre instrumentos ni una decoración aislada.
4. **Jerarquía audible.** Se entiende quién conduce y cómo los demás lo apoyan.
   Se permiten simultaneidad y contrapunto; no todas las capas deben competir
   con la misma articulación y registro.
5. **Pulso compartido.** La estabilidad métrica sostiene la escucha corporal.
   Síncopas, anticipaciones y variaciones son posibles sin alterar arbitrariamente
   la fase del ciclo o acumular ataques que parezcan tropiezos.
6. **Evolución con memoria.** Una vuelta conserva algo reconocible y puede cambiar
   su consecuencia mediante armonía, registro, disposición, duración o respuesta.
   No hace falta aplicar todos esos recursos ni cambiar constantemente.
7. **Contraste proporcionado.** La intensidad puede crecer por expectativa,
   densidad, espacio, registro o relación entre voces. Un plateau puede ser
   intencional; subir el volumen o añadir pistas no equivale a desarrollo.
8. **Coherencia armónica en el conjunto.** Importan las notas simultáneas, sus
   registros, duración y resolución, no solo pertenecer a una escala. Se respeta
   la política tonal y de cromatismos elegida por el usuario.
9. **Forma y cierre.** Las transiciones preparan algo; los retornos tienen una
   consecuencia. El final completa la frase y después libera la energía. No
   añade una tónica desconectada ni reabre una pregunta para llenar tiempo.

El objetivo es que estos aspectos funcionen al escuchar el MIDI con un sonido
neutro, sin depender de timbres impresionantes para ocultar un arreglo débil.
Una buena producción sonora puede realzarlo, pero no sustituye la composición.

## Cambio implementado

- El plan de cada escena incluye `musical_decision`: `retained_identity`,
  `development`, `relationship`, `audible_payoff`. La IA define qué conserva,
  qué transforma (o por qué mantiene un plateau), qué voces se relacionan y
  qué resultado concreto debe poder escucharse. Usa los campos de frases y
  estados ya existentes para realizar esa decisión; no es un nuevo calendario
  de notas ni una obligación de añadir pistas.
- El planificador y el escritor reciben el mismo criterio musical completo.
  Se diferencia el sub estable del bajo de movimiento: el segundo puede tener
  una llegada o respuesta deliberada sin abandonar los ataques permitidos del
  groove. La segunda melodía debe desarrollar su propio argumento conectado.
- La memoria de escena expone las últimas 12 notas anteriores de cada voz
  comprometida, aunque haya permanecido callada durante una escena completa.
  Muestra el número de notas omitidas y no llama a este fragmento una frase
  completa. Así una respuesta puede tomar como referencia MIDI real, no solo
  la etiqueta de función de la pista.
- Los planes anteriores siguen siendo válidos. El nuevo esquema exige los
  campos nuevos; el validador comprueba estructura, no decide qué música es buena.

No se altera el MIDI mediante reglas nuevas, no se rellenan silencios, no se
imponen cuotas de actividad, ni se añade una ronda de reparación por puntuación.
La memoria es de solo lectura. Todo cambio musical sigue siendo escrito por IA.
El alcance es el flujo Club con escenas conjuntas, no otros modos ni Cloud.

## Comprobación y prueba auditiva

Las pruebas comprueban la disponibilidad del criterio en planificación/escritura,
la compatibilidad de planes anteriores, la integridad del contrato nuevo y la
memoria acotada de una voz que vuelve después de un silencio. No demuestran calidad.

Se lanza desde cero el mismo encargo «etude DMAJ», 180 segundos, 120 BPM, misma
semilla y opciones. No se reutiliza el MIDI anterior ni se fuerza una reparación
de sus compases. La variabilidad del modelo impide atribuir una mejora causal
a un solo resultado; primero evaluamos el candidato y después otros encargos.

Al terminar: contrastar decisiones con notas reales, continuidad del segundo
pensamiento, papel del bajo en las transiciones, legibilidad del groove y cierre.
Escuchar especialmente el desarrollo intermedio, no solo la última cadencia.
Si hay más actividad pero menos claridad, no es un avance. Si hay menos notas
pero un argumento reconocible que llega a destino, puede serlo.

## Siguiente prueba: continuidad entre destinos

La candidata `d3a0f3fa-f072-41eb-9e2b-7cb0b99b8677` consiguió contraste de
arreglo: retiró el bajo de movimiento y preparó su regreso. Pero la voz de
respuesta pasó gran parte de los compases 41–56 sosteniendo/rearticulando Fa#.
El pulso armónico creció mientras la protagonista quedó en 19 notas. No son
errores por cantidad: el problema editorial es prometer desarrollo de frase
y realizar principalmente una llegada sostenida.

También se comprobó una limitación de contexto: `developmentBrief` solo
mostraba referencias concretas de arcos que intersectaban la ventana actual.
En esa obra los arcos estaban en la presentación y el cierre; los retornos
intermedios podían quedarse sin las referencias concretas, aunque conservaban
las descripciones de escena y los extractos del MIDI aceptado.

Ahora el escritor recibe además el último arco terminado de cada voz cuando
no tiene uno activo. No se incluyen arcos futuros ni versiones históricas
redundantes. Se etiqueta explícitamente como referencia, no como actividad
obligatoria ni plantilla para copiar notas o trasladar tiempos.

La dirección compartida distingue continuación de prolongación: cuando una
escena promete desarrollo, debe concretar un gesto relacionado, su agrupación
métrica y un destino intermedio. Una transformación sustantiva puede tener
su propio arco y referencia, sin imponer uno por sección. Un sustain largo
o un plateau siguen siendo elecciones válidas; no se introduce límite de
duración, densidad mínima ni relleno. Se conserva el criterio de cierre.

Pruebas: selección del último arco previo por voz, exclusión de arcos futuros,
prioridad del arco activo y presencia de la misma dirección en planificación
y escritura. La prueba auditiva sigue siendo una generación desde cero con
el mismo encargo y ajustes; no una edición puntual de la obra anterior.
