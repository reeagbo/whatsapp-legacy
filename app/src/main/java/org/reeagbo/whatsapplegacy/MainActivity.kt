package org.reeagbo.whatsapplegacy

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Build
import android.widget.Toast
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import okio.ByteString.Companion.toByteString
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

class MainActivity : Activity() {
    private external fun nativeLog(handle: Long): String
    private external fun nativeDiagnostics(): String
    private external fun nativeCreate(path: String, phone: String): Long
    private external fun nativeFree(handle: Long)
    private external fun nativeStart(handle: Long)
    private external fun nativeFeed(handle: Long, bytes: ByteArray)
    private external fun nativeOutput(handle: Long): ByteArray?
    private external fun nativeState(handle: Long): Int
    private external fun nativeStatus(handle: Long): String
    private external fun nativeCode(handle: Long): String
    private external fun nativePing(handle: Long)

    private val handler = Handler(Looper.getMainLooper())
    private val timer = Executors.newSingleThreadScheduledExecutor()
    private val http = OkHttpClient.Builder()
        .connectTimeout(20, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.SECONDS)
        .writeTimeout(20, TimeUnit.SECONDS)
        .pingInterval(0, TimeUnit.SECONDS) // Use the encrypted WA IQ heartbeat only.
        .build()
    private lateinit var output: TextView
    private lateinit var code: TextView
    private lateinit var number: EditText
    private lateinit var pair: Button
    private lateinit var check: Button
    private lateinit var stop: Button
    private var connection: PairConnection? = null
    private var destroyed = false
    private lateinit var debugLog: DebugLog
    private var attempts = 0
    private val exportRequest = 301
    private var oldCrashHandler: Thread.UncaughtExceptionHandler? = null
    private var crashHandler: Thread.UncaughtExceptionHandler? = null
    private val appVersion: String by lazy { packageManager.getPackageInfo(packageName, 0).versionName ?: "unknown" }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        debugLog = DebugLog(File(filesDir, "diagnostics/latest.txt"))
        debugLog.add("APP_OPEN version=$appVersion sdk=${Build.VERSION.SDK_INT} abis=${Build.SUPPORTED_ABIS.joinToString(",")}")
        oldCrashHandler = Thread.getDefaultUncaughtExceptionHandler()
        crashHandler = Thread.UncaughtExceptionHandler { thread, error ->
            debugLog.failure("UNCAUGHT", error)
            oldCrashHandler?.uncaughtException(thread, error)
        }
        Thread.setDefaultUncaughtExceptionHandler(crashHandler)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val pad = (20 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
        }
        layout.addView(TextView(this).apply { text = "WhatsApp Legacy $appVersion"; textSize = 26f })
        layout.addView(TextView(this).apply {
            text = "Vinculación experimental mediante código. Necesitas WhatsApp principal activo. Esta versión todavía no envía ni recibe mensajes."
            textSize = 16f
        })
        number = EditText(this).apply {
            hint = "Número internacional sin + (ej.: 34…)"
            inputType = InputType.TYPE_CLASS_PHONE
            setSingleLine(true)
            // Avoid putting the phone number into saved view state or backups.
            isSaveEnabled = false
        }
        pair = Button(this).apply { text = "Solicitar código de vinculación" }
        check = Button(this).apply { text = "Comprobar sesión guardada" }
        stop = Button(this).apply { text = "Cancelar"; isEnabled = false }
        code = TextView(this).apply { textSize = 34f; setTextIsSelectable(true) }
        output = TextView(this).apply { textSize = 16f; setTextIsSelectable(true) }
        val diagnostic = Button(this).apply { text = "Pruebas locales C99" }
        val saveLog = Button(this).apply { text = "Guardar log" }
        val viewLog = Button(this).apply { text = "Ver log" }
        val copyLog = Button(this).apply { text = "Copiar log" }
        listOf(number, pair, check, stop, code, output, diagnostic, saveLog, viewLog, copyLog).forEach { layout.addView(it) }
        saveLog.setOnClickListener { exportLog() }
        copyLog.setOnClickListener {
            (getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager)
                .setPrimaryClip(ClipData.newPlainText("WhatsApp Legacy log", debugLog.snapshot()))
            Toast.makeText(this, "Log copiado", Toast.LENGTH_SHORT).show()
        }
        viewLog.setOnClickListener {
            val text = TextView(this).apply { this.text = debugLog.snapshot(); setTextIsSelectable(true); setPadding(16, 16, 16, 16); textSize = 12f }
            AlertDialog.Builder(this).setTitle("Registro de diagnóstico")
                .setView(ScrollView(this).apply { addView(text) }).setPositiveButton("Cerrar", null).show()
        }
        setContentView(ScrollView(this).apply { addView(layout) })
        pair.setOnClickListener {
            val phone = number.text.toString().trim()
            if (!phone.matches(Regex("^[1-9][0-9]{7,14}$"))) {
                number.error = "Usa entre 8 y 15 dígitos, con prefijo de país y sin +"
            } else start(phone)
        }
        check.setOnClickListener { start("") }
        stop.setOnClickListener {
            connection?.cancel()
            connection = null
            render("Conexión cancelada. Las claves guardadas se conservan.", "", false)
        }
        diagnostic.setOnClickListener { if (connection == null) output.text = nativeDiagnostics() }
        render("Listo. Las claves se guardan en el espacio privado de esta aplicación.", "", false)
    }

    private fun render(status: String, pairingCode: String, busy: Boolean) {
        if (destroyed) return
        output.text = status
        code.text = if (pairingCode.length == 8) "${pairingCode.take(4)}-${pairingCode.takeLast(4)}" else ""
        pair.isEnabled = !busy
        check.isEnabled = !busy
        stop.isEnabled = busy
        number.isEnabled = !busy
    }

    private fun start(phone: String) {
        connection?.cancel()
        debugLog.hide(phone)
        val attempt = ++attempts
        debugLog.add("ATTEMPT $attempt BEGIN mode=${if (phone.isEmpty()) "check-session" else "pair-code"}")
        val next = PairConnection(phone, attempt)
        connection = next
        render("Preparando claves y conexión segura…", "", true)
        next.connect()
    }

    private inner class PairConnection(private val phone: String, private val attempt: Int) : WebSocketListener() {
        private val lock = Any()
        private var handle = 0L
        private var ws: WebSocket? = null
        private var finished = false
        private var lastState: Int? = null
        private fun log(event: String) { debugLog.add("A$attempt $event") }
        private fun drainNative() {
            if (handle != 0L) nativeLog(handle).lineSequence().filter { it.isNotBlank() }.forEach { log(it) }
        }
        private var heartbeat: ScheduledFuture<*>? = null
        private var deadline: ScheduledFuture<*>? = null

        fun connect() = synchronized(lock) {
            log("NATIVE_CREATE_BEGIN")
            handle = nativeCreate(File(filesDir, "identity.v1").absolutePath, phone)
            drainNative()
            log("NATIVE_CREATE_END allocated=${handle != 0L}")
            if (handle == 0L) { finish("No se pudo preparar la sesión."); return@synchronized }
            if (nativeState(handle) < 0) { finish(nativeStatus(handle)); return@synchronized }
            log("WEBSOCKET_CONNECT_REQUEST")
            val tracedHttp = try { NetworkTrace.client(http) { event -> log(event) } }
                catch (error: Exception) { debugLog.failure("NETWORK_SETUP_FAILED", error); finish("No se pudo preparar TLS."); return@synchronized }
            ws = tracedHttp.newWebSocket(
                Request.Builder().url("wss://web.whatsapp.com/ws/chat")
                    .header("Origin", "https://web.whatsapp.com")
                    .header("User-Agent", "WhatsAppLegacy/$appVersion (experimental)")
                    .build(), this
            )
            deadline = timer.schedule({ synchronized(lock) {
                if (!finished) { log("DEADLINE_EXPIRED"); finish("Tiempo de vinculación agotado. Solicita otro código. Las claves se conservan.") }
            } }, 180, TimeUnit.SECONDS)
        }

        override fun onOpen(webSocket: WebSocket, response: Response) = synchronized(lock) {
            if (finished) { webSocket.cancel(); return@synchronized }
            log("WEBSOCKET_OPEN http=${response.code} tls=${response.handshake?.tlsVersion?.javaName}")
            nativeStart(handle)
            drainNative()
            flush(webSocket)
            publish()
            if (!finished) heartbeat = timer.scheduleAtFixedRate({ synchronized(lock) {
                if (!finished && handle != 0L) { nativePing(handle); flush(webSocket); publish() }
            } }, 15, 15, TimeUnit.SECONDS)
        }

        override fun onMessage(webSocket: WebSocket, bytes: ByteString) = synchronized(lock) {
            if (finished) return@synchronized
            if (bytes.size > 2 * 1024 * 1024) { finish("Respuesta demasiado grande."); return@synchronized }
            log("WEBSOCKET_RX binary_bytes=${bytes.size}")
            nativeFeed(handle, bytes.toByteArray())
            drainNative()
            flush(webSocket)
            publish()
        }

        override fun onMessage(webSocket: WebSocket, text: String) = synchronized(lock) {
            log("WEBSOCKET_RX unexpected_text_chars=${text.length}")
            if (!finished) finish("El servidor ha enviado un formato inesperado.")
        }

        private fun flush(webSocket: WebSocket) {
            if (finished || handle == 0L) return
            while (true) {
                val data = nativeOutput(handle) ?: break
                log("WEBSOCKET_TX binary_bytes=${data.size}")
                if (!webSocket.send(data.toByteString())) { finish("No se pudo enviar el paquete cifrado."); break }
            }
        }

        private fun publish() {
            if (finished || handle == 0L) return
            val state = nativeState(handle)
            val status = nativeStatus(handle)
            val pairingCode = nativeCode(handle)
            if (pairingCode.length == 8) {
                debugLog.hide(pairingCode)
                debugLog.hide("${pairingCode.take(4)}-${pairingCode.takeLast(4)}")
            }
            drainNative()
            if (state != lastState) { log("STATE_CHANGE $lastState->$state"); lastState = state }
            if (state < 0 || state >= 4) { finish(status); return }
            handler.post { if (connection === this && !finished) render(status, pairingCode, true) }
        }

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) = synchronized(lock) {
            log("WEBSOCKET_CLOSING close_code=$code")
            webSocket.close(code, null)
            if (!finished) finish("WhatsApp ha cerrado la conexión antes de confirmar la vinculación.")
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) = synchronized(lock) {
            log("WEBSOCKET_FAILURE http=${response?.code ?: "none"}")
            debugLog.failure("A$attempt NETWORK", t)
            if (!finished) finish("Error de conexión: ${t.javaClass.simpleName}. Comprueba Internet y la fecha del móvil.")
        }

        private fun finish(status: String) {
            if (finished) return
            drainNative()
            log("ATTEMPT_END state=${if (handle != 0L) nativeState(handle) else -1}")
            finished = true
            heartbeat?.cancel(false)
            deadline?.cancel(false)
            ws?.close(1000, "Finished")
            if (handle != 0L) { nativeFree(handle); handle = 0L }
            handler.post { if (connection === this) { connection = null; render(status, "", false) } }
        }

        fun cancel() = synchronized(lock) {
            if (!finished) {
                drainNative()
                log("CANCEL state=${if (handle != 0L) nativeState(handle) else -1}")
                finished = true
                heartbeat?.cancel(false)
                deadline?.cancel(false)
                ws?.cancel()
                if (handle != 0L) { nativeFree(handle); handle = 0L }
            }
        }
    }

    private fun exportLog() {
        debugLog.add("LOG_EXPORT_REQUEST")
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date())
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "text/plain"
            putExtra(Intent.EXTRA_TITLE, "whatsapp-legacy-$appVersion-$stamp.txt")
        }
        try { startActivityForResult(intent, exportRequest) }
        catch (error: Exception) { debugLog.failure("EXPORT_PICKER_FAILED", error); Toast.makeText(this, "Usa Copiar log para exportarlo", Toast.LENGTH_LONG).show() }
    }

    @Deprecated("Legacy Activity result API supports Android 5.0")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == exportRequest && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            try {
                val stream = contentResolver.openOutputStream(uri, "wt") ?: throw java.io.IOException("No output")
                stream.use { it.write(debugLog.snapshot().toByteArray(Charsets.UTF_8)) }
                Toast.makeText(this, "Log guardado; puedes adjuntarlo en el chat", Toast.LENGTH_LONG).show()
            } catch (error: Exception) {
                debugLog.failure("LOG_EXPORT_FAILED", error)
                Toast.makeText(this, "No se pudo guardar; usa Copiar log", Toast.LENGTH_LONG).show()
            }
        }
    }

    override fun onDestroy() {
        debugLog.add("APP_DESTROY changing_configuration=$isChangingConfigurations")
        if (Thread.getDefaultUncaughtExceptionHandler() === crashHandler) Thread.setDefaultUncaughtExceptionHandler(oldCrashHandler)
        destroyed = true
        connection?.cancel()
        connection = null
        timer.shutdownNow()
        http.dispatcher.executorService.shutdown()
        http.connectionPool.evictAll()
        handler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }
    companion object { init { System.loadLibrary("wa_legacy") } }
}
