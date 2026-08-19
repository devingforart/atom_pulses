# Live Native Sound Director

## Criterio tímbrico electrónico 0.48

Cuando el dominio es `club_electronic` y la intención orquestal es baja, la materialización y la
resolución de Browser aplican el mismo contrato: mallets afinados, chiptune, 8-bit, toy y game-like
no pueden convertirse accidentalmente en el foreground. Se sustituyen por synths, pads o texturas
compatibles con la función de la pista. Una petición explícita del músico sigue teniendo prioridad.

## Objetivo

PULSO compone MIDI y Ableton Live produce el sonido final. El plugin no aloja instrumentos
VST ni duplica motores dentro de su callback. `PulsoDeployRemote` crea las pistas en
Arrangement y carga instrumentos, presets o Racks nativos desde el Browser de Live.

## Contrato de sonido

Cada parte contiene:

- `catalog_id`: identidad instrumental estable de PULSO.
- `native_device`: dispositivo nativo elegido por GPT o por el usuario.
- `preset_intent`: descripción tímbrica; nunca un nombre de preset inventado.
- `timbre_signature`: identidad perceptual estructurada (`source`, `envelope`, `spectrum`,
  `motion`, `space`, `texture`, `uniqueness`).
- `sound_selection_seed`, `sound_variation` y `sound_locked`: variedad reproducible,
  regeneración por pista y conservación explícita del sonido elegido.
- `device_candidates`: intentos ordenados para resolver contenido instalado.
- función orquestal, articulación, divisi y prominencia.

El request incluye además una `sound_world` compartida. Identidad y familia siguen siendo
restricciones fuertes; la paleta global sólo ordena candidatos compatibles para que todas
las pistas pertenezcan al mismo espacio tímbrico.

GPT sólo puede elegir `Drum Rack`, `Instrument Rack`, `Simpler`, `Sampler`, `Drift`,
`Meld`, `Wavetable`, `Operator`, `Analog`, `Electric`, `Tension`, `Collision` o
`Granulator III`. La normalización rechaza cualquier otro valor.

## Inventario y resolución

El Remote Script indexa incrementalmente `Sounds`, `Drums`, `Instruments`, `Max for Live`
y `User Library`. Nunca visita `Plug-Ins`. Publica el inventario real en
`%LOCALAPPDATA%\PULSO\LiveBridge\inventory.json` y procesa el Browser en lotes pequeños
para no congelar Live.

El schema 9 elige entre los mejores candidatos que cumplen identidad y fidelidad. La
semilla evita azar opaco, `sound_history.json` aplica un cooldown de doce elecciones por
catálogo y el lock recupera la última ruta verificada de esa pista. El historial sólo se
actualiza después de que Live cargó y verificó realmente el dispositivo.

Cuando el dispositivo cargado es Drift, Wavetable, Operator, Analog o Meld, el puente
traduce la firma a cambios conservadores de filtro, ataque, release, modulación y anchura.
Los parámetros ausentes se omiten y el contrato de release se aplica después como límite
de seguridad.

Al desplegar, puntúa nombres y rutas por intención, instrumento y dispositivo. Un `Drum
Rack`, `Instrument Rack`, `Sampler` o `Simpler` vacío nunca cuenta como sonido. El resolver
usa palabras completas con acentos normalizados y reserva kits poblados 909, 808 o
Percussion Core/Spirit como red de seguridad para las pistas rítmicas.

Después de solicitar cada carga, el puente comprueba que Live haya creado un dispositivo y
que todo Rack tenga cadenas. `status.json` registra preset, ruta y verificación por pista;
por eso `20/20` significa veinte sonidos comprobados, no veinte llamadas aceptadas por el
Browser. El estado también informa `fallbacks` y `missing`, sin disfrazar ausencias.

La identidad instrumental es una restricción fuerte: cello sólo puede resolver a cello o,
si no está instalado, a la familia Strings; oboe sólo a oboe o Winds; timpani, toms y cymbal
sólo a sus identidades o familias de percusión. Adjetivos como `solo`, `section`, `low` o
`orchestral` únicamente ordenan candidatos ya compatibles y nunca permiten cruzar familias.
En Drum Racks, cada nota MIDI utilizada debe corresponder además a un pad poblado.

