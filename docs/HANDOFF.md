# Continuidad del proyecto — 6 de noviembre de 2026

## Actualización 0.37-debug: fijados y orden

- SQLite v8: pinned y pinned_changed; migración conservando historial y fusión PN/LID.
- Historial Conversation.pinned (24) y app-state regular_low pin_v1 / SyncActionValue.pinAction (5), apiVersion 5.
- Selección de chats: Fijar/Desfijar con cambio local inmediato y cola remota existente, incluso sin mensaje ancla.
- Fijados por timestamp descendente; resto por actividad descendente, desempate por JID. Se conserva conversationTimestamp/lastMsgTimestamp importado.
- Pruebas locales: pin/unpin autenticado y descifrado; migración v7-v8, orden, desfijado y protección frente a historial tardío; suite SQLite existente aprobada.
- Compilación local ARM32 aprobada: assembleDebug, testDebugUnitTest, lintDebug y suite completa C99. APK de 3.238.809 bytes sin firmar; no instalable como actualización. El usuario autorizó explícitamente publicar en main y ejecutar GitHub Actions para generar la APK firmada; se inicia la entrega remota. Se corrigieron también las rutas 0.37 del workflow. No se ejecutó emulador.
- La cola saliente sigue sin persistencia ni reintento tras fallo. Validación en cuenta real pendiente. Entrega ARM32 exclusivamente.

## Actualización anterior: 0.36-debug (6 de noviembre)

Esta sección sustituye el estado 0.35 descrito más abajo. El usuario sigue probando con un Samsung M13; su Android admite aplicaciones armeabi-v7a (32 bits). Se entregó exclusivamente ARM32.

- Commit funcional: 2e7f35cb1bff58315f1df0a489327aaf84a9b935.
- CI completo aprobado (C99, SQLite, Android, lint, unidad y emulador/instrumentación): https://github.com/reeagbo/whatsapp-legacy/actions/runs/42211043578
- Descarga ZIP: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42211043578/artifacts/13499841014
- APK: whatsapp-legacy-0.36-debug-armeabi-v7a.apk. Instalar como actualización.
- Revisión solicitada por inconsistencias con el principal: docs/consistency-review-0.36.md contiene hallazgos, correcciones y límites pendientes.
- Corregidos: resumen no revierte estado de salida; notificaciones invalidan miembros y solicitan metadatos actuales; respuestas anteriores a una notificación se descartan; diálogo de información se actualiza; archivo de historial importado sin sobrescribir decisiones con timestamp.
- Confirmados pendientes: cola de archivo/borrado no persistente y sin reintento; operaciones remotas de grupos vacíos carecen de ancla; metadatos y envío limitados a 64 miembros (96 dispositivos para envío); no se replican leído/no leído, fijado, silenciado ni preferencia de desarchivo automático. Ver informe para riesgos adicionales.
- No hay validación nueva en cuenta real: sigue pendiente probar textos de grupo en ambos sentidos y las correcciones en el teléfono.

Este documento resume el cierre de la conversación de desarrollo hasta la versión **0.35-debug**. Leer primero este archivo y AGENTS.md; después contrastar con el HEAD actual. El README y core/README.md contienen etapas antiguas y no son una descripción fiable del estado actual.

## Objetivo y forma de trabajar

- Repositorio: https://github.com/reeagbo/whatsapp-legacy
- Cliente WhatsApp mínimo para móviles antiguos: texto, conversaciones individuales y grupos. Multimedia fuera del alcance.
- Núcleo de protocolo y Signal en C99 portable; interfaz Android nativa en Kotlin; JNI como adaptador. Mantener esa separación.
- Hoy se vincula como dispositivo de una cuenta existente mediante código, sin QR. El registro de un número nuevo/SMS queda para investigación futura.
- Retrocompatibilidad máxima a largo plazo. Mínimo Android actual: API 21 / Android 5.0.
- Respuestas breves en español y trabajo autónomo; preguntar solo decisiones estratégicas.
- En esta fase está autorizado compilar y entregar una versión después de cambios funcionales, sin que el usuario lo pida. GitHub Actions se ejecuta en pushes a main.
- Incluir versión y arquitectura en nombre de APK. Entregar enlaces directos a GitHub.
- Documentación sola: commit con [skip ci], sin nueva versión ni APK.
- Borrado local inmediato, sin confirmación ni aviso de éxito; intentar sincronización remota sin revertir la operación local.

## Arquitectura del teléfono: decisión importante

El teléfono informa en APP_OPEN: **sdk=34 abis=armeabi-v7a,armeabi**.
Admite aplicaciones ARM de **32 bits**. No inferir el ABI por el procesador o el modelo.

