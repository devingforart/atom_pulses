# Candidato local Club — 2026-10-24

El motor activo para las próximas pruebas **locales** de Club utiliza el plan
conjunto de bajo, acordes y voz protagonista, escritura de todo el conjunto
en una misma ventana IA y escucha de primera pasada por defecto. La casilla
local de primera pasada puede desactivarse explícitamente para un A/B.
Cloud y el VST no se cambian con este candidato.

## Ancla auditiva

- Referencia aprobada: `fefbe424-6f44-4c5c-85cf-db643ec32e01` — *Sombra de Cobalto*,
  60 segundos, Do menor, 122 BPM.
- Candidato largo: `1e8675dd-3ed5-49ef-b125-3b3b02277fd0` — *Sombra de Do*,
  180 segundos solicitados, 88 compases, Do menor, 122 BPM, 9 pistas MIDI.
- Escucha local: `http://localhost:4191/?job=1e8675dd-3ed5-49ef-b125-3b3b02277fd0`.
- Semilla: `6484725674144142049`.

La extensión larga empleó `reference_from_job` para resumir **notas reales** de
la referencia en el encargo IA: motivo, fase del bajo y acordes, y entradas de
las voces. No copia ni genera notas por procedimiento. Esta referencia es
**específica de esa obra** y no debe aplicarse por defecto a prompts ajenos.
Para reproducir exactamente la prueba se necesita conservar los directorios
de ambos jobs en `build/local-studio-data/jobs/`; Git guarda el motor y esta
receta, no el audio/MIDI ni respuestas privadas del proveedor.

La nueva obra superó la integridad MIDI: 1560 notas, ninguna fuera de Do menor,
ninguna nota huérfana o inseguramente corta. Es un **candidato auditivo**, no
una aprobación automática de calidad musical. La siguiente evaluación humana
determinará el siguiente cambio.
