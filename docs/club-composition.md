# Club: repetición y evolución

Club es una intención compositiva explícita, no una plantilla de notas. Se selecciona
en la Suite y se conserva como `behavior: "club"` en solicitudes, checkpoints y
manifiestos. Adaptativo, Hipnótico y Narrativo conservan sus índices y comportamiento.

## Contrato compartido

`AiClubPolicy.h` centraliza el criterio editorial que reciben macroforma, reparto,
arquitectura de frases y escritura. Los estados tienen un horizonte perceptivo de
aproximadamente 64 compases: no se insertan cortes, cadencias ni transiciones por reloj.
La IA decide cuándo conservar y cuándo transformar sus células cortas. El esquema
existente de frases describe estados de capas, sin exigir nuevas melodías constantes.
La memoria de notas anteriores incluye también bajo, stabs y arpegios en este modo.

La política v2 añade un ciclo métrico común elegido por la IA y mantiene su fase
absoluta a través de ventanas/secciones. En conjuntos Club de hasta ocho propietarios,
las voces se escriben juntas, no separadas en protagonistas y acompañantes. El
experimento y sus límites se documentan en `club-groove-nucleus.md`.

Club activa por defecto una revisión IA acotada de los voicings sostenidos. En local
se puede activar explícitamente Primera pasada para comparar sin esa revisión; el
comportamiento adaptativo conserva su preferencia anterior. El valor está en el
perfil compartido con Cloud, no en una configuración diferente del servidor.

La repetición literal deja de ser un déficit de novedad. Tampoco se impone una proporción
de movimientos por grado conjunto a un hook de club. Permanecen la autoría real, la
integridad MIDI, la compatibilidad armónica, las voces independientes, la continuidad
y el cierre. No hay duplicador nuevo de loops, generador local de notas ni reparador
determinista. La instrumentación sigue siendo exclusivamente afinada: sin baterías
ni percusiones. El permiso de cromatismos es independiente de Club.

La revisión de voicings solicita exclusivamente alturas a la IA: no cambia número de
notas, ataques, duraciones, velocidades, arpegios, stabs ni melodías. Las direcciones
de edición se resuelven por propietario y colocación real, también después de
recuperar checkpoints con otros nombres de células. Se rechazan direcciones ambiguas
o células reutilizadas que afectarían otras apariciones. Las propuestas que colapsan
notas o introducen alturas ajenas se conservan como rechazadas y reciben un reintento
acotado con diagnóstico; nunca se corrigen por transposición local automática.

La evaluación del cierre Club reconoce un retorno temático real seguido de un outro
armónico estable; no exige nuevos ataques del solista en los dos últimos compases.
Una premisa de introducción puede iniciar con bajo/arpegio antes de los pads. Esto
no exime la audibilidad, el desarrollo de voces, el clímax ni los controles técnicos.

## Pruebas

El estudio local usa `http://localhost:4214`. Las corridas del agente aparecen en Auto;
las del usuario, en Personal. El código de Cloud transporta el mismo comportamiento;
una modificación local no cambia producción hasta desplegarla explícitamente.

```powershell
ctest --test-dir build-cloud -C Release --output-on-failure
node --test tests/composer_profile_parity.test.mjs tests/local_studio_checkpoint_integrity.test.mjs
node scripts/audit-club-score.mjs build/local-studio-data/jobs/ID
```

La auditoría lee los MIDI exportados: registra recurrencia literal de patrones de
cuatro compases, ocupación, ataques polifónicos, solapamientos de una misma altura,
pulso, densidad por sección y final. No escribe archivos ni modifica música.
Sus cifras permiten detectar problemas, pero no certifican musicalidad ni sustituyen
la escucha humana. Un pad sostenido puede tener poca recurrencia de ataques y seguir
cumpliendo perfectamente su función.

Para una segunda lectura musical opcional de la obra completa ya terminada:

```powershell
node scripts/review-club-score.mjs build/local-studio-data/jobs/ID
```

Esta llamada consume API, usa el modelo configurado del compositor y guarda
`club-ai-review.json` con evidencia, rangos de compases, uso de tokens y SHA-256
del MIDI examinado. No reescribe notas ni activa otra composición. El dictamen
es una lectura simbólica del MIDI, no una afirmación de haber escuchado audio.
