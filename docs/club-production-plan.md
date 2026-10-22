# Club por capas: experimentos locales v3 y v4

## Alcance

La base Adaptativo se conserva. Club tiene un plan estructurado propio en lugar
de convertir obligatoriamente una trama de hablante/respuesta en música electrónica.
No hay despliegue Cloud, cambio de modelo, instalación de VST ni commit en esta prueba.

`AiClubProductionPlan.h` contiene esquema, validación, contexto de escritura y
medición de cumplimiento. El plan se guarda en el campo existente de arquitectura
para mantener checkpoints y se exporta también como `club-production-plan.json`.
Las arquitecturas de frases antiguas siguen siendo legibles para reanudar históricos.

## v4: núcleo antes que apoyo y fase verificable

El candidato v3 `9ff7ba3d-3def-4390-b3df-a192a94884ad` fue rechazado en escucha.
No es un anclaje aprobado: su pulso armónico cambia de contratiempo de corchea a
semicorchea en los compases 17–24. Todos los ataques están cuantizados, pero eso
no constituye una identidad métrica estable. El MIDI exportado coincide exactamente
con las notas del checkpoint: no es un desplazamiento de exportación.

El plan nuevo pide `groove_core_ids`, de una a cuatro capas elegidas por la IA.
Su `rhythm_cell`, `cycle_beats` y `cycle_origin_beat` son decisiones de la IA y
permanecen como contrato de fase. Puede omitir ataques, cambiar alturas, gates,
voicings y dinámica; no puede deslizar la base en cada ventana. Los campos antiguos
siguen siendo legibles sin inventar una base para históricos.

El escritor agrupa primero ese núcleo; escribe después los apoyos contra el MIDI
aceptado de la misma ventana. Ninguna nota se duplica, crea, estira o mueve por esta
agrupación. Antes de solicitar notas, el esquema limita los ataques del núcleo a
su propia fase absoluta; el parser también la valida. Un ataque inválido pide
reescritura IA, no cuantización ni desplazamiento local. Las capas no centrales
conservan libertad rítmica. La auditoría de la Suite expone `core_phase_mismatch`
incluso cuando todas las notas están en la rejilla de semicorcheas.

Ensayo inicial desde cero: `08fe6038-c249-46db-b06f-6e74c6311e05`, 32 compases / 64 s /
120 BPM / Re menor, ocho funciones explícitas: bajo, pulso de acordes, secuencia,
colchón, pad superior, stabs, hook y textura. Semilla `202610220224`.
Se prueba el conjunto completo y su núcleo en escucha separada; no se presenta
como éxito por tener cero errores de fase o un número alto de notas.

Este ensayo se canceló deliberadamente al descubrir otra incompatibilidad del
contrato macro: imponía tres secciones mínimas y tres actos aun para una sola
sección de producción. La IA devolvió un estado de 32 compases y dos marcadores
de cero compases; el saneamiento heredado los convertía en secciones reales y
acortaba la principal. No se presenta ese resultado. El esquema macro Club admite
ahora desde un estado y prohíbe duraciones cero. Si el usuario pide explícitamente
una sección plena, solicita una única sección con la duración exacta. Adaptativo
conserva su esquema anterior. Esto corrige el contrato ANTES de la llamada; no
reestructura ni estira MIDI recibido.

La siguiente corrida, `47a8e060-a7e3-44b1-bfdd-84fafa351dd1`, falló antes del MIDI:
el reparto inventó cuatro IDs de sección en `active_sections` que no coincidían
con la única sección real del macro. El esquema del reparto Club ahora enumera
los nombres exactos del macro. También se corrige el acumulador de compases de
la auditoría provisional, que arrancaba con la longitud demo de `SongPlan`.
No se desactivó la revisión de cobertura ni se rellenaron capas ausentes.

`b069fd19-c0fc-483c-851e-dc80c5cc66fb` expuso otra contradicción del reparto:
el pad se describía como segundo cuerpo, pero declaraba función `color` en vez
de `body`, y la textura se clasificaba como `transitions`. El contrato pide ahora
esa responsabilidad sostenida explícitamente y la recuperación explica qué
propietarios no califican. `75d84c66-0b85-42a4-b242-1812eeeec1f7` pasó el reparto
y falló en el lector, que todavía exigía tres secciones. Se añade prueba del lector
real con una sección Club y se mantiene el mínimo anterior para Adaptativo.

### Primera realización v4 y revisión focalizada

