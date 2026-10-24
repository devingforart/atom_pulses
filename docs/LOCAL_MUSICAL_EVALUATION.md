# Evaluación musical local de PULSO

El estudio local incluye una ficha de escucha, un banco de encargos y comparaciones A/B ciegas. **No cambia el compositor ni aprueba automáticamente una canción**. Su objetivo es registrar qué funciona para una persona que escucha, junto con la semilla, el prompt, la huella del worker y las auditorías MIDI.

## Flujo recomendado

1. Abrir **Laboratorio** y elegir uno de los seis encargos. **Cargar encargo** solo completa el formulario; no llama a la API. Componer cuando se desee.
2. Escuchar la obra completa en **Piano MIDI**. Si se evalúa también en Ableton, registrar ese contexto en la ficha. No comparar versiones con instrumentos o volúmenes distintos.
3. Pulsar **Evaluar** y puntuar de 1 a 5 cada dimensión: idea, conexión, melodía, armonía, ritmo, evolución, clímax, cierre y utilidad. Marcar hasta seis problemas y anotar los mejores y peores momentos con compás o tiempo. La ficha se puede actualizar.
4. Repetir cada encargo con varias semillas. Antes de concluir que un cambio de código mejora el motor, comparar varias obras nuevas, no solo una semilla favorecida.
5. Para un cambio controlado, generar la misma solicitud en ambas versiones y usar **Laboratorio → Comparación A/B**. Deben coincidir prompt, duración, BPM, enfoque y semilla. El orden A/B se asigna aleatoriamente; los títulos y IDs se revelan después de votar. Ambas se escuchan con el mismo piano y volumen.
6. Descargar **Informe JSON** para analizar puntuaciones, problemas anotados, votos, parámetros, costes aproximados y auditorías. Las métricas técnicas son contexto, no sustitutos de la escucha.

**Primera pasada** conserva el MIDI inicial sin revisión musical. Una valoración de esa obra mide el borrador, no el resultado editorial completo. Para una comparación controlada, abrir la obra completada y pulsar el botón de revisión IA: el plan, el prompt, la semilla y el MIDI original se conservan; la revisión crea otra obra y consume nuevas llamadas a la API. En Club revisa principalmente armonía, capas y cierre; no es una reescritura del concepto rítmico. La obra inicial no se sobrescribe. Al cargar un encargo Club del banco, la primera pasada se desactiva por defecto; se puede activar manualmente si se quiere estudiar el borrador.

## Criterio editorial

Una pieza puede ser tonal, técnicamente íntegra y aun así no ser utilizable. **No promover una versión solo porque sus validadores pasan o por una mejora en una obra concreta.** El campo «¿La usarías en una producción?» y los comentarios musicales deben seguir visibles junto a las métricas, nunca ser reducidos a una media única.

La propuesta para la siguiente fase es un estudio pequeño: tres semillas por encargo, al menos dos versiones del motor y varios oyentes con experiencia musical cuando estén disponibles. Empezar con 1–3 minutos; escalar a 5 y 10–15 minutos solamente cuando el desarrollo y el cierre funcionen en obras nuevas. Estos números son un protocolo de prueba, no una promesa de calidad ni un umbral automático implementado.

## Persistencia y privacidad

- Obras: `<PULSO_LOCAL_DATA_DIR>/jobs/<job-id>/`.
- Fichas: `<PULSO_LOCAL_DATA_DIR>/evaluations/<job-id>.json`.
- Comparaciones: `<PULSO_LOCAL_DATA_DIR>/comparisons/<comparison-id>.json`.
- Banco: `web/local-studio/benchmark-cases.json`.
- Informe: `GET /api/evaluations/report?download=1` en el servidor local.

Solo el estudio ligado a `127.0.0.1` ofrece estas rutas. Los cambios se guardan mediante solicitudes locales protegidas por `X-Pulso-Local`; no se suben a Cloud ni se publican con **Share**. Archivar una carpeta `jobs` separadamente no elimina sus fichas: para restaurar un análisis histórico hay que devolver también la obra a su raíz local original.

## Diagnóstico de fallos

Anotar el compás o segundo antes de editar código. Clasificar el problema según la fase que probablemente lo origina: premisa/motivo, plan formal, elenco, escritura de notas entre voces, revisión o reproducción. Mantener una hipótesis por experimento y revertir las versiones que pierdan en la escucha A/B. Si las mejoras de prompts y revisiones siguen sin ganar de forma consistente en varios encargos, el siguiente estudio debe evaluar un modelo o corpus musical especializado, no añadir más reglas a ciegas.
