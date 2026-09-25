# PULSO Web

Sitio comercial y portal de clientes de PULSO. React entrega la interfaz; la API Rust mantiene identidad, suscripciones, derechos de descarga y la conexión privada con GitHub Releases.

## Qué está incluido

- Landing, producto, planes, registro, acceso, cuenta y descarga responsive.
- Sesiones revocables mediante cookie `HttpOnly`, `SameSite=Lax` y token almacenado únicamente como SHA-256.
- Contraseñas Argon2id y PostgreSQL con migraciones automáticas.
- Stripe Checkout para Studio perpetuo y Cloud mensual/anual, Customer Portal y webhooks firmados e idempotentes.
- Email verificable, recuperación de contraseña sin enumeración de cuentas y envío mediante Resend.
- Activación de hasta dos dispositivos, revocación y licencia offline Ed25519 de 30 días.
- Manifiesto estable/beta firmado para comprobar actualizaciones sin autoactualizar dentro del DAW.
- Consulta de la última GitHub Release y proxy de descarga autorizado, compatible con repositorios privados.
- Imagen Docker multi-stage, Compose local y CI para React, Rust, contenedor y releases del VST.

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
- En Stripe registra `https://TU_DOMINIO/api/billing/webhook` para `checkout.session.completed`, `customer.subscription.created`, `customer.subscription.updated` y `customer.subscription.deleted`.
- Activa Stripe Customer Portal, facturas, impuestos y los métodos de pago deseados. No habilites los precios Cloud hasta desplegar y auditar el gateway de IA.
- Para un repositorio privado, usa un fine-grained `GITHUB_TOKEN` con acceso de lectura a Contents de este repositorio.
- Cambia los textos legales provisionales por políticas revisadas para la empresa y jurisdicción reales.
- Antes de publicar, carga `WINDOWS_CERTIFICATE_BASE64`, `WINDOWS_CERTIFICATE_PASSWORD` e `INNO_SETUP_LICENSE_KEY` como secretos de GitHub; el workflow se detiene si la firma o la licencia comercial del compilador no están configuradas.

## Releases sincronizadas

El tag debe coincidir con `CMakeLists.txt`: para PULSO 0.58.38, publica `v0.58.38`. El workflow `release.yml` construye Windows, genera instalador y ZIP, verifica checksums, SBOM, procedencia y firmas, y los adjunta a GitHub Releases. La web consulta `/releases/latest`; no contiene una versión hardcodeada.

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
