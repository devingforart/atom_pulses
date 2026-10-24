# Revisión armónica focalizada

El motor compartido de Local Studio y PULSO Cloud revisa el MIDI final antes
de publicarlo. Cuando una pista cuyo papel declarado es `primary_chord_bed`
mantiene un acorde en solo uno de cuatro compases y luego desaparece mientras
otra capa armónica continúa en al menos tres, la marca como **candidata a
revisión**, siempre que la sección esté activa para esa pista y no sea una
bajada, interludio, outro u otra pausa formal declarada. No interviene en
ventanas completamente retiradas ni en frases armónicas ya desarrolladas.

Para cada candidata, el motor envía a la IA el plan, el contexto MIDI de las
otras pistas y las notas originales del plano. La IA decide todas las nuevas
alturas, entradas, duraciones y velocidades de la frase; el código no duplica
acordes, no prolonga notas ni agrega notas por patrón. Cada respuesta viene
como MIDI estructurado. La revisión está acotada a cuatro compases y a un
máximo de cuatro frases por obra, con presupuesto total de cuatro minutos.
Si el plan de Club declaró erróneamente un único ataque cíclico para ese
colchón, la revisión admite ataques en cambios armónicos ya escritos por la IA;
no modifica el ciclo del bajo ni del pulso ni crea ataques automáticamente.

El candidato solo reemplaza a la frase original si el render verifica:

- cuerpo sostenido real en los cuatro compases;
- todas las demás notas y controles intactos;
- integridad MIDI y tasa de conflictos tonales por nota no peores que antes;
- sin nuevos sostenidos inválidos ni mayor choque armónico Club.

Una respuesta ausente, incompleta o musicalmente insegura conserva la obra
anterior. El modo de primera pasada omite el resto de las revisiones globales,
pero permite esta comprobación focalizada cuando el perfil compartido la
habilita. El estudio local puede probar el editor aisladamente con
`PULSO_AI_HARMONIC_PHRASE_ONLY=1` al ejecutar directamente un worker experimental de revisión; Cloud
no expone esa opción.

Referencia de escucha aprobada: [Sombra de Do, compases 9–12](anchors/club-harmonic-phrase-2026-10-24.md).
