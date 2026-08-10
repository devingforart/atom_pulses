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
3. Activa la monitorización o arma la pista.
4. Inicia el transporte.
5. Toca y mantén un acorde.

El sintetizador de `Preview` permite escuchar el patrón sin cargar otro instrumento.
La armonía se actualiza a partir de las notas sostenidas y se aplica en el siguiente
compás.

## 3. Construir una frase armónica

1. Selecciona `Phrase: 4 bars` y `Mode: Loop`.
2. Crea un clip MIDI de cuatro compases con, por ejemplo, Cm | Ab | Fm | G.
3. Deja cada acorde sostenido desde el primer tiempo de su compás.
4. Reproduce una vuelta completa para que PULSO capture las cuatro posiciones.
5. Escucha la segunda vuelta: el motivo se conserva, los apoyos siguen cada acorde y
   el cuarto compás prepara el regreso a Cm.
6. Cambia a `Evolve`: las vueltas siguientes alteran detalles, no la identidad central.

Para una comparación clara, usa primero `Repeat` alto, `Complex` medio y `Develop`
medio. Baja `Repeat` para obtener más contraste; sube `Develop` para una cadencia o fill
más marcado. `New Variation` crea otra identidad completa.

## 4. Enviar el MIDI a otro instrumento

1. Conserva PULSO en la primera pista.
2. Crea una segunda pista MIDI y carga el instrumento deseado.
3. En `MIDI From`, selecciona la pista que contiene PULSO.
4. En el segundo selector de entrada elige la salida del plugin cuando Live la muestre.
5. Pon `Monitor` en `In` o arma la segunda pista.
6. Desactiva `Preview` en PULSO si solo quieres escuchar el instrumento externo.

Para convertir el resultado en un clip, graba la segunda pista. Esta ruta conserva
notas y velocidades como MIDI normal.

## 5. Variaciones desde Max for Live

El plugin interpreta la nota MIDI 127 del canal 16 como el comando `New Variation`.
No la reenvía ni la usa como información armónica.

`ableton/PulsoBridge.maxpat` es un patch Max que emite ese comando. Para usarlo:

1. Crea un Max MIDI Effect antes de PULSO.
2. Abre el dispositivo en Max.
3. Abre o copia el contenido de `PulsoBridge.maxpat`.
4. Asegúrate de que `pulso_bridge.js` esté en la misma carpeta o en el search path.
5. Guarda el dispositivo como `.amxd` en tu User Library.

El puente es opcional: el botón de la interfaz del VST realiza la misma acción.

## Limitaciones conocidas

- Live no ofrece a un VST acceso general al contenido de todas las pistas.
- El MVP aprende la armonía del MIDI que llega a su propia instancia.
- La escritura directa de clips requerirá un dispositivo Max for Live más profundo.
- El modo `Percussion` utiliza canal MIDI 10; algunos instrumentos pueden necesitar
  remapeo de canal.
