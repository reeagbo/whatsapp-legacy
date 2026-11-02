# WA Core fase 12 — núcleo de vinculación C99

Requisitos locales: compilador C99, OpenSSL, CMake. Ejecutar `./build.sh`.
Incluye archivo fuente oficial libsignal-protocol-c 2.3.3 de Debian, sin descargas de Internet.

## Probado
- TLS/WebSocket: codificación y validación local del protocolo; no conexión remota.
- Noise: intercambio de claves y certificados (rechazo de mensajes malformados; falta respuesta real positiva).
- ClientFinish: cifrado AES-GCM, activación de claves transporte y empaquetado.
- ClientPayload de registro: constructor C99 de campos Protobuf (UserAgent, WebInfo, DevicePairingRegistrationData y DeviceProps); comprobaciones de formato y tamaño.

## Falta para una vinculación real
- Generación y almacenamiento persistente de claves Signal de identidad y preclave firmada.
- Integrar wa_registration_payload en probe.c y enviar ClientFinish solo tras autenticar ServerHello.
- Decodificar respuestas cifradas del servidor y gestionar QR/código de enlace, pruebas de vinculación.
- Conectividad de red desde entorno de ejecución.

IMPORTANTE: la prueba de registro usa material de claves sintético y NO prueba que WhatsApp acepte un registro. No utilices esta versión con cuentas reales.
El constructor contiene la versión predeterminada de Baileys 2.3000.1043857760 (consulta 2026-11-02); esto es una dependencia de compatibilidad que más adelante será configuración. La presencia de campos Protobuf ha sido contrastada contra un extracto del esquema de WAProto, no mediante prueba de interoperabilidad real.

Referencias:
- https://github.com/WhiskeySockets/Baileys/blob/master/src/Utils/validate-connection.ts
- https://github.com/WhiskeySockets/Baileys/blob/master/src/Utils/noise-handler.ts
- https://github.com/jfelipesjc/wa-go/blob/v0.1.10/internal/waproto/waproto.proto

Licencia libsignal-protocol-c: GPL-3.0.
