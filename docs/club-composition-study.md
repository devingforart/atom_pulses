# Estudio Club · Memoria de Re · 2026-10-22

Estado: RECHAZADA por escucha humana. El usuario identificó un resultado atolondrado,
sin pulso colectivo ni dirección convincente. Las métricas técnicas y las lecturas IA
fueron insuficientes para detectar ese fallo. No es un anclaje musical aprobado.
Se conservan los datos siguientes como trazabilidad del experimento, no como éxito.

## Escuchar

- Final: http://localhost:4214/?job=367381b1-8197-4a5d-9ddf-babd15a16b5f
- Original A: http://localhost:4214/?job=4f0781e4-605a-40b1-bc8f-187c5549c376
- Todas las corridas son Auto. Las creaciones personales no se modificaron.

Encargo: progressive house oscuro, Re menor, cinco minutos, 122 BPM, sin baterías
ni percusiones; bajo, arpegio, stabs, colchón central, pad superior, textura y hook.
Semilla `202610220064`; Terra/medium; cromatismos desactivados; IA soberana.
El ajuste a compases completos da 152 compases / 608 beats / aproximadamente 4:59.

## Resultado medido

| Observación del MIDI exportado | Original | Final |
| --- | ---: | ---: |
| Pistas con notas | 10 | 10 |
| Notas | 1799 | 1799 |
| Notas locales/procedurales | 0 | 0 |
| Clústeres de semitono entre pistas con al menos un beat compartido | 30 | 0 |
| Compases globalmente vacíos | 0 | 0 |
| Alturas fuera de la escala declarada | 0 | 0 |

La IA cambió 18 alturas en las dos pistas complementarias de pads. Ocho archivos de
pista son idénticos byte por byte al original, incluidos bajo, arpegio, stabs, hook,
respuesta y colchón central. Todas las pistas conservan cantidad de notas, ataques,
duraciones y velocidades. No se recortó ni se expandió ninguna célula localmente.

El final conserva su música original: se corrigió el criterio de evaluación para
reconocer un outro armónico de club, en lugar de exigir un nuevo solo al final.
Lo mismo ocurre con la introducción escrita con bajo/arpegio antes de los pads.
Estas reglas dependen del modo Club, no de nombres de canciones, compases concretos
o alturas prefijadas. Adaptativo conserva su contrato anterior.

La validación final normal, sin excepción de escucha, informa nueve voces
desarrolladas frente a ocho esperadas, clímax realizado, cierre válido y ninguna
incidencia. El último `club-ai-review.json` concluye `ready_for_human_listening`,
sin problemas detectados, después de examinar todas las notas y la armonía.

## Qué verificar al escuchar

- Introducción: identidad de bajo/arpegio antes de la llegada de los pads.
- Desde aproximadamente 1:58: bajada con memoria armónica.
- 2:29: retorno del núcleo reconocible y acumulación de capas.
- 4:35–4:59: retirada del movimiento y outro sobre Re menor.

La lectura IA es evidencia adicional, no sustituye la aprobación auditiva del usuario.
Las percusiones siguen desactivadas: ésta es la composición afinada del arreglo Club.

## Trazabilidad y alcance

La escritura inicial se hizo desde cero; las siguientes pasadas recuperaron sus
checkpoints completos y revisaron exclusivamente las alturas de pads. Los defectos
encontrados llevaron a cambios generales del escritor/revisor, no a reglas para
los compases 29/83 ni para las notas Mi/Fa o La/Sib de esta pieza.

Cadena útil: original `4f0781e4-605a-40b1-bc8f-187c5549c376` → revisión
`dada71a5-d92c-4d38-aa30-6bb37d75c080` → revisión
`a206148d-d062-4029-9c9d-695ff86ee6ca` → final
`367381b1-8197-4a5d-9ddf-babd15a16b5f`.

Los informes, uso de API y MIDI están en cada directorio
`build/local-studio-data/jobs/<id>/`. Las lecturas IA adicionales registran su
consumo por separado del contador de composición, en `club-ai-review.json`.

SHA-256 del MIDI final:
`4261d6b6ed940a37629b4c71de4f7913fcd717d7c7fc2b1a5989e6b4538b68ef`

SHA-256 del worker local de esta prueba:
`57f78a2de3d8e434447db1a84b82136798d5a4e4cb7c79b269ff040ecb6b2d59`

Trabajo local en branch `1.0`. Sin despliegue en Cloud, push ni instalación del VST.
El código compartido de Cloud y su migración quedan preparados para un despliegue
posterior; producción no se modificó en este experimento.
