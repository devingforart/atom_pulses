# Motores Cloud v1 y v1.1

Cloud guarda `engine_version` en cada solicitud. El selector está siempre
disponible, también fuera de Club; v1 es el valor inicial y el valor de las
obras previas. La preferencia queda en el borrador y en «Repetir ajustes».
La versión acompaña a la cola y a todos los reintentos de checkpoint.
Un identificador desconocido se rechaza; no hay sustitución silenciosa.

## Versiones congeladas

- **v1**: binario exacto del despliegue `ef945acd4daf2d3d61b84c732a5d3ab8e810588b`,
  SHA256 `040c4fe0915be8eb3340f30e0aa3891093f1a317f0227142fc2ce0dcaa34ac32`.
- **v1.1**: fuentes del motor `saved_1.1`, commit
  `80f896ce75b0c905258d50b29cb18caa1ec7c717`; revisión activada y
  `PULSO_AI_CLUB_ENSEMBLE_SCENES=1`, como en la última prueba local.
  El hash Linux se registra durante el despliegue, no se compara con el .exe.

Los ejecutables se guardan en el volumen persistente, bajo
`PULSO_CLOUD_OUTPUT_DIR/engines/{version}/pulso_cloud_worker`, nunca en las
carpetas descargables de obras. `cloud-engine-profile.json` congela el perfil
base de Cloud; la configuración de v1.1 se aplica por versión. Las claves se
inyectan en ejecución y no están en los binarios ni en los perfiles.

El despliegue verifica el hash de v1 antes de copiarlo y no sobrescribe una
versión publicada con un binario diferente. Conserva ambas al reemplazar la
imagen API. `engine-v1.1-sources.sha256` comprueba que las fuentes del worker
siguen siendo las guardadas. Un futuro motor debe recibir otra versión y su
propio registro/perfil, no reemplazar v1.1 detrás de la misma etiqueta.

## Interfaz y compatibilidad

Solo en Cloud se oculta «Evolución por capas». Las nuevas solicitudes se
normalizan a `auto` en el backend, incluidos borradores antiguos y repetición
de ajustes. No se reescribe el historial de solicitudes anteriores. El control
«Club: recurso principal libre» se mantiene. Los controles locales no cambian.

La migración 0013 añade la columna con valor v1 y restricción de valores.
No modifica notas, obras, créditos ni licencias. El despliegue comprueba que
no haya trabajos activos antes de sustituir la API y difiere la activación
si los hay. La imagen anterior queda disponible como rollback.

Verificación: pruebas React de selección/repetición, compilación frontend,
pruebas Rust de perfiles y aislamiento de versiones, y validación de fuentes
y hashes en el despliegue. No se generan obras pagadas como prueba automática.
