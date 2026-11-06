# Continuidad del proyecto — 6 de noviembre de 2026

## Actualización 0.41-debug: interfaz What!

- Entrega completada: commit a05aa763b3e18bdc473c73c52e902f4ba5082d9e.
- CI completo: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42268462269
- Descarga: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42268462269/artifacts/13527738015
- APK whatsapp-legacy-0.41-debug-armeabi-v7a.apk: 3.254.520 bytes; firma/contenido y certificado idéntico a 0.40 comprobados, solo ARM32. Library: libfile_c590a290991c8191967b7b71f016a5f2.
- Aprobación del usuario: tono distinto para fijados; tres círculos en triángulo con vértice abajo para grupos; selección en barra superior de altura fija (opción a); título temporal What!; nuevo grupo junto a + y seleccionar todas en cabecera; menú anclado a los tres puntos. No modificar no leídos ni altura de vistas previas.
- Cabecera normal y contextual comparten dimensiones: mismo alto de controles/paddings y línea de estado. Las acciones de selección sustituyen la cabecera, sin añadir una fila que desplace la lista; cancelar está en esa misma línea. Se mantienen selección múltiple y posición del scroll.
- GlyphView dibuja símbolos con Canvas/rectas/círculos: sin fuentes de iconos, imágenes, dependencias de UI ni cambios del protocolo. Controles redondeados con zona 48x52 dp, descripciones accesibles y foco para teclado. Pinned usa wa_pinned, y selección conserva un tono más fuerte.
- Menú PopupWindow compacto, alineado al botón, cerrado al navegar/destruir actividad. Las actualizaciones de lista se difieren mientras está abierto y se aplican al cerrar, sin hacer desaparecer el menú; los refrescos conservan el scroll. Nuevo grupo y seleccionar todas salen del menú para ocupar la cabecera de la lista. Etiqueta del lanzador: What!; paquete, firma y archivos de APK mantienen su identidad.
- Pruebas de interfaz adaptadas: igualdad de posición de una fila antes/durante/después de selección, alineación vertical de controles, cancelar con cruz, nueva ubicación de seleccionar todas y nuevo grupo. Capturas de selección, menú anclado y grupos con/sin fijado.
- SQLite y CI completo aprobados: C99, Android, unidad, lint, emulador e instrumentación. La prueba de igualdad de posición de las filas al seleccionar/cancelar pasó; revisión visual de capturas de grupos/fijados, selección y menú aprobada. No hay cambios en estados leído/no leído, preview de mensajes ni en protocolo/sincronización.

## Actualización 0.40-debug: límite de fijados y diagnóstico de salida

