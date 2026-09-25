# Distribución de PULSO

## Objetivo

PULSO debe distribuirse como un producto comercial completo, no únicamente como un archivo VST3 dentro de un ZIP. La distribución debe abarcar venta, licenciamiento, instalación, actualización, seguridad, soporte y control del costo de inteligencia artificial.

La web de PULSO será el centro de la relación con el cliente. GitHub funcionará como infraestructura privada de versionamiento y releases, sin convertirse en la tienda ni en la experiencia principal de descarga.

## Modelo comercial recomendado

Actualmente PULSO puede utilizar la API key de OpenAI aportada por el usuario. En ese escenario, una suscripción obligatoria al software sería difícil de justificar, porque el cliente pagaría PULSO y también el consumo del proveedor de IA.

Se recomienda separar el producto en dos ofertas.

### PULSO Studio

Licencia perpetua orientativa: USD 149–199.

Incluye:

- VST3 y aplicación standalone.
- Integración completa con Ableton Live.
- Activación en hasta dos computadoras.
- Doce meses de actualizaciones.
- Uso comercial de las composiciones.
- Uso de una API key propia del cliente.
- Acceso a las versiones publicadas durante el período de actualizaciones.

Después de doce meses, el software continúa funcionando. El cliente solamente paga una renovación reducida si desea recibir versiones nuevas.

### PULSO Cloud

Suscripción opcional orientativa: USD 15–25 mensuales.

Incluye:

- Créditos de inteligencia artificial gestionados por PULSO.
- Uso sin configurar una API key externa.
- Historial de composiciones y proyectos en la nube.
- Generaciones prioritarias.
- Acceso a modelos premium.
- Funciones colaborativas o de continuidad entre dispositivos.
- Actualizaciones de PULSO Studio incluidas.

Esta separación permite vender una herramienta real sin convertir obligatoriamente el software en un alquiler permanente.

## Canal de distribución

```text
Web de PULSO
   ↓
Stripe
   ↓
Cuenta y licencia
   ↓
Descarga autorizada
   ↓
GitHub Release inmutable
   ↓
Instalador firmado
   ↓
PULSO + PulsoDeployRemote
```

La web debe ser el storefront principal. GitHub Releases actúa como origen versionado detrás del backend de PULSO.

Las releases inmutables de GitHub pueden generar attestations que relacionan el tag, el commit y los archivos publicados. Esto permite auditar exactamente qué código produjo cada instalador.

Referencia: [GitHub Immutable Releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases).

## Formato de entrega

El ZIP actual es apropiado para desarrollo o beta interna, pero no debería ser la entrega comercial final.

El producto público debe utilizar un instalador como:

```text
PULSO-Setup-0.58.38.exe
```

El instalador debe:

- Comprobar que Ableton Live y otros hosts estén cerrados.
- Instalar el VST3 en la ubicación estándar de Windows.
- Instalar `PulsoDeployRemote` en la versión compatible.
- Incluir la aplicación standalone.
- Incluir documentación y avisos de licencia.
- Incluir un desinstalador.
- Permitir instalación, reparación y actualización.
- Preservar preferencias y credenciales existentes.
- Detectar arquitectura y versión del sistema operativo.
- Mostrar claramente el publicador y la versión.
- Registrar la versión instalada.
- Desinstalar todos los componentes propios sin borrar proyectos del usuario.
- Verificar integridad antes de reemplazar una versión existente.

Las opciones recomendadas para construirlo son Inno Setup o WiX Toolset, integradas en GitHub Actions.

## Firma digital

Antes de vender públicamente deben firmarse todos los artefactos ejecutables:

- Instalador.
- VST3.
- Aplicación standalone.
- Desinstalador.
- Ejecutables auxiliares.
- Archivos temporales ejecutables generados durante la instalación.

Windows puede bloquear software desconocido o sin firma. Microsoft recomienda Azure Artifact Signing para distribución fuera de Microsoft Store y certificados provenientes de autoridades confiables para funcionar correctamente con Smart App Control.

