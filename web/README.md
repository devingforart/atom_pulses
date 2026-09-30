# PULSO Web

Sitio comercial y portal de clientes de PULSO. React entrega la interfaz; la API Rust mantiene identidad, suscripciones, derechos de descarga y la conexión privada con GitHub Releases.

## Qué está incluido

- Landing, producto, planes, registro, acceso, cuenta y descarga responsive.
- Sesiones revocables mediante cookie `HttpOnly`, `SameSite=Lax` y token almacenado únicamente como SHA-256.
- Contraseñas Argon2id y PostgreSQL con migraciones automáticas.
- Stripe Checkout alojado para Studio perpetuo, factura postcompra, Customer Portal y webhooks firmados e idempotentes.
- Órdenes persistentes conciliables con Price, PaymentIntent, factura, importe, moneda e impuestos.
- Pagos demorados, reembolsos y disputas sincronizados con el acceso a la licencia.
- Códigos de promoción de Stripe y canje auditable de la campaña gratuita SouthAtoms.
- Email verificable, recuperación de contraseña sin enumeración de cuentas y envío mediante Resend.
- Activación de hasta dos dispositivos, revocación y licencia offline Ed25519 de 30 días.
- Manifiesto estable/beta firmado para comprobar actualizaciones sin autoactualizar dentro del DAW.
- Consulta de la última GitHub Release y proxy de descarga autorizado, compatible con repositorios privados.
- Imagen Docker multi-stage, Compose local y CI para React, Rust, contenedor y releases del VST.

### PULSO Cloud beta

Para una prueba privada, deja `PULSO_CLOUD_BETA_STUDIO=false` y define
`PULSO_CLOUD_BETA_EMAILS` con los correos verificados autorizados, separados
por comas. Asi Cloud no se habilita para todos los compradores de Studio. Usa
una clave de proyecto OpenAI dedicada con limite de gasto antes de iniciar
solicitudes reales.

La ruta `/cloud` permite a una cuenta autorizada solicitar una composición en segundo plano, cerrar el navegador y volver a descargar cada pista MIDI no vacía. La API Rust guarda la cola, el estado, la idempotencia y el manifiesto en PostgreSQL. Un proceso C++ aislado reutiliza exactamente `AiComposer`, `SongComposer` y `MidiExporter` del VST. La salida MIDI permanece en el volumen persistente `pulso-cloud-jobs`.

Desde 0.58.56, VST y Cloud también comparten `SongGenerationPipeline`: la preparación del prompt, el cálculo de duración en compases, la finalización del plan y el contexto del render. La prueba `pulso_pipeline_tests` comprueba sin llamadas pagas que una misma solicitud y un mismo plan rinden las mismas notas. La información de Live, cuando existe, es una entrada adicional del VST; y dos respuestas independientes de la IA no prometen un MIDI idéntico.
La imagen de Cloud compila y ejecuta las pruebas del núcleo y de paridad en Linux antes de empaquetar el worker; un fallo interrumpe la construcción.

Para probar la beta con licencias Studio, configura `OPENAI_API_KEY` exclusivamente en el entorno del servidor y `PULSO_CLOUD_BETA_STUDIO=true`. Por defecto la beta está cerrada y cada cuenta tiene dos trabajos por 24 horas y uno simultáneo. `PULSO_CLOUD_DAILY_JOBS` permite ajustar el límite. Los planes Cloud de Stripe continúan deshabilitados hasta fijar precios y presupuestos a partir de consumos medidos.

Una interrupción del worker se marca como fallida; no se repite una solicitud de IA automáticamente porque ello podría duplicar el cobro. Los resultados terminados, el progreso y la solicitud idempotente sí sobreviven al cierre del navegador. La integración de activación del VST con esta cola y la reanudación interna de fases todavía son trabajo pendiente; no se debe comercializar Cloud como terminado hasta verificarlos.

## Puesta en marcha local

1. Instala Node 24+, Rust estable, Docker Desktop y Stripe CLI.
2. Copia `.env.example` como `.env` y completa valores de prueba.
3. Crea en Stripe Studio (pago único). Cloud mensual/anual queda opcional hasta desplegar el gateway administrado.
4. Ejecuta `stripe listen --forward-to localhost:8080/api/billing/webhook` y copia el `whsec_...` temporal.
5. Inicia todo con `docker compose up --build` y abre `http://localhost:8080`.

Para desarrollo independiente:

```powershell
docker compose up postgres -d
npm.cmd --prefix frontend install
npm.cmd --prefix frontend run dev
cargo run --manifest-path backend/Cargo.toml
```