- Entrega completada: commit funcional bd7c7e527bec7a112cbe58639ea0c291f7b46601.
- CI completo: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42261356030
- Descarga: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42261356030/artifacts/13525068353
- APK whatsapp-legacy-0.40-debug-armeabi-v7a.apk: 3.250.860 bytes, firma/contenido verificados, certificado idéntico a 0.39 y solo ARM32. Library: libfile_44901303471c8191b3f8900ba439e256.
- Log 0.39 real whatsapp-legacy-0.39-debug-20261106-181805.txt (libfile_c373eac136f481919e2cfaef9aa53fa7): recuperación completada en versión 187, 43 acciones pin y 48 archive confirmadas; lista principal de 24 a 4 grupos y 2 fijados. No confundir acciones pin con número de chats fijados: incluyen false.
- El usuario confirma progreso pero cambios de pin desde Legacy no siempre aparecen en el principal. Aclaró que el fallo se observa en el principal; no sabe aún si afecta también a desfijar.
- Cuatro operaciones salientes del log tienen CHAT_CHANGE_SYNC_RESULT state=1 (unpin, pin, unpin, pin); esto solo demuestra aceptación del servidor, no aplicación visual en el principal. La causa completa de la intermitencia no está confirmada.
- Corregido defecto independiente: Legacy permitía superar tres chats fijados, límite documentado por WhatsApp (https://faq.whatsapp.com/645907560577342/). El lote que excede el límite de la lista principal se rechaza antes de tocar SQLite o la cola. Fijados de grupos e individuales cuentan juntos; desfijar libera plaza; no se limita el estado remoto importado.
- Trazas PIN_LOCAL, PIN_SYNC_START y PIN_SYNC_APPLIED: referencia opaca del chat, valor deseado/recibido/efectivo y timestamp. PIN_LIMIT_BLOCKED registra intentos que exceden el límite. No se registran nombres, JID ni contenido.
- Regresión de instrumentación: tres fijados entre grupos y un individual; un cuarto se bloquea sin cambio local ni cola; tras liberar una plaza, la misma selección se puede fijar. SQLite local aprobado. CI completo firmado ARM32 aprobado, incluida la regresión de límite en instrumentación.
- Próxima validación necesaria en cuenta real: fijar/desfijar el mismo chat desde Legacy y comprobar el principal; el nuevo log permite correlacionar cada operación sin depender de deducciones por recuentos de grupos.

## Actualización 0.39-debug: recuperar fijados y archivo existentes

- El usuario confirma muchos más grupos visibles en 0.38, pero faltan estados de fijado/archivo.
- Log real whatsapp-legacy-0.38-debug-20261106-174831.txt (libfile_dae053c6e9c88191993e7df5eac81611): 24 resúmenes, lista de 24 grupos y 0 fijados; inicio de regular_low en versión 185, solo deltas hasta 187, ningún GROUP_SYNC_ROW pin/history-pin ni SYNC_CHAT_ARCHIVE. La lista no recibe el estado anterior completo.
- Recuperación única de regular_low cuando falta el marcador durable state-low-projection-v1: consulta versión cero con snapshot, reconstruye mapa autenticado con todos los deltas, incluidas versiones ya recibidas, y aplica los estados finales aunque el cursor ya esté en esa versión.
- No se rebaja el cursor ni se aplican páginas parciales. Una reconstrucción final anterior al cursor se rechaza y se vuelve a solicitar. Se conservan las protecciones de timestamp de SQLite frente a decisiones locales posteriores.
- Slot y base separados de las operaciones salientes: history-task-12 y rebuild-base-low; las operaciones del usuario conservan sus slots/bases. La recuperación es reanudable y el marcador se escribe después de confirmar SQLite. Las páginas se verifican con los mismos HMAC, claves e integridad del mapa que las escrituras existentes.
- Pruebas C99: instantánea vieja más deltas ya recibidos, pin de grupo y unarchive finales, manipulación rechazada, cursor más reciente rechazado, páginas sin estados intermedios, continuación, marcador solo tras commit y primera petición versión cero.
- Log mejorado: APP_STATE_REBUILD_BASE, regular_low-rebuilt y contadores pins/archives por chunk confirmado.
- Entrega completada: commit funcional af6ec0525b829911b58b5e454b6449370aa36b1e. CI completo aprobado (C99, SQLite, Android, unidad, lint, UI e instrumentación): https://github.com/reeagbo/whatsapp-legacy/actions/runs/42257225816
- APK firmada whatsapp-legacy-0.39-debug-armeabi-v7a.apk: 3.249.224 bytes; integridad, arquitectura ARM32 y certificado idéntico a 0.38 comprobados. Descarga: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42257225816/artifacts/13524285318
- Library de la APK: libfile_5acfc46f82588191b3fffe5af0082f13. Instalar como actualización y dejar conectar para reconstrucción automática. La corrección de estados reales debe confirmarse en el teléfono tras la entrega.

## Actualización 0.38-debug: visibilidad y sincronización de grupos

- El usuario confirmó que los fijados individuales sí suben y se marcan correctamente. El fallo pendiente estaba en los grupos; no cambiar la ordenación de individuales.
- Reproducido: chats de grupo con actividad y fijado guardados pero sin mensajes no aparecían en queryChats porque group_metadata.visible permanecía a cero.
- Los resúmenes participating, las conversaciones de historial y los fijados de grupo hacen visible el chat sin exigir mensajes de texto importados. No se crean mensajes ficticios ni se infiere la salida del grupo por un resumen sin participantes.
- SQLite v9 repara al actualizar los grupos ocultos ya guardados en v8. Respeta archivo y marcas de borrado: un resumen o historial anterior no resucita una conversación borrada; una actividad o fijado posterior sí puede hacerlo.
- Trazas GROUP_SYNC_ROW (fuente y referencia opaca del grupo) y GROUP_SYNC_LIST (grupos visibles y fijados), sin nombres ni texto. Permiten distinguir llegada/importación de la lista final si aún faltan grupos reales.
- Regresiones SQLite: migración con rollback, grupo sin texto, estado fijado, archivo, borrado y datos tardíos. Regresión Android ampliada: eventos summary sintéticos hacia UI/almacenamiento, pin/unpin, borrado y reapertura de BD v8.
- Entrega firmada ARM32 completada: commit funcional 2f369906e9edab626c281329457979584b8ced84.
- CI completo aprobado (C99, SQLite, Android, unidad, lint, UI y migración v8-v9 instrumentada): https://github.com/reeagbo/whatsapp-legacy/actions/runs/42230019696
- Descarga: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42230019696/artifacts/13510205687
- APK whatsapp-legacy-0.38-debug-armeabi-v7a.apk, 3.247.666 bytes. Verificados firma/contenido, certificado idéntico a 0.37 y arquitectura ARM32 única. Instalar como actualización.
- Library de la APK firmada: libfile_6ab1e65055348191a9ba7a494881eee0.
- Todavía hace falta validar los grupos de la cuenta real en el teléfono; no afirmar que todos llegan ni que el envío real de grupos está validado.

## Actualización 0.37-debug: fijados y orden

- Entrega firmada completada: commit funcional 2e2c9bbbde5c2833b586a48aa8c29244490e8ab4.
- CI completo aprobado (C99, SQLite, Android, unidad, lint, UI e instrumentación): https://github.com/reeagbo/whatsapp-legacy/actions/runs/42225141688
- Descarga: https://github.com/reeagbo/whatsapp-legacy/actions/runs/42225141688/artifacts/13505118763
- APK whatsapp-legacy-0.37-debug-armeabi-v7a.apk: 3.246.151 bytes; verificados contenido firmado, arquitectura ARM32 única y certificado idéntico a 0.36. Instalar como actualización.
- SQLite v8: pinned y pinned_changed; migración conservando historial y fusión PN/LID.
- Historial Conversation.pinned (24) y app-state regular_low pin_v1 / SyncActionValue.pinAction (5), apiVersion 5.
- Selección de chats: Fijar/Desfijar con cambio local inmediato y cola remota existente, incluso sin mensaje ancla.
- Fijados por timestamp descendente; resto por actividad descendente, desempate por JID. Se conserva conversationTimestamp/lastMsgTimestamp importado.
- Pruebas locales: pin/unpin autenticado y descifrado; migración v7-v8, orden, desfijado y protección frente a historial tardío; suite SQLite existente aprobada.
- Compilación local ARM32 aprobada: assembleDebug, testDebugUnitTest, lintDebug y suite completa C99. APK de 3.238.809 bytes sin firmar; no instalable como actualización. El usuario autorizó explícitamente publicar en main y ejecutar GitHub Actions para generar la APK firmada; entrega remota completada y verificada. Se corrigieron también las rutas 0.37 del workflow. El emulador se validó posteriormente en el CI completo de esta entrega.
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