- La APK universal 0.30 incluía ARM32, ARM64 y x86_64, y pesaba 7,19 MB.
- La 0.31 solo ARM64 NO era compatible. Fue un error de elección de arquitectura.
- Desde 0.32 se entrega únicamente **armeabi-v7a**.
- x86_64 solo se compila para el emulador de CI y no se publica en la descarga del usuario.
- No generar otras entregas hasta que se prueben otras arquitecturas.
- La 0.35 pesa 3.241.872 bytes (3,24 MB).

## Versión actual y descarga

**0.35-debug-armeabi-v7a**
- Commit funcional: 6015d3fdcb8e4617d021ad94437d12d7ead733b9
- CI: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42204998082
- Descarga: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42204998082/artifacts/13496687313
- Dentro del ZIP: whatsapp-legacy-0.35-debug-armeabi-v7a.apk
- Instalar como actualización, sin desinstalar, para conservar sesión e historial.
- La clave de firma de desarrollo se conserva en la caché de Actions.
- Todas las comprobaciones del workflow pasaron, incluida la regresión de creación de grupo en Android.
- El usuario TODAVÍA no ha confirmado el comportamiento de la 0.35 en el teléfono.

## Funciones y validación

### Conversaciones individuales
Envío y recepción de texto, Unicode/emojis, estados de mensaje, nombres locales/agenda/recibidos, resolución PN/LID, SQLite, inbox nativo duradero, reconexión automática y carga de mensajes anteriores.

Archivar, desarchivar y borrar en ambas direcciones fueron confirmados por el usuario en **0.29** después de una larga depuración. No volver a tratarlos como pendientes de implementación básica.
La recuperación completa de todas las conversaciones antiguas/inactivas no está garantizada: depende del móvil principal y de lo que entregue.

### Selección múltiple (0.30)
Pulsación larga inicia selección; toques añaden/quitan filas; seleccionar/deseleccionar todas; Atrás cancela.
Archivar, desarchivar y eliminar en lote, también desde Archivados.
El lote se guarda localmente en una transacción y los cambios remotos se encolan uno por uno.

El usuario observó lentitud entre acciones. Cada operación reconstruye snapshot/patches autenticados antes de escribir; hay timeout de 90 s, pero no una pausa fija intencionada entre elementos. Se propuso reutilizar estado actualizado, pero **el usuario lo dejó aparcado**. No trabajar en esa optimización ahora.

### Agenda (0.33)
Nueva conversación conserva número y nombre manual y añade **Buscar en la agenda**.
Búsqueda local por nombre/número con normalización E.164 y hasta 100 resultados. Permiso Android automático si falta.
El selector se reutiliza al crear grupos. No se sube la agenda completa.
La interfaz se probó automáticamente; no hay confirmación específica del usuario sobre todos los casos de selección real.

### Grupos (primera implementación: 0.33)
Menú → Nuevo grupo. Nombre y números internacionales o selector de agenda.
Incluye consultas de grupos participantes/metadatos, participantes y administradores, envío/recepción de texto mediante Signal Sender Keys, autor en burbujas, importación de historial de grupos, archivo y borrado de conversación mediante app-state, y salida del grupo aparte.

**Borrar conversación NO equivale a salir del grupo.** Salir está en Información del grupo y espera confirmación del servidor.
La información de grupos por sí sola no llena la lista con grupos vacíos; un grupo recién creado sí es visible deliberadamente.
Una confirmación entregado/leído en grupo indica al menos un participante; no todos.
Límites iniciales de envío: 64 miembros y 96 dispositivos. Comunidades, hosted devices y herramientas completas de administración no están implementados.
Se consulta membresía actual antes de enviar y se rota/distribuye sender key en cada envío. Esto prioriza seguridad ante cambios de miembros, con sobrecoste.
Añadir/quitar participantes, cambiar nombre remoto, invitaciones y administración avanzada quedan pendientes.
**No hay todavía validación del envío/recepción de grupo con una cuenta real.** Las pruebas criptográficas son sintéticas.

## Incidentes recientes y resultado

### 0.33: desconexión al cargar grupos
El decodificador admitía solo 256 nodos / 16 atributos. Respuestas grandes de grupos podían cerrar el canal Noise autenticado.
Se reprodujo con una respuesta de 320 participantes.

### 0.34: corrección de conexión
- Nodos en bloques de direcciones estables, asignados según necesidad; límites 8.192 nodos y 32 atributos.
- Se conservan autenticación, límites de frame/descompresión y rechazo de formatos inválidos.
- Inicio con resúmenes de grupos; miembros completos bajo demanda.
- Listas grandes se drenan por la cola acotada de eventos sin perder el resto.
- Crear/salir/consultar desde UI tiene prioridad sobre refresco automático; respuestas tardías no cancelan la nueva solicitud.
- Claves de creación aleatorias de 32 caracteres hexadecimales.
- Pruebas de 320 participantes sobre canal cifrado, frame posterior en la misma sesión, 100 grupos y prioridad de creación.

