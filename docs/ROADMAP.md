# Roadmap

## 0.1 — MVP actual

- Motor algorítmico determinista.
- Bajo, percusión y contramelodía.
- Contexto por MIDI sostenido.
- Transporte del host, salida MIDI y preescucha.
- Windows VST3 y standalone.

## 0.2 — Validación con músicos

- Persistir semilla y patrón exactos.
- Bloqueos separados para ritmo, armonía y dinámica.
- Botones A/B/C y historial de variaciones.
- Swing y anticipaciones.
- Pruebas automatizadas del processor con bloques de transporte simulados.
- Instalador firmado y matriz de compatibilidad de Live.

## 0.3 — Contexto Ableton

- Dispositivo `.amxd` firmado/empacado.
- Observación de pista, escena, clips y armadura mediante Live API.
- Escritura directa de clips MIDI.
- Mapeo optimizado para Push.
- Canal local IPC entre el bridge y el plugin.

## 0.4 — Audio contextual

- Sidechain estéreo opcional.
- Onsets, densidad, centroid y energía por bandas.
- Regla de espacio negativo frente al kick y la voz.
- Worker dedicado con colas lock-free.

## 0.5 — Motor neuronal

- `PatternProvider` intercambiable.
- Modelo MIDI pequeño exportado a ONNX.
- WinML/DirectML en Windows y CoreML/MLX en macOS.
- Inferencia anticipada por compás, nunca en el callback de audio.
- Fallback algorítmico cuando el modelo no responde.

## 1.0 — Producto

- VST3 Windows/macOS, AU/AUv3 y standalone.
- Presets curados y migración estable de estado.
- Telemetría estrictamente opt-in y sin subir audio.
- Accesibilidad, internacionalización y manual completo.
- Firma de código, instaladores, crash reporting opt-in y soporte.
- Revisión legal de modelos, dataset, marca y licencias.

