# Roadmap

## 0.1 — MVP base

- Motor algorítmico determinista.
- Bajo, percusión y contramelodía.
- Contexto por MIDI sostenido.
- Transporte del host, salida MIDI y preescucha.
- Windows VST3 y standalone.

## 0.2 — Frases coherentes

- Frases seleccionables de 1, 2, 4, 8 o 16 compases.
- Línea armónica capturada por compás.
- Motivo estable, conducción de voces, desarrollo y resolución por rol.
- Modos de reproducción exacta y evolución gradual.
- Semilla de identidad persistente.
- Pruebas de invariantes musicales y retención de identidad.

## 0.3 — Compositor jerárquico

- `CompositionPlan` común con ADN rítmico y contorno interválico.
- Ensemble coordinado en canales MIDI 1, 2 y 10.
- Función armónica, secciones de cuatro compases y curva de tensión.
- Variaciones emparentadas separadas de la creación de un ADN nuevo.
- Swing, microtiming, dinámica, energía y cohesión reproducibles.
- Worker de tiempo real, recuperación de transporte y pruebas simuladas del processor.

## 0.4 — AI Composition Browser

- [x] GPT mediante Responses API y Structured Outputs.
- [x] Armonía, melodía, bajo y batería como capas MIDI independientes.
- [x] Locks exactos por capa, regeneración selectiva y Undo.
- [x] Interfaz sin perillas basada en ideas y lenguaje natural.
- [x] Fallback local explícito y seguro.
- Evaluaciones de musicalidad con productores y comparación de modelos.
- Instalador firmado y matriz de compatibilidad de Live.

## 0.5 — Contexto Ableton

- Dispositivo `.amxd` firmado/empacado.
- Observación de pista, escena, clips y armadura mediante Live API.
- [x] Arrastre directo de archivos MIDI completos o por rol a Ableton.
- Mapeo optimizado para Push.
- Canal local IPC entre el bridge y el plugin.

## 0.6 — Audio contextual

- Sidechain estéreo opcional.
- Onsets, densidad, centroid y energía por bandas.
- Regla de espacio negativo frente al kick y la voz.

## 0.8 — Interpretación y respiración

- [x] Doce voces con presencia variable por frase y sección.
- [x] Diálogo entre lead y contrapunto, alternancia de bajos y drops coordinados.
- [x] Ocho mundos sonoros multitimbrales con selección automática desde el prompt.
- [x] Kits 808/909/Modern/Organic y modelos independientes de bajo, armonía y melodía.
- [x] Variación analógica de snare/hats, hat choke y voz mono sin octava chiptune.
- [x] Paleta individual persistente para las quince voces desde sus propias filas.
- [x] Playhead de arrangement sincronizado con PPQ, sección, compás y tiempo musical.
- [x] Osciladores band-limited, filtros, articulaciones y efectos espaciales.
- Evaluaciones ciegas con productores y ajuste de vocabularios por familia musical.
- Biblioteca factory opcional de samples propios con velocity layers y round-robin.

## 0.9 — Dirección de frase

- [x] Partitura previa de atención: foreground, response, support, texture y accent.
- [x] Presupuestos máximos de ataques y ventanas de entrada/salida por voz y compás.
- [x] Melodías con ritmo y silencios explícitos; lead y contrapunto nunca compiten.
- [x] Ritmo armónico variable, CC expresivos interpretados y evaluaciones de naturalidad.

## 0.10 — Integridad tonal

- [x] Tonalidad canónica derivada de `root_pitch_class` y modo, incluso si GPT contradice su etiqueta.
- [x] ADN temático reconciliado con la escala antes del desarrollo de la obra.
- [x] Validación horizontal de escala, apoyos fuertes, cromatismo preparado y resolución.
- [x] Validación vertical de choques ásperos y solapamientos sobre cambios armónicos.
- [x] El mismo contrato se aplica a GPT, compositor local, preview y MIDI exportado.

## 0.11 — Dirección rítmica

- [x] `RhythmPlan` por sección con estados `muted`, `reduced`, `sparse` y `four_on_floor`.
- [x] Gestos GPT explícitos: drop del último kick, doble kick, pickup, mute y fill.
- [x] Invariantes posteriores al render para que los anchors requeridos nunca desaparezcan.
- [x] Presupuestos independientes y pistas MIDI separadas para kick, clap, hats y percusiones.
- [x] Microtiming por instrumento, ciclos de velocity y coordinación de transientes kick–bass.
- [x] Interpretación estricta de pedidos como «bombo en negras constante» en GPT y fallback local.

## 0.12 — Lenguaje generativo abierto (actual)

- [x] Celdas rítmicas GPT de 1–4 compases en seis instrumentos independientes.
- [x] Desarrollo seccional mediante add/remove/shift/ratchet/velocity con propósito musical.
- [x] Segunda pasada GPT de crítica y revisión con fallback seguro a la primera propuesta.
- [x] Contrato elástico: la IA escribe contenido; el motor sólo protege invariantes explícitos.
- [x] SOLO/MUTE persistente por voz para MIDI enviado y preview, sin afectar exportación.
- [x] Cliente OpenAI cancelable con watchdog, deadlines por etapa y cierre seguro del host.
- [x] UI por etapas con `CANCEL` y rollback transaccional de patrón y linaje.
- [x] Rejilla compositiva exacta y `HUMAN PERFORMANCE` no destructivo con una única pasada.

## Futuro — Motor neuronal

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
