# WhatsApp Legacy — Android + C99

Proyecto experimental propio, sin Node.js, Termux ni navegador.

## Estado comprobable

Se ha recuperado íntegramente el núcleo de `wa_core_phase12.zip` en `core/`.
La interfaz Android y el puente JNI de este repositorio se han reconstruido porque
los archivos Android mencionados en la conversación anterior no estaban guardados.
No se ha recuperado una implementación de `pair-success` ni `pair-device-sign`.

**Esta versión no vincula una cuenta WhatsApp.** El núcleo recuperado construye
ClientPayload y tiene pruebas locales de WebSocket, Noise y ClientFinish. Faltan
claves Signal persistentes, completar el intercambio con el servidor y el protocolo
de vinculación por código. La app muestra diagnósticos reales del núcleo mediante
JNI, utilizando datos sintéticos que nunca envía. No genera códigos falsos.

## Compilación con GitHub Actions

Subir todo el contenido de este directorio, incluida `.github/`, a la raíz del
repositorio. Cada push ejecuta `Android APK and C99 tests`. Primero ejecuta las
pruebas del núcleo y después compila una APK debug firmada para instalar.
Descargar el artefacto `whatsapp-legacy-debug` de la ejecución completada.

El workflow compila y publica el artefacto de APK en GitHub Actions. Consultar
el resultado de la ejecución antes de considerar validada la compilación Android.

Android 5.0 o posterior (API 21), ARMv7, ARM64 y x86_64. API 21 es una decisión
técnica provisional de esta reconstrucción, no un requisito histórico recuperado.
AGP 8.7.3, Gradle 8.9, Kotlin 2.0.21, JDK 17, NDK 27.2.12479018.

## Compilación local

Para C99: `bash core/build.sh` (CMake, compilador C99 y cabeceras OpenSSL).
Para Android: instalar los SDK/NDK fijados en el workflow, definir
`ANDROID_NDK_HOME`, ejecutar `bash scripts/build-native-deps.sh` y después
`gradle :app:assembleDebug`. La instalación local requiere Gradle 8.9.
OpenSSL 3.0.18 se obtiene desde su repositorio oficial durante la compilación.
La dependencia Signal viene en el archivo original de la fase 12.

## Licencias

El paquete original de libsignal-protocol-c 2.3.3 incluye su licencia GPL-3.0.
Consultar `core/README.md` y las licencias dentro de cada dependencia. Este proyecto
no está asociado con WhatsApp o Meta. No se incluyen claves ni cuentas de usuario.
