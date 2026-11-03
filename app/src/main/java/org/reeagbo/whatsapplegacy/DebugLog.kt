package org.reeagbo.whatsapplegacy

import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/** Bounded, synchronous diagnostics. Never accepts packet bodies or credentials. */
class DebugLog(private val file: File) {
    private val limit = 256 * 1024
    private val started = System.nanoTime()
    private val secrets = linkedSetOf<String>()
    private val stamp = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.ROOT).apply {
        timeZone = TimeZone.getTimeZone("UTC")
    }
    private val text = StringBuilder(runCatching {
        if (file.isFile && file.length() <= limit) file.readText(Charsets.UTF_8) else ""
    }.getOrDefault(""))
    private var diskError = false

    @Synchronized fun hide(value: String) {
        if (value.length >= 4) secrets.add(value)
    }

    @Synchronized fun add(event: String) {
        val safe = redact(event)
        val ms = (System.nanoTime() - started) / 1_000_000
        val line = "${stamp.format(Date())} +${ms}ms $safe\n"
        text.append(line)
        val trim = text.length > limit
        if (trim) {
            val cut = text.indexOf("\n", text.length - limit / 2)
            text.delete(0, if (cut >= 0) cut + 1 else text.length - limit / 2)
            text.insert(0, "[older log entries removed]\n")
        }
        runCatching {
            file.parentFile?.mkdirs()
            FileOutputStream(file, !trim).use { out ->
                out.write((if (trim) text.toString() else line).toByteArray(Charsets.UTF_8))
                out.fd.sync() // Retain the last stage even if the native process crashes.
            }
        }.onFailure { diskError = true }
    }

    @Synchronized fun failure(label: String, error: Throwable) {
        add("$label exception_chain=${generateSequence(error) { it.cause }.take(6).joinToString(" > ") { it.javaClass.simpleName }}")
        // Exception messages and full stack dumps may contain URLs, headers or identifiers.
        error.stackTrace.take(10).forEach { frame ->
            add("STACK ${frame.className}.${frame.methodName}:${frame.lineNumber}")
        }
    }

    @Synchronized fun snapshot(): String =
        "WhatsApp Legacy diagnostic log; timestamps UTC. Payloads and credentials excluded.\n" +
            (if (diskError) "NOTE: disk write failed; this export includes in-memory entries.\n" else "") + text

    private fun redact(raw: String): String {
        var value = raw.take(4096).replace(Regex("[\\p{Cntrl}]"), " ")
        secrets.sortedByDescending { it.length }.forEach { value = value.replace(it, "[redacted]") }
        value = value.replace(Regex("[A-Za-z0-9_.:+-]+@(?:s\\.whatsapp\\.net|lid|hosted(?:\\.lid)?)"), "[jid]")
        value = value.replace(Regex("(?<![A-Za-z0-9])\\+?[0-9]{7,}(?![A-Za-z0-9])"), "[number]")
        value = value.replace(Regex("(?<![A-Za-z0-9])[A-Za-z0-9+/]{32,}={0,2}(?![A-Za-z0-9])"), "[opaque-value]")
        return value
    }
}
