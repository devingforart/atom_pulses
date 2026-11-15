# Piano MIDI de escucha

Las versiones MP3 son conversiones del mismo juego de muestras CC0, sin cambiar
su afinación, velocidad o contenido musical. Se ofrecen como alternativa para
Safari/iPhone que no pueda decodificar OGG. El reproductor comprueba capacidad
y decodificación real, y prueba el otro formato si el primero falla.

Las 16 muestras OGG de este directorio provienen de **Meta Open Sound / Meta Piano**, capa de velocidad 03, publicación `main` de https://github.com/facebookincubator/Meta-open-sound/tree/main/systems/instruments/meta-piano/ogg.

Las muestras originales y sus metadatos están publicados bajo [CC0 1.0 Universal](https://github.com/facebookincubator/Meta-open-sound/blob/main/LICENSE). Se distribuyen aquí para que la escucha en Local y Cloud funcione desde el propio sitio, sin CDN ni permiso MIDI del sistema.

El reproductor utiliza el mismo piano para todas las pistas, con transposición cromática desde la muestra más cercana, y no modifica los archivos MIDI exportados. El modo `MIDI neutro` conserva el monitor senoidal anterior.
