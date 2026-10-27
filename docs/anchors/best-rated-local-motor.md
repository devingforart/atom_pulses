# Motor principal local: referencia mejor puntuada

Desde el 26/10/2026, `scripts/start-local-studio.ps1` inicia por defecto el
motor aislado en el commit `7184327`. Parte del anclaje `99798fb`, incorpora
la revision armonica focalizada de `7c407ca` al flujo general y da mas tiempo
a una respuesta remota en cola sin hacer una segunda llamada. El codigo fuente se mantiene
en `build/anchor-harmonic-review-source` y el worker compilado en
`build/anchor-harmonic-review-build`. El script de preparacion verifica el
commit y recompila solo si falta el ejecutable o se solicita `-Rebuild`.

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

El worker recompilado usa el codigo de `7184327`, aunque su hash
binario puede diferir del ejecutable historico por la compilacion. La ruta
Club local mantiene `PULSO_AI_JOINT_ONE_GROUP=1`, como en el contexto guardado
de la generacion de referencia. La casilla de primera pasada esta activada
por defecto, pero la revision armonica focalizada corre antes de presentar
esa primera pasada si detecta un hueco real en el colchon principal. No hay
una regla que copie el MIDI historico ni que garantice 36/45 en nuevas obras.

Prueba de integracion local: al retomar la partitura `1e8675dd` con el motor
generalizado, el trabajo `cd43fc48-bc47-4e20-aeb9-03839fc64d50` reescribio
solo el plano sostenido en los compases 9-12 (11 notas sustituidas por 16).
Las otras nueve pistas y los compases exteriores conservaron sus notas.
El resultado fue publicado y queda pendiente su evaluacion auditiva: superar
las pruebas tecnicas no le adjudica automaticamente la puntuacion 36/45.

La ruta normal de primera pasada tambien se probo retomando exactamente la
obra fuente. El trabajo `dffd984a-d148-4164-932f-775d7a012889` publico
una revision IA diferente: sustituyo 11 notas por 12 solo en el plano
sostenido de los compases 9-12; las otras nueve pistas quedaron intactas.
Una tentativa anterior agoto el tiempo de transporte y conservo el original,
por eso se amplio de 90 a 150 segundos el presupuesto de esa unica respuesta
dentro del limite total de cuatro minutos de la revision focalizada.

El motor validado se integro en `1.0` mediante el merge `06c8fc9` y se
desplego en PULSO Cloud desde la revision exacta `7184327`. El contenedor
paso sus pruebas C++ durante el build; luego se verificaron el health check
publico, la pagina Cloud, el ejecutable del worker y el perfil que activa
`PULSO_AI_HARMONIC_PHRASE_REVIEW=1`. No se lanzo una composicion de pago desde
una cuenta de usuario como prueba de produccion. El VST no se cambio.

Los experimentos no promovidos se conservaron en `experiments-20261026`.
Las planillas y scripts de nutricion pertenecen a otro proyecto y siguen
locales, sin incluirse en la release ni en los commits de Pulso.
