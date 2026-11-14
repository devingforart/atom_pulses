# Desarrollo de arreglo: experimento posterior al anclaje de 24 voces

Base preservada: commit `0500b06`, estudio `http://localhost:4313`.
Nuevo estudio separado: `http://localhost:4314`, script
`scripts/start-arrangement-development.ps1`. No despliega ni modifica Cloud.

El cambio es de dirección compositiva para la IA y de descripciones del contrato
de escenas: se aplica al plan global, al reparto, a las escenas y a la escritura
de notas de Club con escenas conjuntas. No edita el MIDI después de componer,
no introduce un nuevo umbral de rechazo y no modifica el motor no-Club.

Se mantienen pulso, tonalidad, exclusión de percusión, capacidad de 24 voces y
ventanas coordinadas. La nueva dirección pide identidades complementarias,
respuestas con continuación, texturas con conducción de voces y transiciones
relacionadas con lo que viene antes y después. No obliga a usar cada recurso ni
a aumentar la actividad. Un efecto de sonido no se considera realizado por
nombrar una pista: MIDI neutro no reproduce un barrido de filtro.

Comparación propuesta desde cero con el prompt, semilla, duración y opciones de
`b02c87c0-e009-4748-ba70-c116c87f2e24`. La misma semilla no garantiza una salida
idéntica del proveedor. No reutilizar su blueprint: impediría probar los cambios
de reparto y planificación. Mantener primera pasada para no confundir el cambio
con una revisión posterior. La mejora musical requiere escucha; los tests solo
comprueban integración y conservación técnica.