Vite reenvía `/api` a `localhost:8080`.

## Despliegue en servidor

La página autenticada `/cloud` incluye la misma vista de partitura y monitor
senoidal que el estudio local. Ambos importan `web/shared/midi.mjs` y
`web/shared/audio-engine.mjs`: una nueva versión del frontend debe copiar
`web/shared` durante el build (incluido en `web/Dockerfile`). El navegador
descarga cada MIDI exclusivamente por la ruta autenticada de su propietario;
escuchar una obra guardada no crea otra composición ni consume la API de IA.

El modo de autoría y la semilla se envían explícitamente al worker Cloud.
La migración `0006_cloud_authorship.sql` conserva el render anterior para
trabajos existentes y clientes antiguos; la nueva interfaz selecciona
`IA soberana` por defecto, igual que el estudio local. Ningún cambio de
sonido altera los MIDI exportados.

`docker-compose.production.yml` mantiene PostgreSQL en una red interna y publica la API únicamente en `127.0.0.1:8188`. Copia `.env.production.example` como `.env`, completa los secretos en el servidor y coloca Nginx delante usando `nginx-pulso.conf.example` como base. No abras 8188 en el firewall.

```bash
docker compose -f docker-compose.production.yml config
docker compose -f docker-compose.production.yml up -d --build
curl --fail http://127.0.0.1:8188/api/health
```

## Configuración de producción

- Usa HTTPS y deja `SECURE_COOKIES=true`.
- Define un `PUBLIC_URL` canónico sin barra final.
- Guarda los secretos en el gestor del proveedor, nunca en GitHub ni en el frontend.
- Genera `PULSO_LICENSE_SIGNING_KEY` con 32 bytes aleatorios codificados como 64 caracteres hexadecimales y consérvala sólo en el gestor de secretos.
- Configura `RESEND_API_KEY` y valida el dominio usado por `MAIL_FROM`.
- En Stripe registra `https://TU_DOMINIO/api/billing/webhook` para `checkout.session.completed`, `checkout.session.async_payment_succeeded`, `checkout.session.async_payment_failed`, `checkout.session.expired`, `charge.refunded`, `refund.created`, `refund.updated`, `charge.dispute.created`, `charge.dispute.closed`, `invoice.paid`, `invoice.payment_failed`, `customer.subscription.created`, `customer.subscription.updated` y `customer.subscription.deleted`.
- Studio usa una factura de Stripe creada por Checkout. Configura el producto como software descargable y confirma con asesoría fiscal si corresponde uso personal (`txcd_10202000`) o comercial (`txcd_10202003`) antes de activar live mode.
- Define `PULSO_SOUTHATOMS_PROMO_CODE` y `PULSO_PROMOTION_UPDATES_DAYS` en el entorno de producción. El valor inicial `SouthAtoms` concede Studio y 365 días de actualizaciones una vez por cuenta; rota o elimina la variable al cerrar la campaña.
- Activa Stripe Customer Portal, facturas, impuestos y los métodos de pago deseados. No habilites los precios Cloud hasta desplegar y auditar el gateway de IA.
- Para un repositorio privado, usa un fine-grained `GITHUB_TOKEN` con acceso de lectura a Contents de este repositorio.
- Cambia los textos legales provisionales por políticas revisadas para la empresa y jurisdicción reales.
- La beta de Windows usa NSIS y no requiere secretos de firma ni licencia comercial del compilador. El instalador se publica sin firma digital y debe identificarse como tal; Windows SmartScreen puede mostrar una advertencia.

## Releases sincronizadas

El tag debe coincidir con `CMakeLists.txt`: para la versión `X.Y.Z`, publica `vX.Y.Z`. El workflow `release.yml` construye Windows, genera instalador NSIS y ZIP, verifica checksums, SBOM y procedencia, y los adjunta a GitHub Releases. La firma digital se pospone hasta contar con un certificado comercial. La web consulta `/releases/latest`; no contiene una versión hardcodeada.

La descarga requiere una licencia perpetua Studio o una suscripción Cloud `active`/`trialing`. El backend pide el instalador a GitHub con su token y lo transmite sin exponer credenciales.

## Comandos de calidad

```powershell
npm.cmd --prefix frontend ci
npm.cmd --prefix frontend run lint
npm.cmd --prefix frontend test
npm.cmd --prefix frontend run build
cargo fmt --manifest-path backend/Cargo.toml -- --check
cargo clippy --manifest-path backend/Cargo.toml --all-targets -- -D warnings
cargo test --manifest-path backend/Cargo.toml
```
