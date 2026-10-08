# Auditoría de desarrollo musical en el estudio local

El estudio local (`http://127.0.0.1:4177`) muestra **Desarrollo y procedencia MIDI** al terminar una obra. También permite descargar `composition-audit.json`. Estos datos no cambian el MIDI ni activan el pipeline de publicación.

## Cómo comparar

1. Abrir la obra anterior **F# // La Luz que Desciende**. Su referencia es 2869 notas, 859 notas en bloques de ocho compases literalmente repetidos (29,9 %) y 24 instancias de notas idénticas entre pistas. La procedencia nota por nota no está disponible porque la versión anterior no la guardaba.
2. Usar **Repetir ajustes** y generar una nueva obra local. Para una comparación justa, conservar prompt, duración, tempo, modo y semilla. La respuesta del modelo aún puede variar; la semilla no garantiza el mismo MIDI de IA.
3. Escuchar la obra completa y el arpegio por separado. Comparar el desarrollo de frases y el contraste entre secciones, no solo el porcentaje global de repetición. Un pedal puede repetir mucho legítimamente.
4. Revisar el nuevo `composition-audit.json`. `noteProvenance` separa notas de primer uso de una celda IA, reutilizaciones, transformaciones y reutilizaciones transformadas. `exactRepeatedEightBarNotes` es otra medición: reconoce bloques de MIDI idénticos aunque procedan de celdas distintas. No deben sumarse esas categorías entre sí.

Los bloques se comparan por inicio relativo, altura y duración de cada nota; se ignora la velocidad. La coincidencia entre pistas exige el mismo inicio absoluto, altura y duración. Ambas son pruebas de identidad literal, no juicios de calidad musical. Una obra puede tener excelente repetición hipnótica o muchas notas diferentes sin propósito.
