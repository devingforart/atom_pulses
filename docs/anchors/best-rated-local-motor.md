# Motor principal local: referencia mejor puntuada

Desde el 26/10/2026, `scripts/start-local-studio.ps1` inicia por defecto el
motor aislado en el commit `99798fb` (`candidate-club-2026-10-24`). El codigo
fuente se mantiene en `build/anchor-best-rated-source` y el worker compilado
en `build/anchor-best-rated-build`. El script de preparacion verifica el commit
y recompila solo si falta el ejecutable o se solicita `-Rebuild`.

El estudio local habitual es `http://localhost:4177/`. Para abrir el motor
experimental posterior de forma explicita: `scripts/start-local-studio.ps1
-Engine Experimental -Port 4301`. La carpeta de datos y las evaluaciones son
compartidas; ninguna obra ni puntuacion fue borrada.

## Que se esta recuperando

- `1e8675dd-3ed5-49ef-b125-3b3b02277fd0`, *Sombra de Do*: mejor
  generacion completa evaluada, 30/45, primera pasada IA.
- `663cc430-f245-4c58-b49b-dcfa143273e5`, *Sombra de Do - A/B frase
  armonica 9-12*: 36/45, pero una revision focalizada de cuatro compases
  sobre la obra anterior, no una demostracion del motor generico.

El worker recompilado usa exactamente el codigo del anclaje, aunque su hash
binario puede diferir del ejecutable historico por la compilacion. La ruta
Club local mantiene `PULSO_AI_JOINT_ONE_GROUP=1`, como en el contexto guardado
de la generacion de referencia. La casilla de primera pasada esta activada
por defecto en esa interfaz.

Este cambio solo selecciona el motor de pruebas **locales**. No modifica el
branch `1.0` con experimentos en curso, el VST ni PULSO Cloud; cualquier
promocion a produccion requiere una decision y validacion separadas.
