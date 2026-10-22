# Club: comparación con forma completa

Referencia corta conservada: commit `2456dbe`, tag `anchor-club-20261022`.
No se modifica el anclaje ni se despliega Cloud.

## Resultado audible

- Trabajo `eaf93a40-aecb-44b4-b9f2-191fae9a19bb`, «Fase Umbría».
- Pedido: 180 s, 120 BPM, Re menor natural, semilla `202610220225`.
- Resultado: 88 compases / 176 s / ocho pistas / 1980 notas escritas por IA.
- Entrada: compases 1–12, promedio 5,5 pistas con sonido por compás.
- Desarrollo: 13–32, promedio 7,2.
- Contraste: 33–48, promedio 5,5.
- Regreso: 49–72, promedio 7,83.
- Salida: 73–88, promedio 5,69.
- Cero notas fuera de Re menor natural, ataques fuera de rejilla o
  solapamientos de la misma altura dentro de una pista.
- Revisión final: ocho voces desarrolladas, continuidad armónica 0,954,
  sin hallazgos bloqueantes. Esto no equivale a aprobación de escucha humana.

## Problemas encontrados y cambios locales

El primer ensayo (`80bc96ef-5993-44e2-9504-e167f5472cef`) describió un arco
entero dentro de una única sección y marcó ausencias globales incompatibles
con entradas posteriores. Se canceló exclusivamente ese ensayo del agente.
Se pidió una forma con varias secciones reales, sin alterar el generador.

El nuevo plan (`1c24d299-6791-410b-bc2c-a3caa10e96c5`) falló dos veces:
la validación exigía permisos de activación que el prompt de producción no
incluía. Ahora envía `allowed_section_indices` por propietario, con feedback
exacto de la sección incompatible. Prueba de regresión en SongPipelineTests.

La escritura de `825444fc-5071-445b-84ca-49953caa8b29` completó las doce
ventanas, pero la revisión final detectó piso sostenido insuficiente en el
regreso (12/24 compases). La revisión de apoyos Club retornaba prematuramente
cuando todas las voces tenían notas, ignorando los déficits de continuidad.
Ahora identifica propietarios y secciones del informe real, muestra los
compases sin cuerpo sostenido y acepta avances de continuidad sin perder
voces ni integridad. No se reduce el umbral de publicación.

Una propuesta útil puede recibir una revisión de alturas IA antes de ser
descartada por una nueva colisión. No se calculan notas de reemplazo localmente.
Se siguen protegiendo las demás voces y los límites técnicos; se vuelve a
medir el MIDI completo antes de aceptar. El último arreglo del pad pasó de
0,816 a 0,954 de continuidad sin aumentar el peso de choques ni las notas
sostenidas incompatibles. Se conservaron las voces rítmicas y melódicas.

`pulso_pipeline_tests` pasa. Los enlaces del estudio y del MIDI responden 200.
Cambios experimentales sin commit, push, despliegue ni compilación del VST.
La muestra se entrega para escucha, no se declara un nuevo anclaje aprobado.
