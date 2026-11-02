package org.reeagbo.whatsapplegacy

import android.app.Activity
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
        .pingInterval(20, TimeUnit.SECONDS)
        .build()
    private lateinit var output: TextView
    private lateinit var code: TextView
    private lateinit var number: EditText
    private lateinit var pair: Button
    private lateinit var check: Button
    private lateinit var stop: Button
    private var connection: PairConnection? = null
    private var destroyed = false

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val pad = (20 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
        }
        layout.addView(TextView(this).apply { text = "WhatsApp Legacy"; textSize = 26f })
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
        listOf(number, pair, check, stop, code, output, diagnostic).forEach { layout.addView(it) }
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
        val next = PairConnection(phone)
        connection = next
        render("Preparando claves y conexión segura…", "", true)
        next.connect()
    }

    private inner class PairConnection(private val phone: String) : WebSocketListener() {
        private val lock = Any()
        private var handle = 0L
        private var ws: WebSocket? = null
        private var finished = false
        private var heartbeat: ScheduledFuture<*>? = null
        private var deadline: ScheduledFuture<*>? = null

        fun connect() = synchronized(lock) {
            handle = nativeCreate(File(filesDir, "identity.v1").absolutePath, phone)
            if (handle == 0L) { finish("No se pudo preparar la sesión."); return@synchronized }
            if (nativeState(handle) < 0) { finish(nativeStatus(handle)); return@synchronized }
            ws = http.newWebSocket(
                Request.Builder().url("wss://web.whatsapp.com/ws/chat")
                    .header("Origin", "https://web.whatsapp.com")
                    .header("User-Agent", "WhatsAppLegacy/0.2 (experimental)")
                    .build(), this
            )
            deadline = timer.schedule({ synchronized(lock) {
                if (!finished) finish("Tiempo de vinculación agotado. Solicita otro código. Las claves se conservan.")
            } }, 180, TimeUnit.SECONDS)
        }

        override fun onOpen(webSocket: WebSocket, response: Response) = synchronized(lock) {
            if (finished) { webSocket.cancel(); return@synchronized }
            nativeStart(handle)
            flush(webSocket)
            publish()
            heartbeat = timer.scheduleAtFixedRate({ synchronized(lock) {
                if (!finished && handle != 0L) { nativePing(handle); flush(webSocket); publish() }
            } }, 15, 15, TimeUnit.SECONDS)
        }

        override fun onMessage(webSocket: WebSocket, bytes: ByteString) = synchronized(lock) {
            if (finished) return@synchronized
            if (bytes.size > 2 * 1024 * 1024) { finish("Respuesta demasiado grande."); return@synchronized }
            nativeFeed(handle, bytes.toByteArray())
            flush(webSocket)
            publish()
        }

        override fun onMessage(webSocket: WebSocket, text: String) = synchronized(lock) {
            if (!finished) finish("El servidor ha enviado un formato inesperado.")
        }

        private fun flush(webSocket: WebSocket) {
            if (finished || handle == 0L) return
            while (true) {
                val data = nativeOutput(handle) ?: break
                if (!webSocket.send(data.toByteString())) { finish("No se pudo enviar el paquete cifrado."); break }
            }
        }

        private fun publish() {
            if (finished || handle == 0L) return
            val state = nativeState(handle)
            val status = nativeStatus(handle)
            val pairingCode = nativeCode(handle)
            if (state < 0 || state >= 4) { finish(status); return }
            handler.post { if (connection === this && !finished) render(status, pairingCode, true) }
        }

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) = synchronized(lock) {
            webSocket.close(code, null)
            if (!finished) finish("WhatsApp ha cerrado la conexión antes de confirmar la vinculación.")
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) = synchronized(lock) {
            if (!finished) finish("Error de conexión: ${t.javaClass.simpleName}. Comprueba Internet y la fecha del móvil.")
        }

        private fun finish(status: String) {
            if (finished) return
            finished = true
            heartbeat?.cancel(false)
            deadline?.cancel(false)
            ws?.close(1000, "Finished")
            if (handle != 0L) { nativeFree(handle); handle = 0L }
            handler.post { if (connection === this) { connection = null; render(status, "", false) } }
        }

        fun cancel() = synchronized(lock) {
            if (!finished) {
                finished = true
                heartbeat?.cancel(false)
                deadline?.cancel(false)
                ws?.cancel()
                if (handle != 0L) { nativeFree(handle); handle = 0L }
            }
        }
    }

    override fun onDestroy() {
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
