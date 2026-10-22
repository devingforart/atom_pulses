# Club v5: sustancia musical, experimento local

La escucha del usuario rechazó v4 (`7d08b168-c84a-47b9-a130-ed6e5a5293ac`):
la estabilidad mejoró, pero el contenido seguía siendo escueto e incompleto.
No se considera anclaje musical aprobado aunque pase la revisión técnica.

## Cambios

- Se separa sencillez del suelo métrico de pobreza del contenido. El bajo y el
  pulso pueden seguir siendo estables mientras una secuencia/voz principal
  desarrolla material perceptible. No se fuerza a esa voz a ser un reloj escaso.
- El plan IA incorpora `musical_arcs`: spans absolutos de compases, propietario
  del argumento, sujeto, continuación, llegada y respuesta del acompañamiento.
  La IA decide esos spans; no se prescribe una forma de ocho compases ni una
  secuencia de notas. Los argumentos pueden cruzar ventanas técnicas.
- El escritor recibe sólo los argumentos que atraviesan su ventana real,
  además del MIDI ya aceptado. No reinicia el relato en cada solicitud.
- Se mantiene la validación de IDs, spans, escala, fase del núcleo e integridad.
  Los planes históricos sin argumentos nuevos siguen siendo reanudables.
- El crítico compara intención y notas realizadas: no aprueba por el número
  de pistas, cobertura, adjetivos narrativos o ausencia de disonancias.

No hay expansor de arpegios, relleno de notas, inserción de silencios, transposición
o cuantización creativa local. Todas las notas siguen siendo escritas por IA.
No cambia el modelo: Terra / medium. No se despliega Cloud ni se compila VST.

## Ensayo

Trabajo Auto `37e05572-7f2a-4f88-8d78-a1023af23345`, semilla `202610220225`.
32 compases / 64 segundos / 120 BPM / Re menor / ocho pistas / sin percusión.
Se vuelve a componer desde cero: el reparto anterior restringía la voz principal
a un registro muy estrecho y un gesto mínimo; no se reutiliza como si fuese un
plan adecuado para un desarrollo más completo.

Las pruebas unitarias comprueban que un argumento que cruza ventanas conserva
su contexto, no aparece fuera de su span y no puede citar propietarios inexistentes.
Es una comprobación de datos, no una garantía de narrativa audible.

### Revisión del material realizado

La primera escritura entregó 1050 notas IA en ocho pistas. El análisis de las
notas detectó choques sostenidos entre apoyos: no se presenta esa salida bruta
como versión final. La revisión `07cda257-e84b-4223-abb5-f299a94df097` conserva
el material aceptado y solicita a la IA cambios de alturas de los apoyos,
protegiendo ataques, duraciones y voces restantes.

Se corrigió además un límite de infraestructura: una ventana con más de ocho
objetivos de revisión antes se omitía. Ahora se procesa en transacciones de
ocho objetivos y se vuelve a medir el MIDI restante. El límite de la respuesta
no se confunde con la cantidad de problemas que puede tener una ventana.
No se calculan alturas sustitutivas ni se crea contenido por código.

Límite musical conocido: la melodía mantiene el mismo contorno de alturas en
las cuatro ventanas, con pequeñas variantes de ataques. Más contenido y una
declaración de arco no prueban por sí solos un desarrollo narrativo global.
La escucha del usuario sigue siendo necesaria para valorar esta nueva versión.

La revisión independiente de las 1050 notas exportadas emitió
`ready_for_human_listening` para `07cda257-e84b-4223-abb5-f299a94df097`.
Reconoció arpegio identificable, suelo métrico, frase conectada y cuerpo
sostenido real; dejó reservas sobre la exposición del pad frente al lead y la
llegada sobre el acorde del reenganche. No es una aprobación auditiva humana.

La telemetría identificó otro límite concreto: respuestas de revisión
`incomplete / max_output_tokens` con los 2200 tokens usados enteramente en
razonamiento. La transacción Club dispone ahora de 4096 tokens y hasta 90 s,
siempre dentro del presupuesto restante del proceso. El cambio sólo permite
terminar la respuesta de la IA; no genera ni transforma notas localmente.

### Candidato audible de esta iteración

`23a3b558-11f7-4d57-adb9-7f18f4d5c59c`, «Umbral de Re», completado.
1050 notas IA / ocho pistas / 32 compases / 64 segundos. El exportado no tiene
ataques fuera de rejilla ni superposiciones de la misma altura dentro de una
pista. El final alcanza el límite del ciclo sin barra vacía.
Las revisiones de apoyos conservaron cantidad de notas, ataques y duraciones;
los pares de semitono sostenido observados bajaron de 19 a 5, y los pares de
novena menor compuesta de 23 a 6. Son observaciones, no una prohibición de
tensiones ni una garantía de calidad. Persisten tensiones y la melodía conserva
su contorno repetido: se entrega como nueva comparación audible, no como obra
profesional certificada ni como anclaje aprobado por el usuario.

La página, el trabajo seleccionado y `full-song.mid` responden HTTP 200.
Worker local actualizado; `pulso_pipeline_tests` pasa. Sin commit, despliegue
Cloud ni compilación del VST en esta iteración.