Referencias:

- [Opciones de firma para desarrolladores de Windows](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options).
- [Firma compatible con Smart App Control](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control).
- [Reputación de SmartScreen](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

Azure Artifact Signing tiene restricciones geográficas según el tipo de cuenta. Si la entidad comercial o el desarrollador no califican desde Argentina, debe utilizarse un certificado OV emitido por una autoridad incluida en Microsoft Trusted Root Program.

Un certificado autofirmado solamente es válido para desarrollo interno; no es adecuado para distribución pública.

## Licenciamiento y activación

PULSO debe evitar un DRM agresivo que ponga en riesgo sesiones musicales.

El sistema recomendado es:

- Login inicial mediante el navegador.
- Token de licencia firmado y almacenado de manera segura localmente.
- Hasta dos activaciones simultáneas por licencia.
- Portal web para visualizar y desactivar computadoras.
- Funcionamiento offline durante un período de gracia de aproximadamente 30 días.
- Renovación silenciosa de la licencia cuando exista conexión.
- Ninguna comprobación de red dentro del hilo de audio.
- Ninguna interrupción de una sesión de Ableton por falta temporal de internet.
- Conservación de proyectos anteriores si la licencia o suscripción expira.

Si una licencia deja de estar vigente, PULSO debería mantener la apertura y reproducción de material existente y limitar solamente la creación de nuevas composiciones o funciones premium.

La verificación local debe usar criptografía asimétrica: el servidor firma la licencia con una clave privada y el VST solamente contiene la clave pública necesaria para validarla. Ningún secreto de activación debe quedar embebido en el plugin.

## Actualizaciones

PULSO no debe intentar reemplazar su propio VST3 mientras Ableton Live esté abierto.

Flujo recomendado:

1. El plugin consulta un manifiesto firmado de actualizaciones.
2. Compara la versión instalada con el canal elegido.
3. Informa que existe una versión nueva.
4. Abre la cuenta web del usuario.
5. El usuario descarga el instalador firmado.
6. El instalador solicita cerrar Ableton Live.
7. Verifica firma y checksum.
8. Actualiza conjuntamente el VST3 y `PulsoDeployRemote`.
9. Conserva configuración, licencia y preferencias.
10. Permite regresar a una versión anterior compatible.

### Canales

- **Stable:** versión recomendada para producción musical.
- **Beta:** versión voluntaria para usuarios que desean probar funciones nuevas.
- **Legacy:** archivo de versiones anteriores disponibles manualmente desde la cuenta.

Cada versión debe incluir:

- Número semántico.
- Fecha.
- Changelog.
- Sistemas compatibles.
- Hash SHA-256.
- Firma del publicador.
- Commit o attestation de procedencia.
- Compatibilidad mínima con Ableton Live.

## Inteligencia artificial y API keys

Nunca debe distribuirse una API key perteneciente a PULSO dentro del VST, instalador, ZIP o repositorio. Una clave embebida puede extraerse y utilizarse fuera del producto.

Existen dos modalidades seguras.

### BYOK

El usuario aporta su propia API key.

- La clave se almacena mediante Windows Credential Manager.
- La web de PULSO no recibe esa clave.
- El usuario paga directamente el consumo al proveedor de IA.
- PULSO Studio puede funcionar sin una suscripción Cloud.

### PULSO Cloud

El VST se comunica con el backend de PULSO y el backend realiza las llamadas al proveedor.

Requiere:

- Autenticación de dispositivo.
- Créditos o límites por cuenta.
- Presupuesto máximo por composición.
- Límite mensual.
- Cancelación efectiva de trabajos.
- Métricas de tokens y costo.
- Protección contra abuso.
- Reanudación mediante checkpoints.
- Cola de trabajos.
- Idempotencia para evitar cobros duplicados.
- Transparencia del consumo para el usuario.
- Alarmas de gasto y límites globales de seguridad.

La modalidad Cloud justifica una suscripción recurrente porque PULSO absorbe infraestructura, modelos, continuidad y consumo de IA.

## Pagos, impuestos y facturación

Stripe debe encargarse de:

- Checkout alojado.
- Métodos de pago.
- Facturas.
- Renovaciones.
- Cancelaciones.
- Actualización de tarjetas.
- Recuperación de pagos fallidos.
- Impuestos sobre productos digitales cuando corresponda.
- Customer Portal.

El backend de PULSO mantiene únicamente el identificador del cliente, el estado de la licencia o suscripción y los derechos de descarga. Los datos completos de tarjeta nunca deben atravesar los servidores de PULSO.

La interfaz debe mostrar precios obtenidos desde Stripe, no valores duplicados manualmente en el frontend.

## Fases de lanzamiento

### 1. Alpha privada

- Entre 20 y 30 productores seleccionados.
- Comunicación directa.
- Instalaciones controladas.
- Seguimiento de rechazos, costos, crashes y compatibilidad.
- Sin campañas públicas.

### 2. Founders Edition

- Aproximadamente 100 licencias.
- Precio reducido.
- Acceso temprano.
- Actualizaciones amplias.
- Canal privado de feedback.
- Expectativas de beta claramente comunicadas.

### 3. Beta pública

- Instalador firmado.
- Sistema de licencias.
- Portal de cuenta.
- Soporte estructurado.
- Telemetría opcional y respetuosa de la privacidad.
- Política de reembolso publicada.

### 4. PULSO 1.0

- Venta normal desde la web.
- Stable channel.
- Documentación definitiva.
- Tutorial inicial.
- Proyecto de demostración.
- Procedimiento de soporte y recuperación.

### 5. Marketplaces externos

Plugin Boutique y otros marketplaces deben considerarse solamente después de estabilizar instalación, soporte, reembolsos y versionamiento. Estos canales ofrecen alcance, pero agregan comisiones, coordinación de releases y presión operativa.

## Soporte

El producto debe incluir:

- Guía de inicio rápido.
- Video de instalación.
- Tutorial de primera composición.
- Proyecto demo de Ableton.
- Página de estado del servicio Cloud.
- Base de conocimiento.
- Changelog accesible.
- Diagnóstico de versión instalada.
- Botón para exportar un bundle de soporte.

El bundle de soporte debe incluir versiones, configuración no sensible, métricas del último proceso y logs sanitizados. Nunca debe incluir API keys, tokens de sesión, prompts privados completos ni información innecesaria del proyecto.

## Privacidad y telemetría

La telemetría debe ser opcional y explicar claramente qué registra.

Puede incluir:

- Versión instalada.
- Sistema operativo.
- Versión de Ableton.
- Resultado técnico de la generación.
- Duración del proceso.
- Tipo de error.
- Crash reports.

No debe incluir por defecto:

- Contenido completo de proyectos.
- MIDI del usuario.
- API keys.
- Credenciales.
- Nombres de archivos personales.
- Prompts completos sin consentimiento explícito.

## Compatibilidad y pruebas

Antes de cada release deben probarse:

- Windows 10 y Windows 11 limpios.
- Ableton Live 12 en configuraciones soportadas.
- Instalación nueva.
- Actualización desde la versión anterior.
- Reparación.
- Desinstalación.
- VST3 en un usuario sin permisos administrativos permanentes.
- Superficie de control.
- Proyectos guardados con versiones anteriores.
- Diferentes sample rates y buffer sizes.
- Smart App Control o políticas equivalentes.
- Instalación sin conexión, cuando sea aplicable.
- Renovación y vencimiento de licencia.
- Interrupción de red durante una composición Cloud.

## Licencias de terceros

La distribución debe incluir los avisos exigidos por JUCE, VST3 SDK y todas las dependencias correspondientes.

Steinberg permite distribuir y vender plugins VST3 en formato binario bajo las condiciones actuales de su SDK y sus avisos de licencia.

Referencia: [Licenciamiento oficial de VST3](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Licensing.html).

Antes del lanzamiento debe realizarse un inventario completo de dependencias, licencias, copyrights y obligaciones de atribución.

## Elementos necesarios antes de vender

- Instalador EXE profesional.
- Firma Authenticode confiable.
- Sistema de licencias y activaciones.
- Recuperación de contraseña.
- Verificación de email.
- Dominio y correo transaccional.
- Política de privacidad definitiva.
- Términos comerciales definitivos.
- Política de reembolsos.
- Configuración de impuestos y facturación.
- PostgreSQL y backend productivos.
- Monitoreo, backups y alertas.
- Cola segura para PULSO Cloud.
- Control de costos de IA.
- Documentación y tutoriales.
- Bundle sanitizado de soporte.
- Pruebas sobre instalaciones limpias.
- Revisión de licencias de terceros.

## Decisión recomendada

PULSO debería venderse directamente desde su web mediante una licencia perpetua de PULSO Studio con doce meses de actualizaciones. PULSO Cloud debería ofrecerse como suscripción opcional para quienes prefieran créditos de IA administrados, sincronización y servicios online.

La entrega debe realizarse mediante un instalador firmado, con licencias tolerantes al uso offline y releases inmutables provenientes de GitHub. La web debe controlar pagos, cuenta, activaciones y derechos de descarga, mientras GitHub conserva la trazabilidad técnica de cada versión.

## Estado de implementación

La infraestructura descrita en este documento está implementada en el repositorio:

- `installer/PULSO.iss` instala VST3, Standalone, documentación y PulsoDeployRemote; conserva datos de usuario y produce un desinstalador.
- `scripts/build-installer.ps1` firma binarios e instalador cuando recibe el certificado, verifica Authenticode y genera SHA-256.
- `scripts/support-bundle.ps1` genera diagnósticos sanitizados sin copiar claves ni tokens.
- La API Rust incluye compra perpetua Studio, Cloud mensual/anual, webhooks idempotentes, recuperación y verificación de cuenta, activación de dos dispositivos, revocación y licencias offline Ed25519 de 30 días.
- La cuenta web expone descargas autorizadas, dispositivos y facturación; el manifiesto de actualización estable/beta está firmado.
- `.github/workflows/release.yml` bloquea una publicación si fallan pruebas, firma cuando existen secretos, genera SBOM, atestación de procedencia, checksums y GitHub Release.

Los únicos pasos que requieren identidad o credenciales externas y no pueden resolverse desde el código son: comprar el certificado Authenticode y la [licencia comercial solicitada por Inno Setup](https://jrsoftware.org/isorder.php), configurar dominio/DNS, cargar precios y secretos reales de Stripe/Resend/GitHub, definir la clave privada de licencias, completar identidad legal y políticas revisadas, y activar la opción **Immutable releases** del repositorio. Sin esos valores el entorno local funciona, pero una release pública debe considerarse de prueba y aparecerá sin firma Authenticode. El workflow comercial exige `WINDOWS_CERTIFICATE_BASE64`, `WINDOWS_CERTIFICATE_PASSWORD` e `INNO_SETUP_LICENSE_KEY` y se detiene si falta alguno.

La API y el portal ya cubren autorización y activaciones, pero el VST debe incorporar en una siguiente versión el cliente de *device flow* y la clave pública Ed25519 para hacer cumplir la licencia offline dentro del binario. Hasta integrar y probar ese cliente en máquinas limpias, el instalador es técnicamente distribuible para beta privada, no una barrera antipiratería lista para venta pública.

PULSO Cloud aparece como **próximamente** y sus precios son opcionales. La infraestructura Stripe de suscripción existe, pero no debe habilitarse comercialmente hasta implementar el gateway de modelos, créditos, límites de costo y observabilidad; Studio BYOK es la modalidad funcional actual.
