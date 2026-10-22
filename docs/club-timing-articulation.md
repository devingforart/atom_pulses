# Club v6 — intención temporal y articulación

Problema observado en «Fase Umbría»: colchón, pad y textura usaban un retraso
recurrente de 0,25 beats aunque el plan de textura indicaba fase cero. El lead
no tenía ataques sobre pulsos enteros. Son datos de MIDI, no una medición de
sensación humana. Estar en rejilla no garantiza una frase articulada.

## Cambio de generación

Cada capa nueva declara `timing_mode` y `timing_intent`. La IA elige:

- `cycle_bound`: sus posibles ataques son el ciclo, origen y offsets que ella
  misma escribió. Puede omitir posiciones, no inventar retrasos fuera del plan.
- `phrase_flexible`: conserva libertad de ataques para frases desarrolladas;
  la célula expresa identidad, no una plantilla obligatoria.

El núcleo siempre conserva su ciclo. Colchones, texturas y secuencias pueden
comprometer su propia fase sin convertirse en propietarios del núcleo.
El schema limita las posiciones antes de escribir; el parser detecta cualquier
contradicción. No se desplazan notas, se expanden patrones ni se crean ataques.
Los planes históricos conservan su comportamiento: no se reinterpretan como
contratos nuevos ni se cambia el MIDI ya generado.

Las instrucciones distinguen suavidad de envolvente y retraso de MIDI. Se
permite compartir llegadas armónicas y separar por registro, disposición y
dinámica. Se elimina la insinuación de que todas las capas deben esquivar
todos los ataques del núcleo. Las síncopas siguen permitidas y no se impone
un porcentaje mínimo de negras, corcheas o semicorcheas.

## Comprobación

Pruebas C++: un apoyo cíclico en fase cero rechaza un onset 0,25 sin moverlo;
una frase libre conserva ese onset. Compatibilidad con planes anteriores.
`scripts/audit-club-timing.mjs` revisa notas exportadas y promesas reales de
ciclo. Informa subdivisiones sin declararlas errores musicales por sí solas.
Pruebas JS: no mutación de notas y síncopa de semicorchea explícita válida.

Ensayo Auto `653d480c-29f0-4b83-8e16-7c2a04c78a23`: mismo prompt, semilla y
duración que «Umbral de Re»; se recompone desde cero para probar la generación,
no se edita a mano la obra anterior. No es un A/B de notas idénticas: cambió
el contexto del generador y la IA puede elegir otro material.

Anclaje `2456dbe` intacto. Modelo Terra / medium sin cambios. Sólo local;
sin commit, despliegue Cloud ni compilación del VST.

## Muestra entregada

`1dc62b19-0576-4808-b83a-36718b2e1f2c`, revisión de alturas de la prueba
anterior, «Umbral de Re — Sección plena», 64 s, 892 notas, ocho pistas.
El exportado tiene cero contradicciones con los ciclos, cero ataques fuera de
rejilla, cero solapamientos de la misma altura dentro de una pista y cero notas
fuera de Re menor natural. Colchón, pad y textura tienen respectivamente
16/32/16 ataques, todos sobre pulsos enteros. El lead tiene 85 ataques sobre
pulsos enteros, 30 a contratiempo de corchea y nueve en otras semicorcheas:
la corrección no elimina adornos ni síncopas deliberadas.

La comparación entre las dos versiones exportadas confirma que la revisión
de alturas preserva todos los ataques, duraciones y velocidades por pista.
Persisten tensiones armónicas sostenidas (14 pares adyacentes y seis novenas
menores compuestas según el auditor); no se presenta como armonía libre de
tensiones ni como calidad humana certificada. La aceptación musical de esta
muestra queda pendiente de escucha del usuario.

`pulso_pipeline_tests` y los siete tests JS de auditoría pasan. Página y MIDI
responden HTTP 200. El worker local usa política Club v6; otros enfoques y
planes históricos no reciben nuevas restricciones temporales implícitas.