`7b6b1982-532d-4f03-b5ce-70e2f61f6d0d`: «Profundidad de Re — Sección plena».
La IA eligió como núcleo `propulsive_bass`, `chord_pulse` y `arpeggiated_sequence`.
En los 32 compases, el bajo usa 0/1.5/3, el pulso 1 y la secuencia 0.5/2.5, en
beats locales de cada compás. Son decisiones de ESTA planificación IA, no patrones
codificados. La fase del núcleo se contrasta con sus ciclos de ocho beats.
Cuatro ventanas, dos grupos por ventana; 412 notas, sin recuperación de escritura.
El control final detectó la textura incompleta (tres señales breves).

`4f8ae245-b458-4bda-b14d-f693da33e2ae`: la IA revisó sólo esa capa y devolvió
cuatro entradas. Pasó a 413 notas y ocho voces desarrolladas. Esta corrección no
convierte el halo en una textura densa: continúa siendo una capa episódica, de
registro fijo, según la intención del plan. No se presenta densidad estadística
como prueba de riqueza musical.

Se incorpora al flujo Club una revisión acotada de apoyos realmente incompletos,
sin reabrir el núcleo. En esta primera implementación solicita secciones de hasta
32 compases y un presupuesto máximo de tres minutos; no afirma completar arbitrariamente
obras largas ni garantiza el arreglo entero. Sólo acepta MIDI escrito por IA que
mejora el desarrollo medido, conserva intactos los demás propietarios y no aumenta
choques sostenidos ni duraciones inválidas. La calidad sigue requiriendo escucha.

La auditoría independiente del MIDI `8c50b561e0f6ff75e5c1a3d6bd31ca1fdd357b4942b7e9602efc6472bec58df6`
dio `ready_for_human_listening`, con una reserva: seis entradas de pad se adelantan
una semicorchea a la misma altura de la secuencia; hay además un par secuencia/hook.
No es disonancia ni error de cuantización, sino posible doble ataque perceptivo.
Se añade evidencia `retrigger_mask` para que el editor IA pueda revocear apoyos
sostenidos expuestos inmediatamente antes de una voz rítmica; no desplaza ataques
ni prohíbe unísonos simultáneos. Se prueba en `7d08b168-c84a-47b9-a130-ed6e5a5293ac`.

