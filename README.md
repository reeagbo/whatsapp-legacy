# WhatsApp Legacy — Android + C99

Cliente experimental propio para Android 5.0+ (API 21), ARMv7, ARM64 y x86_64.
Sin Node.js, Termux ni navegador. C99 implementa Noise, Protobuf, nodos binarios,
claves Signal y el intercambio de vinculación. Kotlin proporciona la interfaz y
OkHttp el transporte TLS/WebSocket con la verificación estándar de certificados
y de nombre del servidor, sin excepciones de confianza.

## Diagnóstico de identidades (pendiente de compilar)

Por petición del usuario, el log incluye dominio (`lid`/`pn`) y los últimos seis
dígitos de direcciones válidas. `MESSAGE_ADDRESS` registra remitente/destinatario
y alternativas del sobre; `MESSAGE_TARGET` indica la conversación de guardado;
`CONTACT_ALIAS_*` refleja la resolución LID/número. `CHAT_IDENTITY` muestra
si conocemos el número, permiso de agenda y origen del nombre (manual, agenda,
recibido o identificador), sin registrar el nombre. `AGENDA_LOOKUP` cuenta los
candidatos, coincidencias completas y si se aceptó una. Abrir un chat genera
`CHAT_OPEN` para correlacionar el caso visible con el log. Las terminaciones
pueden coincidir entre personas y no son una prueba suficiente para unir chats.

No se registran teléfonos completos, identificadores completos, cuerpos de
mensajes ni nombres de agenda. Estas entradas aparecen con eventos nuevos o al
abrir conversaciones con la próxima versión; no reconstruyen datos omitidos por
los logs anteriores. Versión y Actions manual permanecen sin cambios.

## Cambios posteriores a 0.9 (sin compilar)

- La integración con la agenda está siempre activa. La app solicita el permiso
  Android al primer inicio que lo necesite, sin tener que activar un botón.
  Si se deniega, explica cómo habilitarlo en ajustes; nunca omite el consentimiento
  del sistema ni interrumpe la recepción de mensajes por falta de agenda.
- Se elimina «Contacto» de la etiqueta de conversación sin número conocido.
  Al abrirla, se indica «Número pendiente de identificar». Los dígitos de un
  LID no se convierten en un teléfono ni se asocian manualmente por su terminación.
- La búsqueda en agenda valida el número internacional completo de cada candidato
  de PhoneLookup; descarta coincidencias por sufijo y casos con varios contactos.
- Incidencia 187136: el log 0.9 aportado omite números e identificadores. No permite
  confirmar a qué dirección corresponde esa terminación ni probar una atribución
  errónea concreta. Hace falta distinguir un teléfono con + de un LID con ….

Se mantienen la versión 0.9 y Actions manual. No se ejecutan comprobaciones que
compilen hasta la siguiente petición expresa de una versión. La prueba de UI
concede el permiso al emulador; la solicitud, denegación y revocación quedan para
comprobación con la siguiente APK.

## Versión 0.9-debug: contactos y reconexión

La 0.8 real recibió tres textos y tres copias de otros dispositivos en el log
aportado el 3 de noviembre, con seis entregas confirmadas a SQLite. Esa sesión
no contiene errores de Signal; el error «Frame demasiado grande» anterior al
marcador `APP_OPEN version=0.8-debug` corresponde a una ejecución previa.
El usuario confirma recepción de mensajes y aparición de conversaciones.

Cambios incluidos en la versión solicitada:

- Reconexión cuando Android anuncia que vuelve la red, aunque se hayan agotado
  los tres reintentos. Una desconexión manual o un error de almacenamiento
  desactiva esa reanudación hasta pulsar Conectar. No se reenvía texto incierto.
- Relación LID/número aprendida del servidor y de las consultas de dispositivos,
  persistida en C99. Se aceptan pares entre dominios correctos y se rechaza que
  un LID ya guardado cambie de persona. Las sesiones Signal no se renombran.
- SQLite v3 une conversaciones existentes LID/número en una sola transacción.
  Conserva texto, nombres manuales, fechas y el estado de entrega más avanzado;
  el inbox pendiente se confirma con su dirección original tras el guardado.
- Nombres: manual > agenda Android autorizada > nombre recibido (`notify`) >
  número. **⋮ → Usar nombres de la agenda** solicita el permiso cuando haga
  falta. La consulta se hace fuera del hilo de interfaz y no se sube la agenda.
  Tocar el título de un chat o mantener pulsada una fila permite nombrarlo.