### Log real de 0.34, ya analizado
Archivo: whatsapp-legacy-0.34-debug-20261106-113633.txt
Library ID: libfile_a6bc9496dec48191928078da77cd3f00

El log contiene historial de versiones anteriores. A partir de APP_OPEN version=0.34:
- Un solo intento de conexión, autenticación correcta.
- Cero reconexiones, fallos de WebSocket o rechazos de decodificación.
- Resúmenes de grupos procesados correctamente.
- A las 10:35:50 UTC se envía create.
- A las 10:35:51 UTC llega IQ result con group, eventos group/group-members y CHAT_OPEN.
- Los pings siguen contestándose.
- No hay intento de enviar texto a un grupo en ese tramo.
Conclusión: conexión recuperada y **creación real de grupo aceptada**.

### 0.35: indicador “Creando grupo…” atascado
El usuario dijo que la app se quedaba creando. La creación ya había terminado, pero statusText seguía con ese texto.
Se separó creatingGroup del estado de conexión:
- Confirmación: limpia progreso y abre el chat.
- Error / timeout: limpia progreso.
- Desconexión / nuevo intento: limpia progreso.
- Eventos de diagnóstico GROUP_CREATION_UI_CONFIRMED y GROUP_CREATION_UI_FINISHED.
Prueba de instrumentación Android con resultados sintéticos confirma navegación y limpieza en éxito/error/timeout/desconexión.
La APK 0.35 se entregó. Se indicó al usuario usar el grupo existente, sin crearlo otra vez.

## Próximo paso concreto

1. Confirmar en el teléfono que la 0.35 elimina “Creando grupo…” y permite usar el grupo existente.
2. Probar texto app → grupo y otro dispositivo → app.
3. Analizar el log nuevo si falla; diferenciar versiones dentro del archivo acumulado.
4. Después validar archivo/borrado/salida en grupos y seguir con administración de miembros según prioridades del usuario.
No afirmar que envío de grupos funciona con WhatsApp real hasta comprobarlo.

## Pendientes aparcados

- Persistencia/reintentos de cambios salientes de archivo/borrado tras cerrar o reiniciar app; ver docs/pending.md.
- Optimización de acciones múltiples: el usuario la aparcó.
- Historial completo inicial y conversaciones antiguas sin actividad: limitaciones del principal.
- Registro de un número nuevo: investigación futura.
- Ampliar otras arquitecturas/SO cuando haya dispositivos de prueba.

## Referencias de código y documentación

- docs/technical-overview.md: documento en INGLÉS, por procesos y diálogos concretos de protocolo. El usuario rechazó una descripción genérica. Mantener nodos, Protobuf, mensajes, respuestas, funciones y límites.
- docs/groups.md: protocolos, arquitectura y límites de grupos; actualización de robustez 0.34.
- core/messaging.c + core/groups.inc.h: coordinador y grupos.
- core/signal_engine.c: adaptador libsignal-protocol-c, sesiones de usuario y Sender Keys.
- core/pairing.c: sesión Noise, nodos y puente al messenger.
- core/node.c/.h: decodificador y bloques de nodos.
- core/history.c: historial y app-state autenticado.
- core/inbox.c: formato 3 con autor, lee formatos 1/2.
- MainActivity.kt, ChatStore.kt, PhoneContacts.kt, bridge.c.
- SQLite versión 7: grupos y autores en tablas separadas; preserva filas de mensajes.
- scripts/ui-smoke.py y scripts/check-storage-sql.py.
- app/src/androidTest/.../GroupCreationInstrumentation.kt y scripts/check-group-creation-ui.py.
- .github/workflows/android.yml: pruebas, ARM32 entregable, x86_64 de pruebas separado.

## Entorno de trabajo anterior

Hay un árbol local de trabajo sin .git en /workspace/scratch/ffd007e45859/whatsapp-groups.
Puede no existir en otra conversación: **GitHub es la fuente de verdad**.
Las escrituras se hicieron con el conector GitHub mediante árbol/commit/update_ref atómicos.
Si se usa ese método, respetar expected_sha y no sobrescribir avances ajenos.
Se pudo ejecutar bash core/build.sh y python scripts/check-storage-sql.py localmente; Android se compiló/probó en GitHub Actions.
No se han guardado credenciales de cuenta en repositorio ni artefactos.
