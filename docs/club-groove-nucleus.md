# Experimento local: núcleo Club conjunto

La prueba larga «Memoria de Re» fue rechazada por escucha humana aunque sus
revisiones técnicas/IA la consideraban apta. Consonancia y repetición no demuestran
por sí solas pulso, jerarquía ni dirección musical.

## Cambio general del escritor

Para conjuntos Club de hasta ocho propietarios, `AiMelodicDiscourse::writingGroups`
conserva a todos en una misma llamada por ventana. No selecciona primero solistas
y armonía para añadir después el arpegio como acompañante independiente.
Adaptativo conserva su agrupación anterior; el límite evita solicitudes gigantes.

La política Club v2 pide a la IA un ciclo métrico común en `rhythm_shape` y las
responsabilidades/posiciones complementarias de los propietarios en `relationship`.
No suministra patrones de notas ni desplaza ataques. La IA decide el ciclo, las
alturas, los ataques, sus duraciones, las repeticiones y la evolución.

En un encargo explícito de hasta cuatro pistas, la orquestación permite que los
stabs polifónicos sean el cuerpo armónico central, sin exigir dos pads añadidos.
Esto es alcance compositivo, no sustitución posterior de instrumentos.

## Primera prueba

32 compases, 4/4, 120 BPM = 64 segundos. Cuatro propietarios: bajo, stabs, arpegio
y hook. Re menor, cromatismos desactivados, sin percusiones ni pads. Primera pasada
explícita para evaluar lo escrito antes de revisiones de voicing. No es una obra
larga ni pretende sustituir su desarrollo: primero se evalúa si existe un núcleo
que merezca sostenerse y desarrollarse.

`scripts/audit-club-groove.mjs` mide ataques (un acorde cuenta como un ataque),
posiciones métricas, recurrencias de ciclos de uno/dos/cuatro compases y ataques
cercanos entre voces sobre la misma altura. No edita MIDI ni asigna una nota
estética. El revisor opcional recibe esas evidencias y debe juzgar el conjunto,
sin certificar que escuchó audio. La aceptación final sigue siendo auditiva.

Local: http://localhost:4214, historial Auto. Cloud, Git remoto y VST instalado
no se modifican durante el experimento. Las obras personales se conservan.

## Registro de pruebas

A: `ba04cabd-4169-4e6c-b0d0-d38f5e355c4c`, «Núcleo Oscuro en Re».
466 notas directamente IA, cuatro pistas, 32 compases. El MIDI ya conserva
bajo en tiempos 0/2, stabs en 2.5 y arpegio en corcheas: sus patrones no se
superponen como motivos rítmicos inconexos. Sin embargo, los compases 15–16
quedaron completamente vacíos y el hook conservó articulaciones muy breves.
No se presenta como candidata final. La auditoría y la lectura independiente
coinciden en la interrupción no desarrollada del groove. No se rellenó localmente.

La segunda versión del escritor quita la exigencia de secciones desiguales,
explicita la fase absoluta del ciclo frente a coordenadas MIDI locales y pide
articulación melódica diferenciada de las puertas cortas del arpegio. Son cambios
generales de instrucción/agrupación, no notas correctoras para esta obra.

B: `98632e75-d6c6-4cc8-a1f5-86896adc0175`, solicitud nueva desde cero con el
mismo prompt, duración, tempo y semilla; sin importar notas ni planes de A.
La semilla conserva la configuración local, pero no garantiza respuestas de IA
idénticas. No es una comparación determinista de cambios nota por nota.

### Candidata B para escucha

Resultado posterior: RECHAZADA por el usuario por escasez, falta de cuerpo y
carácter de cámara. No es un anclaje musical aprobado. El experimento v3 siguiente
está documentado en `club-production-plan.md`; recupera pads y producción por capas.

http://localhost:4214/?job=98632e75-d6c6-4cc8-a1f5-86896adc0175

309 notas directamente IA, cero notas locales, cuatro pistas, 32 compases.
Ningún compás global vacío, ningún solapamiento de una misma altura dentro de
una pista y ningún ataque cercano de la misma altura entre pistas (< medio beat).
El bajo conserva el ataque en el inicio del compás; los stabs responden en beat 2;
el arpegio repite 0/1/2.5/3.5 en 26 compases. El hook tiene articulaciones de
uno/dos beats (llegada final sostenida de 13), no el gate de cuarto de beat de A.

La retirada es real en compases 17–20 (0:32–0:40), con bajo y arpegio reducido;
el hook regresa en 21 (0:40). El cierre está en 29–32 (0:56–1:04), con llegada
melódica a Re6 y sostén de Re menor. No se presenta como una producción terminada
ni como una prueba de desarrollo largo. Requiere escucha humana del groove.

El control técnico pasa. La auditoría narrativa/soundscape heredada aún informa
fallos de capas sostenidas/diálogo/cierre; no se relajaron esos indicadores para
declarar éxito. Su contrato no equivale al de este núcleo explícito sin pads.
La lectura IA adicional de todas las notas considera la candidata apta para escucha,
no certificada. Puede contener errores de nomenclatura en su explicación (por ejemplo
llama C9 a una realización de Cadd9); las notas exportadas son la evidencia primaria.

MIDI SHA-256: `56dfe8664afee4a2d239e482b25acc85418dda878a3390f415db118c66807396`.
Worker SHA-256: `8e3a7d03380787b34609832f39a7e0401ed77d088417055e24a69417a6b2dbd7`.
La composición empleó nueve llamadas, aproximadamente 309 segundos; el revisor
adicional tardó 42 segundos y registra su consumo por separado. No se eligió por
velocidad ni por una puntuación aislada: se contrastó el resultado MIDI real.
