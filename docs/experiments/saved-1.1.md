# saved_1.1 — referencia estable local

Snapshot solicitado por el usuario del motor que lanzó la prueba
`e8b61e3b-2644-4d4a-8d23-13c741a4b4ff`, posterior a
`d3a0f3fa-f072-41eb-9e2b-7cb0b99b8677`. No afirma una aprobación auditiva
de la última prueba: es un punto de recuperación del motor actual.

- Rama de referencia: `saved_1.1`.
- Rama de desarrollo a la que se vuelve: `v1.2`.
- Worker local: `build/recovered-club-controls-build/Release/pulso_cloud_worker.exe`.
- SHA256 del binario guardado en este entorno:
  `F2476F321707BC92ABEEA181FFC13C9842CB836919095241CB7D40A56FD0A838`.
- Configuración adicional local: `PULSO_AI_CLUB_ENSEMBLE_SCENES=1`.
  El arranque Recovered la activa explícitamente para no depender de la sesión
  anterior de PowerShell. No cambia el perfil compartido de Cloud.
- Solicitud comparativa: `etude DMAJ`, 180 segundos, 120 BPM, Club,
  semilla `1242456789046076`, IA soberana, cromatismos desactivados,
  solo voces tonales, revisión activada, horizonte auto y recurso libre.

Arranque habitual: `scripts/start-local-studio.ps1 -Port 4312 -Engine Recovered`.
Los binarios, credenciales, obras generadas y recibos de build son artefactos
locales, no se incluyen en Git. El código del worker y las pruebas sí quedan
versionados; una compilación en otro equipo puede producir un hash diferente.

Este guardado no despliega producción, no cambia la rama predeterminada de
GitHub y no incluye los cambios pendientes de marketing, cobros o demos.
El criterio y alcance musical están en `club-musical-development-standard.md`.