## Playback adaptation y commit transaccional

La composición conserva su semántica musical; el clip que recibe Live se adapta al
instrumento verificado. Los one-shots rítmicos se disparan en su nota raíz para evitar
transposición accidental, los Drum Racks remapean cada articulación por nombre de pad y
las articulaciones incompatibles (por ejemplo snare/clap, closed/pedal hat o
bongo/conga/clave) se separan en pistas reproducibles independientes. Las percusiones
priorizan one-shots con identidad completa antes que racks cuyos pads no puedan verificarse.
Marimba, bells, winds, brass y percusión aplican pisos y límites de duración instrumentales,
y toda repetición del mismo pitch recorta la nota
anterior para que su note-off no silencie el ataque siguiente.

El despliegue realiza preflight y staging antes de tocar el arreglo anterior. Una identidad
no disponible usa primero un fallback audible garantizado: one-shot de Drum Hits para
ritmo o Drift/Wavetable/Operator para material tonal. Si una pista aun falla, sólo se
elimina esa pista de staging y las demás se confirman con estado `degraded`. El rollback
completo queda reservado para fallos transaccionales de la API de Live o para un despliegue
sin ninguna pista audible.
El contrato schema 5 incluye `controls`, `expressions`, paleta tímbrica y score de producción.
El puente interpola las
curvas de manera lineal y proyecta su interpretación sobre velocity, velocity deviation,
release velocity y duración/sustain. Si Live expone `add_new_notes`, usa las propiedades
extendidas de nota; de lo contrario conserva la misma intención audible mediante notas
legacy ya modeladas. El reporte de `CREATE IN LIVE` muestra cuántos controles recibió,
cuántas notas modificó, qué sustain extendió y qué API de inserción utilizó.
Antes de serializar, las curvas genéricas de una voz se intersectan con las frases audibles
de cada parte concreta; un instrumento con tres notas ya no hereda cientos de eventos que
pertenecen a otros miembros de su familia.

El inventario publica además identidades exactas, sustituciones familiares y ausencias.
PULSO incluye ese resumen en el contexto de GPT para que la orquestación nazca ejecutable
con el contenido instalado, sin convertir el inventario en una plantilla creativa fija.

## Flujo

```text
GPT SongPlan
  -> InstrumentAssignment(native_device, preset_intent, timbre_signature)
  -> OrchestrationScore / Pattern(partId)
  -> ProductionPolish (metric + tonal + rhythm + expression gate)
  -> request.json schema 9 (notes + expression + sound world + timbre + variation/lock)
  -> PulsoDeployRemote
       -> pistas + clips MIDI
       -> resolución contra inventario nativo
       -> carga secuencial en Browser
       -> status.json
```

`FULL ORCHESTRATION` crea una pista por parte poblada. `QUICK 3-STEM` consolida Ritmo,
Armonía y Melodía. El puente reemplaza exclusivamente las pistas que él mismo creó y no
toca pistas del usuario.

`request.json` conserva el último despliegue como snapshot, pero el puente adopta su UUID
al arrancar y no lo ejecuta. Sólo un UUID nuevo escrito al pulsar `CREATE IN LIVE` puede
crear o reemplazar pistas. Abrir PULSO, abrir un Set o reiniciar Live nunca despliega nada.
`CREATE IN LIVE` sólo admite un clic del puntero: Space y Return quedan reservados para el
transporte/editor y no pueden iniciar un despliegue aunque Live restaure el foco allí.

La preescucha integrada sigue disponible para componer antes del despliegue. Es ligera y
no pretende reemplazar la producción nativa de Live. `CREATE IN LIVE` conserva una
proyección audible y editable de la interpretación; el MIDI multitrack arrastrado permanece
como representación sin pérdida de CC, bends, pressure y metaeventos crudos completos.
