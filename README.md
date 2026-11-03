# WhatsApp Legacy — Android + C99

Cliente experimental propio para Android 5.0+ (API 21), ARMv7, ARM64 y x86_64.
Sin Node.js, Termux ni navegador. C99 implementa Noise, Protobuf, nodos binarios,
claves Signal y el intercambio de vinculación. Kotlin proporciona la interfaz y
OkHttp el transporte TLS/WebSocket con la verificación estándar de certificados
y de nombre del servidor, sin excepciones de confianza.

## Versión 0.4-debug: código sin espera de IQ

Corrige la espera indefinida observada en el log de 0.3: el código es local y se
muestra después de que WebSocket acepte el envío; no requiere una respuesta IQ.
No se considera una vinculación completada hasta verificar la identidad y la
confirmación final. Las cadenas, tokens y JID usan la codificación canónica del
cliente de referencia. Se mantiene el ping WA cifrado y se desactiva el ping
adicional del WebSocket. Los logs correlacionan solicitudes sin revelar sus IDs.

## Registro de diagnóstico

Incluye **Guardar log**, **Ver log** y **Copiar log**. Después de reproducir el fallo,
guarda el TXT y adjúntalo al chat. El log conserva marcas de tiempo UTC, versión,
ABI/API, DNS, conexión TCP/TLS, apertura del WebSocket, fases de Noise y metadatos
de las respuestas binarias. Persiste entre aperturas y se limita a 256 KiB.
No registra teléfonos, códigos, JID, claves, certificados, cabeceras ni paquetes.
Los eventos nativos usan listas de nombres permitidos y hay pruebas de redacción,
recorte, reapertura y exportación en memoria si falla el almacenamiento.

El APK incorpora la versión en su nombre. La firma usa una ruta explícita `.signing/debug.keystore`
y Actions conserva ese archivo en caché. La ruta de caché anterior no existía en
el runner, por lo que 0.2 no conservó su clave y requiere desinstalarla para pasar a 0.3.

## Vinculación por código

Introduce tu número internacional sin `+` y pulsa **Solicitar código**. El código
se prepara localmente y se muestra al enviar la solicitud, sin esperar un IQ result. En WhatsApp
principal abre **Dispositivos vinculados → Vincular dispositivo → Vincular con
número de teléfono** e introduce el código.

Tras verificar la identidad, el servidor solicita un reinicio (515). La app
reconecta automáticamente con las claves guardadas; solo una respuesta de login
`success` confirma la autenticación de la sesión. El reinicio no es por sí solo
una confirmación final. Un nuevo código permite repetir una vinculación rechazada
conservando las claves de largo plazo. Guarda claves y certificado en su directorio privado, con
archivos de modo 0600 y escritura atómica. No incluye datos de cuenta en logs,
artefactos de Actions ni el repositorio. No puede enviar o recibir mensajes.

**Estado de validación:** las pruebas locales incluyen un móvil principal
simulado que descifra el paquete, las firmas de identidad y rechazo de HMAC
falsos. La prueba real con 0.4 verificó HMAC y firma de la cuenta y recibió el reinicio
515, pero después el móvil principal notificó un fallo y el login manual recibió
401. En 0.5 se elimina la espera manual antes de reconectar; queda pendiente
validar el resultado con el móvil principal.
`Comprobar sesión guardada` prueba la autenticación con la identidad existente.
Sin una identidad vinculada, solo verifica el canal de registro sin usar cuenta.

La APK 0.1 fue firmada con una clave debug efímera: puede ser necesario
desinstalarla una vez antes de instalar 0.2. Desde esta versión, Actions conserva
la clave de desarrollo en caché para facilitar actualizaciones. Si GitHub pierde
esa caché, la firma puede cambiar. No desinstales una sesión vinculada sin
considerar que Android borrará sus claves privadas.

## Compilación y pruebas

Cada push ejecuta `.github/workflows/android.yml`: pruebas C99, sonda online sin
número de teléfono (diagnóstica, no bloqueante), compilación y Android Lint.
Descarga el artefacto con versión `whatsapp-legacy-0.5-debug` de la ejecución correcta.

Para pruebas locales: `bash core/build.sh`. Requiere CMake, compilador C99,
OpenSSL y zlib de desarrollo. Para la sonda online sin cuenta: `core/wa_probe`.

Android: AGP 8.7.3, Gradle 8.9, Kotlin 2.0.21, JDK 17, NDK 27.2.12479018.
Instala los SDK/NDK fijados en el workflow, define `ANDROID_NDK_HOME`, ejecuta
`bash scripts/build-native-deps.sh` y después `gradle :app:assembleDebug`.

El núcleo inicial procede de `wa_core_phase12.zip`. La interfaz Android se
reconstruyó al no estar guardada la mencionada en la conversación anterior.
Ver `THIRD_PARTY.md` y `LICENSE` para las dependencias y referencias del protocolo.
