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

Los hallazgos se convierten en una solicitud de reescritura **focalizada** al
editor de IA. Se conservan las otras pistas y las notas de una reparación solo se
aceptan si mejoran la revisión sin degradar la integridad ni el relato. El número
de pasadas y el tiempo de reparación siguen acotados. Si el MIDI final no cumple,
el trabajo no se publica como completado; los checkpoints provisionales y el
registro de auditoría permanecen disponibles para diagnóstico. El worker vuelve
a revisar el MIDI definitivo y guarda `final-score-review.json` antes de exportar.

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