- Si falta una correspondencia LID/número, se conserva una etiqueta diferenciada
  por el final del identificador. No se inventa un teléfono ni se unen personas
  por compartir nombre. Los nombres recibidos se aprenden con nuevos textos;
  esto todavía no descarga la libreta sincronizada ni recupera historial.
- Un fallo al reservar memoria para texto recibido ya no confirma el paquete:
  se cierra la conexión conservando la posibilidad de reentrega del servidor.

Comprobación sin compilación: `python scripts/check-storage-sql.py` ejecuta el SQL
literal de migraciones v1/v2→v3, cierre/reapertura, rollback en cada paso de la
unión, duplicados por dirección, nombres y confirmaciones. Pasó localmente.
Las pruebas C/JVM y el emulador ampliados se ejecutan en la compilación de la 0.9.

Pendiente en móvil con la próxima versión solicitada: cerrar/abrir conservando
sesión y mensajes; cortar/restaurar Wi-Fi y datos; desconectar manualmente y
verificar que no se reconecta; recibir el mismo mensaje sin duplicarlo; aceptar,
denegar y revocar agenda; comprobar un contacto conocido por número y por LID.

Referencia de atributos del protocolo: [decode-wa-message.ts de Baileys](https://github.com/WhiskeySockets/Baileys/blob/master/src/Utils/decode-wa-message.ts).

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
artefactos de Actions ni el repositorio. La versión 0.6 incorpora texto individual cifrado y una lista local de conversaciones.

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

Solo por petición expresa del usuario se inicia manualmente
`.github/workflows/android.yml` mediante `workflow_dispatch`: pruebas C99, sonda online sin
número de teléfono (diagnóstica, no bloqueante), compilación y Android Lint.
Descarga el artefacto con versión `whatsapp-legacy-0.9-debug` de la ejecución correcta.

Para pruebas locales: `bash core/build.sh`. Requiere CMake, compilador C99,
OpenSSL y zlib de desarrollo. Para la sonda online sin cuenta: `core/wa_probe`.

Android: AGP 8.7.3, Gradle 8.9, Kotlin 2.0.21, JDK 17, NDK 27.2.12479018.
Instala los SDK/NDK fijados en el workflow, define `ANDROID_NDK_HOME`, ejecuta
`bash scripts/build-native-deps.sh` y después `gradle :app:assembleDebug`.

El núcleo inicial procede de `wa_core_phase12.zip`. La interfaz Android se
reconstruyó al no estar guardada la mencionada en la conversación anterior.
Ver `THIRD_PARTY.md` y `LICENSE` para las dependencias y referencias del protocolo.


## Conversaciones y texto (0.6)

La pantalla principal es una lista local de conversaciones. Pulsa **+**, introduce
un número internacional y opcionalmente un nombre. La pantalla de chat incluye
texto, compositor y estado por mensaje. La sesión guardada conecta al abrir la
app; la vinculación y el log están en **⋮**. Actualiza sobre 0.5 para conservar
las claves. No se importan contactos ni se descarga el historial anterior.

El núcleo mantiene la sesión abierta, publica 32 preclaves si es necesario,
activa la sesión, consulta dispositivos y direcciones LID, verifica los paquetes
firmados de claves y cifra para todos los dispositivos enumerados. Incluye la
copia DeviceSentMessage para nuestros otros dispositivos. Conserva las sesiones
Signal y fija las identidades conocidas; un cambio no se acepta silenciosamente.
El envío es explícito: no se reenvía automáticamente un mensaje cuyo resultado
se desconoce. Distingue pendiente, enviado sin confirmación, aceptado, entregado
y leído según eventos del servidor. No se mandan confirmaciones de lectura.

Alcance inicial: texto individual UTF-8 de hasta 4.000 bytes, hasta 32 dispositivos
por envío y un envío en preparación a la vez. Recibe texto directo y copias de
nuestros dispositivos. Grupos, multimedia, recuperación de historial, sincronía
de estado, dispositivos hosted y reintentos de descifrado quedan pendientes.
Los errores de consulta o restricciones de WhatsApp pueden impedir un envío;
aceptación por el servidor no equivale a entrega.

**Portabilidad:** `messaging.{c,h}` y `signal_engine.{c,h}` no conocen Android, JNI,
SQLite, sockets ni rutas de archivos. Reciben almacenamiento durable por
`wa_storage`, envío de nodos por callback y tiempo monotónico desde el adaptador.
`storage_posix` implementa la persistencia en Android/Linux y `crypto_signal`
provee la criptografía de libsignal mediante OpenSSL. La interfaz conserva las
conversaciones en SQLite (`ChatStore.kt`) y recibe eventos por JNI. El envoltorio
histórico de vinculación todavía usa el adaptador POSIX para su identidad.

Validación: intercambio sintético bidireccional, persistencia del ratchet,
preclaves, copia propia, Unicode, rechazo de cambio de identidad, estados y
entradas inválidas. ASan/UBSan sin errores. Actions compila tres ABI, ejecuta JVM
unit tests y Lint, y prueba +/validación/navegación/persistencia en un emulador.
El login real ya se confirmó en 0.5; el primer envío real de 0.6 sigue pendiente
de la prueba con el usuario. Ninguna prueba automática usa cuentas o envía mensajes.


## Recepción de paquetes grandes (0.7)

El primer envío real de 0.6 llegó al destinatario y el log recibió confirmación
de entrega. Al iniciar aparecieron dos avisos Signal `-12` después de paquetes
cifrados de unos 30/61 KiB: el receptor solo reservaba 16 KiB para el texto plano.
0.7 reserva memoria según el tamaño acotado de cada paquete y la libera al
terminar; no relaja ninguna comprobación criptográfica. Los mensajes descifrados
cuyo contenido todavía no está soportado se registran como `ignored-payload`,
sin una alerta falsa de descifrado. Esto no implementa sincronización de historial
ni permite recuperar automáticamente los paquetes ya confirmados por 0.6.
Las pruebas incluyen 30/60 KiB y texto posterior en la misma sesión Signal.


## Cambios pendientes de la próxima compilación

Modo oscuro por defecto en la lista, chat, burbujas, compositor, formularios,
diálogos y barras del sistema. Paleta centralizada en `res/values/colors.xml` y
tema nativo compatible con Android 5.0. No se ha incrementado la versión ni
compilado este cambio. Las compilaciones quedan exclusivamente bajo petición.


## Recepción y estabilidad: cambios pendientes de validación

Sin incrementar versión ni compilar, se prepara una bandeja de entrada durable
(C99, almacenamiento por callbacks) para evitar la pérdida entre descifrado y
SQLite. El texto soportado se escribe antes de confirmar el avance del ratchet
Signal y antes de confirmar la recepción por red. La bandeja se recupera al
reconectar y solo se limpia tras la confirmación explícita del commit de SQLite.
El formato usa longitudes explícitas y checksum SHA-256, con hasta 32 entregas
pendientes. Si falla el guardado o se llena, se detiene la recepción sin confirmar
el mensaje afectado. El directorio privado y las escrituras atómicas pertenecen
al adaptador, no al núcleo de mensajería.

SQLite migra conservando mensajes a una clave (conversación, ID, dirección),
permite redelivery idempotente y propaga errores de almacenamiento. No se baja
un estado confirmado por eventos tardíos. Las confirmaciones de lectura propias
no se interpretan como lectura del destinatario y los recibos se atribuyen a su
conversación. Se amplía la cola acotada de respuestas para tolerar ráfagas.

La reconexión limita los fallos a tres intentos (2/4/8 segundos), también si el
fallo sucede durante el handshake; solo una conexión sana de 30 segundos repone
el presupuesto. Desconectar manualmente cancela los reintentos pendientes.
No se reenvían automáticamente mensajes de resultado incierto.

Se han escrito pruebas de recuperación, fallo de almacenamiento, bloqueo del
commit Signal al fallar el guardado, duplicados, recibos y reconexión. Esas
pruebas C/Kotlin no se han compilado ni ejecutado por instrucción del usuario.
Los cambios no incluyen recuperación del historial previo a la vinculación ni
reintentos automáticos de descifrado con solicitud de nuevas claves.


## Versión solicitada 0.8

Incluye el modo oscuro, la corrección de recepción de paquetes grandes y los
cambios de estabilidad y persistencia. Las nueve pruebas C99 y la comprobación
de migración SQLite pasan localmente; la compilación Android, pruebas JVM y
emulador se ejecutan para esta entrega solicitada por el usuario.
La rama principal conserva el workflow exclusivamente manual. Si el conector
no expone workflow_dispatch, se construye desde una rama temporal aislada,
creada expresamente para esta solicitud; no se activan builds por guardar
cambios en main. Se desactiva el disparador de esa rama al acabar.
