# Revisión final del MIDI

El perfil editorial compartido por el estudio local y Cloud evalúa el MIDI renderizado
antes de presentar una obra como terminada. La revisión contrasta el resultado con
el encargo explícito de cantidad de pistas, el reparto instrumental aceptado y la
forma dramática que la IA propuso.

- **Voces desarrolladas:** cuenta líneas independientes con presencia en más de una
  sección, actividad real y desarrollo verificable entre frases. Una reaparición
  transformada puede reutilizar material; la copia literal sección tras sección
  no cuenta como desarrollo melódico. Los colchones sostenidos tienen
  un umbral de ataques diferente de una melodía; las pistas de transición no se
  confunden con voces. Copiar una celda a través de la obra no multiplica voces.
- **Continuidad armónica:** comprueba por compás las capas sostenidas declaradas,
  siguiendo las respiraciones previstas por la forma. Señala la sección concreta
  cuando el piso desaparece involuntariamente.
- **Clímax:** cuando el plan declara uno, compara su peso audible de instrumentos
  simultáneos y actividad con la sección preparatoria. No exige simplemente más
  notas: pide un retorno musicalmente consecuente en los propietarios adecuados.
- **Salida del conjunto:** en un cierre con resolución, comprueba si varias voces
  sostenidas se apagan juntas en un intervalo muy breve al final y si la última
  voz, aun estando sola, llega al límite del MIDI sin dejar un reposo audible.
  Una nota final tonalmente estable no basta para aprobar ese corte: el último
  sonido debe liberar espacio antes de que termine el archivo. Se respeta un
  *hard stop* expresamente compuesto. Una cadencia conjunta también puede
  cerrar bien si la sigue un reposo final audible; no se obliga a escalonar
  sus notas por sistema. Ese reposo final no se cuenta como una brecha del
  colchón armónico en el último compás; los demás compases se miden igual.
  Si falta una salida
  deliberada, la IA decide primero las duraciones de las notas terminales ya
  aceptadas, sin tocar alturas, ataques, dinámica ni otras pistas. Se aprueba
  solo si la salida se vuelve perceptible y no empeora la armonía ni el relato.
  Si no alcanza, la IA puede reescribir solo las voces implicadas en la última
  ventana. Ningún fade ni nota de coda se añade proceduralmente. El MIDI
  anterior y las pistas no afectadas permanecen intactos.
- **Relevo final:** si el plan de frases de la IA entrega la última consecuencia a
  otra voz melódica, la revisión busca el retorno transformado del protagonista
  antes de ese relevo y exige que la voz final tenga notas audibles y una llegada
  estable. No fuerza al protagonista a tocar encima de la respuesta ni confunde
  un pad con el último hablante. Si el relato ya cierra y únicamente falta la
  salida del conjunto, la IA revisa solo las duraciones finales.
  La frase conectada debe llegar hasta el último ataque melódico: un motivo
  anterior seguido de una nota tónica aislada no basta. Si se declara un relevo,
  otra voz melódica no puede añadir un ataque después de que termine quien debía
  pronunciar la última frase. En esos casos la IA reescribe en conjunto la frase,
  el bajo y el lecho armónico de la escena final; el auditor no inventa notas.
  Al retomar un checkpoint completo con un defecto aislado del cierre, se omiten
  las revisiones generales de escenas anteriores. La ventana editable comienza
  antes de la entrada del último hablante que planificó la IA; si no existe ese
  relevo, se usa la última ventana de escritura. Cuando una propuesta mejora la
  frase pero introduce unos pocos choques verticales nuevos, una segunda revisión
  de IA puede elegir únicamente las alturas implicadas. La propuesta se acepta
  solo si conserva intacto el MIDI fuera de esa ventana y supera de nuevo la
  revisión de la obra completa.

Los hallazgos se convierten en una solicitud de reescritura **focalizada** al
editor de IA. Se conservan las otras pistas y las notas de una reparación solo se
aceptan si mejoran la revisión sin degradar la integridad ni el relato. El número
de pasadas y el tiempo de reparación siguen acotados. Si el MIDI final no cumple,
el trabajo no se publica como completado; los checkpoints provisionales y el
registro de auditoría permanecen disponibles para diagnóstico. El worker vuelve
a revisar el MIDI definitivo y guarda `final-score-review.json` antes de exportar.

En el estudio local experimental, `PULSO_AI_TERMINAL_CRITIC_EXPERIMENT=1`
permite auditar un checkpoint ya completo sin volver a escribir las escenas
anteriores. Un crítico de IA recibe el MIDI real del cierre, las notas de apertura
y la armonía de los dos actos finales. Puede conservar el cierre tal como está:
un segundo ataque, una coda tónica o un acorde sin todas sus tensiones
opcionales no son defectos automáticos. Si identifica una debilidad musical
concreta, otro paso reescribe solo la escena final y compara ambas versiones.
La candidata debe pasar integridad MIDI, continuidad, seguridad tonal y una
preferencia A/B independiente; un empate conserva el original. Este ensayo
local no modifica el perfil de Cloud ni el VST y no sustituye la escucha humana.

El número explícito de pistas sobrevive a la reconstrucción del plan. Antes de
escribir MIDI se comprueba que el reparto tenga suficientes líneas autorales y
que no use transiciones o destinos tímbricos derivados para cubrir esa cuota.
La fase de normalización no convierte ninguna línea de un reparto de IA en un
destino tímbrico compartido, haya o no una cantidad de pistas solicitada. Un
relevo solo existe si la IA lo planifica y escribe MIDI propio para cada pista.
La densidad de la matriz de orquestación marca un mínimo orientativo por sección,
no un máximo de ocho pistas simultáneas. Se permiten hasta 70 propietarios
declarados o, si hay menos, el tamaño real del reparto (PULSO admite actualmente
64 instrumentos). La calidad de esa simultaneidad se evalúa después sobre el
MIDI renderizado: choques tonales, continuidad, desarrollo y contraste.
Los grupos secundarios de instrumentos pueden devolver arrays MIDI vacíos en
una ventana cuando el silencio es musicalmente intencional. La base principal
debe seguir sonando y las obligaciones de presencia del reparto se comprueban
en el MIDI completo. No se agregan notas para llenar ese espacio.
Antes de la escritura, el plan de frases comprueba que cada hablante y cada
respuesta asignados estén activos en algún tramo de la frase. Una promesa de
diálogo incompatible con el propio reparto se devuelve a la IA para revisión;
no se inventa después una voz ausente.
La exclusión «no se requieren percusiones ni baterías» se aplica ya al reparto
inicial: los instrumentos rítmicos y las pistas de transición que no pueden
contar como voces desarrolladas se sustituyen antes de pedir la interpretación.
Después de cada bloque se compara lo ya desarrollado con las líneas pendientes
y el máximo de revisiones disponibles: una meta matemáticamente inalcanzable se
detiene antes de pagar el resto de la obra. También se comprueba temprano el
sostén de los dos cuerpos armónicos principales y, en tonalidad consolidada,
una acumulación persistente de choques entre notas. Una obra cercana al objetivo
puede solicitar una única revisión coordinada de dos cuerpos armónicos; ambos
se aceptan solo si el MIDI completo mejora sin nuevos conflictos tonales.

La exportación técnica y la aprobación musical son estados distintos. Una obra
puede terminar con MIDI íntegro y descargable aunque el auditor creativo o tonal
recomiende revisarla; en ese caso el manifiesto marca
`musical_review_required=true` y la suite no la presenta como partitura aprobada.
Esto es un control de coherencia observable, no una garantía automática de valor
artístico. La escucha humana sigue siendo necesaria para aprobar una obra.