**Candidato v4 para escuchar:** [Profundidad de Re — Sección plena](http://localhost:4214/?job=7d08b168-c84a-47b9-a130-ed6e5a5293ac).
32 compases / 1:04 / ocho pistas / 413 notas de IA. Contrato final normal listo,
ocho voces desarrolladas y continuidad armónica 1.0. Cero ataques del núcleo
fuera de su fase, cero solapamientos de misma altura dentro de pista, cero
semitonos/novena menor compuesta sostenidos durante un beat o más en la auditoría.

La revisión final cambió únicamente seis alturas del pad superior: los otros siete
MIDI por pista son idénticos byte a byte a la versión de 413 notas anterior.
Los seis pares pad/secuencia cercanos desaparecen; permanece un par secuencia/hook
en beat 24.5/24.75 (compás 7), que necesita valoración de escucha, no una prohibición
automática de intercambios entre melodías. No se alteraron ataques ni duraciones.
El checkpoint y el archivo exportado contienen ahora las mismas 413 notas.
MIDI SHA-256: `ec13fc3ead96d72f8b19d9b773a64c3b6a505b5da49372d536e1e5e81db11659`.
Este candidato aún no tiene aprobación del usuario ni demuestra un desarrollo
de cinco minutos. El núcleo estable es un avance comprobable, no garantía de groove.
La segunda revisión IA del MIDI final devolvió `ready_for_human_listening`, sin
incidencias mayores y conservando como punto de escucha ese unísono secuencia/hook
del compás 7. El informe `club-ai-review.json` guarda el hash del archivo revisado;
su nomenclatura de octavas/voicings puede equivocarse y no reemplaza la escucha.

### Integridad al reanudar

La revisión de una capa nueva reveló un problema técnico: el parser de notas
directas dejaba el límite del clip sin inicializar hasta la normalización final.
La exportación completa incluía las notas, pero el checkpoint inmediato podía
omitirlas. Ahora el parser fija el contenedor al largo exacto de la ventana, sin
cambiar notas. La Suite también puede recuperar una obra terminada desde el MIDI
final si su checkpoint provisional es anterior, validando reparto, nombres y número
de notas. Una obra completada sin nuevo checkpoint parcial sigue siendo reanudable.
Las pruebas `fe245ba3-c634-4d47-b93c-ab051bfbf87c` y
`2dc62e4e-d679-45ae-86cc-67116e74caa8` conservaron exactamente las 413 notas y el
MIDI byte a byte, sin llamadas API adicionales. No se regeneró música para repararlo.

## Decisiones de la IA

- `format`: sección continua o arreglo completo. Una sección no recibe cadencia,
  introducción, bajada y outro por el simple hecho de tener una duración finita.
- Foco opcional: puede ser una secuencia, bajo, textura o hook. El identificador
  de protagonista del macro/reparto permanece por compatibilidad de datos; el
  escritor Club nuevo no lo convierte en una obligación de solo o diálogo.
- Por capa: propietario exacto, responsabilidad libre, ciclo, fase absoluta,
  gramática de ataques, registro, articulación, polifonía y relación con el conjunto.
- Por sección/capa: activa, reducida, sostenida o ausente, material conservado,
  evolución y estimación de actividad elegida por la IA. El campo histórico interno
  `minimum_sounding_fraction` se conserva, pero es evidencia orientativa, no cuota.

No se proporcionan patrones fijos de alturas o ataques. La escritura devuelve
todas las notas y repeticiones explícitamente. Las células/colocaciones del motor
existente conservan los eventos; no se añadió un expansor generativo de loops.

## Controles

La validación del plan comprueba IDs únicos, correspondencia exacta con el reparto,
referencias de sección, permisos de actividad, ciclos/registros y coordenadas válidas.
La escritura mide el MIDI real contra las intenciones de la IA en ventanas con un
ciclo completo. Sólo una capa prometida completamente omitida, la polifonía prometida
pero ausente o una retirada contradicha requieren recuperación automática. Las
diferencias numéricas de cobertura son orientativas: no fuerzan notas ni duraciones.
Si hay una contradicción, pide a la IA revisar sólo las capas afectadas, conservando
las otras como contexto MIDI inmutable. No inserta notas, alarga sostenidos ni
cuantiza una respuesta inválida. Es un control de cumplimiento, no una puntuación
de gusto musical ni una garantía de calidad profesional. Las ausencias declaradas
se codifican también como arrays vacíos en la solicitud. El esquema de notas vincula
ataque y duración a la ventana técnica antes de generar; nunca recorta notas recibidas.

Club escribe hasta doce capas juntas en ventanas técnicas de ocho compases,
con fase absoluta y memoria de notas anteriores. Adaptativo mantiene sus ventanas
y agrupación. El límite de doce corresponde al experimento de ensamble existente;
el esquema de capas soporta hasta 64, pero eso no afirma que una solicitud de 64
instrumentos use hoy la misma llamada conjunta. La generalización de reparto de
solicitudes grandes queda fuera de esta audición de doce pistas.

La Suite ofrece «Plan de producción Club» y un contraste de capas/MIDI; no muestra
un número ficticio de diálogos. Los informes son orientativos y la escucha decide.

## Prueba inicial

Trabajo Auto `67489261-54a2-4d76-9c40-4fe2383c7d8d`.
32 compases / 4/4 / 120 BPM / 64 segundos / Re menor / sin cromatismos ni percusión.
Doce funciones pedidas: sub, bajo propulsivo, colchón polifónico, pad superior,
dos arpegios/secuencias, stabs, pulso de acordes, hook, contramelodía y dos texturas.
Se solicita una sección plena, sin introducción, bajada ni cierre.
La primera pasada evita editores melódicos generales, pero mantiene integridad y
cumplimiento de las promesas del plan de capas. El contrato final distingue ahora
una sección continua de una canción completa: no exige clímax ni cadencia final a
un cuerpo explícitamente repetible. Mantiene desarrollo de voces y continuidad
armónica. No es un bypass general de la revisión.

Archivos de evidencia: MIDI por pista, `club-production-plan.json`,
`club-production-audit.txt`, `phrase-audit.json` (tipo `club_production_v1`),
informes generales y revisión IA opcional independiente del MIDI completo.

## Evidencia del ensayo y revisión

- Primera escritura completa: `ec7c1d08-18e6-492f-b2e8-71f7c93f8a96`.
  Doce capas, 770 notas directas de IA; seis llamadas, aproximadamente 293 segundos.
  Se conservó el blueprint aceptado del ensayo inicial; esos tiempos no incluyen
  el coste de diseñar dicho blueprint. Las cuatro ventanas MIDI salieron sin reintento.
- Revisión controlada: `d7a907ae-96dc-44ea-8eb3-9291502f5721`.
  Revisión adicional: `5d11349b-9285-4019-9147-87af73b08903`.
  Esta última conserva las 770 notas, pasa el contrato final no orientativo y
  elimina los semitonos contiguos sostenidos detectados. Su MIDI SHA-256 es
  `ca29097cecef48e95231e2cee81412923f53f3055edb03fa81ad2b2b036cd2e7`.
  No equivale todavía a aprobación de escucha: la revisión independiente señaló
  fricciones de novena menor separadas por octavas que el primer detector omitía.
- Se amplió la evidencia de revisión a novenas menores compuestas sostenidas,
  sin incluir séptimas menores, novenas mayores ni pasos breves. La IA decide las
  alturas; el control no convierte intervalos en notas de reemplazo.
- La crítica IA no es infalible: llamó novena menor al intervalo MIDI 62–84,
  que es una séptima menor compuesta. Esa recomendación se descarta; no se
  automatiza una corrección por la prosa del crítico sin contrastar sus notas.

La revisión sólo puede editar alturas concretas de planos sostenidos. Conserva
todos los ataques, duraciones y velocidades de las capas rítmicas, hook y respuesta.
La bisagra cíclica tiene un editor separado: la IA sólo puede extender el cuerpo
del primer/último acorde de la capa central hacia el límite de la sección; no mueve
sus finales/interiores, no añade notas ni cambia alturas. Una propuesta sólo se
acepta si conserva notas e integridad, reduce el problema observado y no aumenta
los choques internos ni las disonancias sostenidas contra ventanas armónicas.

### Candidato para escucha

Trabajo Auto **`9ff7ba3d-3def-4390-b3df-a192a94884ad`**, «Fase Abisal — Sección
Plena en Re menor», en [el estudio local](http://localhost:4214/?job=9ff7ba3d-3def-4390-b3df-a192a94884ad).
Parte del MIDI aceptado, no de otra regeneración aleatoria. Cinco llamadas focalizadas
de alturas, aproximadamente 92 segundos. El control final normal informa `ready=true`,
`advisory_only=false`, doce voces desarrolladas y ninguna incidencia en ese contrato.

Lectura independiente de los archivos MIDI: 770 notas directas de IA, cero notas
de otra procedencia; doce pistas con notas, ningún compás totalmente vacío, colchón
armónico durante los 32 compases; dos planos sostenidos en el 75% de los compases.
No se detectan semitonos contiguos ni novenas menores compuestas sostenidas durante
un beat o más. Esto no afirma que no existan otras tensiones/pasos o que el groove
ya tenga aprobación humana. El auditor tonal general todavía informa ocho sostenidos
no acordales según su clasificación; no fueron borrados ni retocados algorítmicamente.

Los MIDI de sub, bajo, ambas secuencias, stabs, pulso de acordes, hook y contramelodía
son idénticos byte a byte a la primera escritura completa. En pad superior y texturas
sólo cambian alturas; sus ataques, duraciones y velocidades permanecen idénticos como
multiconjunto de eventos. El colchón central conserva todas las notas y sólo extiende
sus ataques/extinciones de frontera, por decisión IA, para enlazar final e inicio.
Brecha cíclica global: cero beats.

MIDI SHA-256: `b8a2793f81ca122296025b1d1ff57526a0b1d9737fbb20c69a45d020028a5267`.
No es una obra de cinco minutos ni una certificación de calidad profesional: es una
sección electrónica plena de 32 compases / 1:04 que necesita audición antes de escalar.

Las estimaciones de cobertura no son cuotas. El primer ensayo mostró que exigir
porcentajes aproximados elegidos en el plan causaba recuperaciones innecesarias.
Sólo se recuperan contradicciones categóricas: una capa prometida omitida, una
polifonía prometida inexistente o notas donde la IA había decidido ausencia.

## Referencia de diseño

El objetivo no es imponer doce pistas a todos los estilos. En esta prueba se
pidieron explícitamente doce funciones. El criterio de ciclos entrelazados y
evolución de capas se contrasta con [Asynchronous or Polyrhythmic Loops, Ableton](https://makingmusic.ableton.com/asynchronous-or-polyrhythmic-loops).
Esto no prescribe notas ni garantiza que superponer loops produzca buen groove.
