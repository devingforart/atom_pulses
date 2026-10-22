# Club: revisión de desenlace y recuperación del checkpoint

## Fallo investigado

La obra `1e488959-04b0-4910-bf28-a1223e63973b`, «Hipnosis de Re», completó
2584 notas y once ventanas, pero falló en la revisión del desenlace. Los
reintentos automáticos no resolvían la causa: Club retornaba antes del editor
terminal compartido y el evaluador comparaba sólo cuatro notas finales.

## Cambios locales

- Las obras Club completas con frase o liberación terminal pendientes alcanzan
  el editor final existente. Las secciones repetibles conservan su salida sin
  obligación de cadencia. Otros enfoques mantienen su ruta anterior.
- Un defecto terminal aislado evita revisiones generales de apoyos y alturas.
  Una reanudación que ya pasa la revisión final devuelve el checkpoint sin
  pedir más cambios musicales.
- El editor recibe notas reales del sujeto y del cierre, métricas de la revisión
  y el contexto de la obra. El schema conserva ventanas y fases cíclicas Club.
- Si la llegada armónica ya pasa, la reparación temática no recompone bajo y
  colchón. Se protege el resto de propietarios y el MIDI fuera del final.
- Los candidatos rechazados se exportan como diagnóstico separado, nunca como
  checkpoint aceptado ni obra publicable.

Las primeras reparaciones IA no pasaron. El diagnóstico mostró candidatos
que citaban el sujeto dentro de la última frase y después descendían a tónica:
el auditor ignoraba esa cita porque sólo miraba las últimas cuatro notas.
No se fuerza a la IA a terminar ascendiendo para satisfacer ese artificio.

El auditor Club ahora busca la evidencia temática dentro de la frase terminal
conectada, conservando los umbrales y la comprobación del acorde final. Si hay
una nueva frase o etiqueta melódica final, no puede tomar prestado un retorno
de otra sección o de una frase desconectada. Se preserva el caso previamente
válido de un outro deliberadamente sin melodía que cede el retorno a la armonía.
Esta lectura no escribe, mueve ni transpone notas.

## Resultado

Trabajo recuperado: `5766a697-0d3b-4814-848f-ed20f11d14dd`.
La comparación de los 2584 eventos del MIDI contra `partial-16.mid` del origen
confirma igualdad de altura, ataque, duración y velocidad. Esta recuperación
no es una nueva composición ni una nueva interpretación de cierre. No hizo
llamadas a la API: corrigió un falso rechazo de la lectura anterior.

Revisión final: ocho voces desarrolladas, continuidad armónica 1,0, contraste
de clímax 1,380, resolución 0,691, frase terminal aceptada, sin bloqueantes.
Auditoría temporal: cero contradicciones con las fases cíclicas declaradas.
Estos indicadores no garantizan calidad de escucha ni ausencia de tensiones.

`pulso_core_tests` y `pulso_pipeline_tests` pasan, incluyendo retorno seguido
de descenso, etiqueta terminal desconectada, outro sin melodía y secciones
repetibles. Worker local actualizado; sin commit, push, Cloud ni build de VST.
El anclaje aprobado `9c0903e` permanece intacto.
