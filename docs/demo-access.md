# Acceso demo y encuestas

Implementado en la API de PULSO y su interfaz Cloud. No se desplegó al servidor.
`pulso_growth` sigue siendo la aplicación de marketing: no decide permisos ni cuenta generaciones.

## Política

- Licencias promocionales activas, incluido SouthAtoms: 3 obras en 24 horas, 7 en 7 días y 12 en 30 días. Son ventanas móviles, no reinicios a medianoche. Los tres límites se aplican simultáneamente.
- Solo cuentan obras completadas. Una solicitud en curso reserva disponibilidad; fallar o cancelar libera esa reserva. Se mantiene una composición simultánea por cuenta.
- `devingforart@gmail.com` tiene uso ilimitado y sin descuento de créditos; sigue requiriendo autenticación y correo verificado.
- Una licencia Studio comprada o suscripción activa no se convierte en demo al canjear el código. Sus condiciones de pago continúan vigentes.
- Canjear otra vez el código no reinicia los contadores. La API aplica los límites incluso si se modifica el navegador.

## Encuestas

Opcionales, después de la primera obra completada. Se vuelven a ofrecer cuando transcurrieron al menos 7 días y se completaron 3 obras adicionales. «Más tarde» aplaza 24 horas. No se muestran automáticamente durante la escucha o composición.

Incluyen experiencia general, calidad musical y facilidad de uso (1–5), qué gustó y qué falta. Se guardan privadamente en PostgreSQL con cuenta, fecha, idioma y referencia a la última obra. No se publican como testimonios ni se envían automáticamente a marketing.

El propietario puede revisarlas y exportarlas en `/admin/demo-feedback`. La autorización se comprueba también en el servidor. Exportación paginada de 50 respuestas. El envío manual admite como máximo una respuesta nueva diaria; los reintentos del mismo envío son idempotentes.

## Probar ahora en esta PC

Abrir **http://localhost:4320/login**. Contraseña de todas las cuentas ficticias de este laboratorio: `Pulso-Demo-Local-2026!`. No es una contraseña de producción.

| Cuenta local | Escenario |
|---|---|
| demo@pulso.invalid | Demo disponible |
| limite-diario@pulso.invalid | Límite diario alcanzado |
| limite-semanal@pulso.invalid | Límite semanal alcanzado |
| limite-mensual@pulso.invalid | Límite mensual alcanzado |
| devingforart@gmail.com | Propietario sin límites y revisión de encuestas |

La aplicación usa la API y autenticación reales con una **base de datos aislada**. No usa cuentas, claves ni obras de Cloud. El ejecutor musical y los pagos están desactivados en este laboratorio para probar cuotas sin gastar en IA. El estudio musical habitual permanece independiente.

1. Entrar con la cuenta demo y enviar una composición.
2. Desde la raíz del proyecto, simular su finalización:

```powershell
node tools/demo-lab.mjs complete demo@pulso.invalid
```

Esto publica una muestra anterior, claramente etiquetada como muestra de laboratorio, no una nueva composición. El navegador actualiza el contador y ofrece la encuesta. Para simular un error sin consumir cuota:

```powershell
node tools/demo-lab.mjs fail demo@pulso.invalid
```

Guardar la encuesta y entrar como propietario para verla en http://localhost:4320/admin/demo-feedback.

## Reiniciar y verificar

```powershell
powershell -ExecutionPolicy Bypass -File scripts/start-demo-lab.ps1
node tools/demo-lab.mjs seed
node tools/demo-lab.mjs verify
```

El script de arranque requiere el frontend construido y el backend debug. PostgreSQL portátil ya está preparado en `build/demo-lab/postgres/pgsql`; sus datos están en `build/demo-lab/pgdata`, puerto 55432, base `pulso_demo_lab`. La escucha está limitada a loopback; no exponer estos puertos. El modo laboratorio exige explícitamente esa base local. Los binarios portátiles proceden de [EnterpriseDB](https://www.enterprisedb.com/download-postgresql-binaries).

`verify` prueba la API contra PostgreSQL real: límites, concurrencia, reintentos, fallos, expiración, propietario, cuentas pagadas y encuestas privadas. Crea cuentas ficticias adicionales; no borra datos del usuario.

## Archivos principales

- `web/backend/src/demo.rs`: política y encuestas.
- `web/backend/migrations/0012_demo_feedback.sql`: persistencia e índices.
- `web/backend/src/cloud.rs`: admisión transaccional de composiciones.
- `web/frontend/src/components/DemoAccess.tsx`: cuotas y encuesta en español, inglés y portugués.
- `web/frontend/src/pages/DemoFeedback.tsx`: revisión privada.

Al desplegar posteriormente, mantener `PULSO_DEMO_LAB` desactivado. Las migraciones se aplican mediante el arranque habitual del backend. Los límites también contemplarán las obras completadas previamente por cuentas promocionales dentro de cada ventana. El usuario aún no autorizó ese despliegue.
