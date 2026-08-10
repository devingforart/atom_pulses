# Probar PULSO en Ableton Live

## 1. Instalar y encontrar el plugin

Compila e instala `PULSO.vst3`. El script usa por defecto la ubicación oficial de
desarrollo `%LOCALAPPDATA%\Programs\Common\VST3`. En Live abre
`Settings > Plug-Ins`, habilita VST3 y pulsa `Rescan`. El plugin aparece como
`Pulso Audio > PULSO`. Si tu versión de Live no explora esa ruta, selecciona la
carpeta como `VST3 Custom Folder` o instala globalmente en
`C:\Program Files\Common Files\VST3` desde una consola elevada.

Para actualizar una versión existente, cierra Live antes de ejecutar el instalador:
Windows bloquea el DLL mientras el plugin está cargado. El script comprueba ese bloqueo
antes de tocar la instalación anterior.

## 2. Escuchar inmediatamente

1. Crea una pista MIDI.
2. Inserta PULSO.
3. Escribe opcionalmente una dirección creativa.
4. Pulsa `GENERATE IDEA`.
5. Inicia el transporte.

El sintetizador de `Preview` permite escuchar el patrón sin cargar otro instrumento.
El indicador distingue una composición GPT validada del motor local.

## 3. Conservar y regenerar capas

1. Genera una idea y escúchala completa.
2. Activa `LOCK MELODY` si te gusta el hook.
3. Activa `LOCK HARMONY` si quieres conservar progresión y voicings.
4. Pulsa `REGENERATE UNLOCKED`: las capas bloqueadas quedan idénticas nota por nota.
5. Usa `UNDO` para volver al resultado completo anterior.

Armonía usa canal 3, melodía canal 2, bajo canal 1 y batería GM canal 10.

## 4. Enviar el MIDI a otro instrumento

1. Conserva PULSO en la primera pista.
2. Crea una segunda pista MIDI y carga el instrumento deseado.
3. En `MIDI From`, selecciona la pista que contiene PULSO.
4. En el segundo selector de entrada elige la salida del plugin cuando Live la muestre.
5. Pon `Monitor` en `In` o arma la segunda pista.
6. Desactiva `Preview` en PULSO si solo quieres escuchar el instrumento externo.

Para convertir el resultado en un clip, graba la segunda pista. Esta ruta conserva
notas y velocidades como MIDI normal.

## 5. Arrastrar clips MIDI directamente

Cuando la partitura ya contiene notas, mantén pulsada una de las asas inferiores y
arrástrala a una pista o espacio vacío del Arrangement de Live:

- `ALL MIDI`: archivo MIDI multitrack con bajo, contramelodía y batería.
- `HARMONY`: acordes y voicings del canal 3.
- `BASS`: clip del canal 1.
- `MELODY`: clip del canal 2.
- `DRUMS`: clip GM del canal 10.

El clip conserva la longitud completa de la frase, tempo, compás, velocidades y
microtiming. Si un rol no existe en el patrón actual, su asa aparece desactivada.

## 6. Variaciones desde Max for Live

El plugin interpreta la nota MIDI 127 del canal 16 como `Regenerate Unlocked`.
No la reenvía ni la usa como información armónica.

`ableton/PulsoBridge.maxpat` es un patch Max que emite ese comando. Para usarlo:

1. Crea un Max MIDI Effect antes de PULSO.
2. Abre el dispositivo en Max.
3. Abre o copia el contenido de `PulsoBridge.maxpat`.
4. Asegúrate de que `pulso_bridge.js` esté en la misma carpeta o en el search path.
5. Guarda el dispositivo como `.amxd` en tu User Library.

El puente es opcional: el botón de la interfaz del VST realiza la misma acción.

## Activar GPT

Ejecuta `scripts/configure-openai.ps1`, cierra Live y vuelve a abrirlo. PULSO lee
`OPENAI_API_KEY` únicamente en el worker de generación. No hay red en el hilo de audio.
Las instrucciones y notas bloqueadas se envían al servicio; el audio no se envía.

## Limitaciones conocidas

- Live no ofrece a un VST acceso general al contenido de todas las pistas.
- El MVP aprende la armonía del MIDI que llega a su propia instancia.
- El canal de batería 10 puede requerir
  remapeo de canal.
