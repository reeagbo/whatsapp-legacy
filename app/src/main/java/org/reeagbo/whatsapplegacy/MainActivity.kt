package org.reeagbo.whatsapplegacy

import android.app.Activity
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.util.concurrent.Executors

class MainActivity : Activity() {
    private val worker = Executors.newSingleThreadExecutor()
    private external fun nativeDiagnostics(): String
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 32, 24, 24)
        }
        layout.addView(TextView(this).apply { text = "WhatsApp Legacy"; textSize = 26f })
        layout.addView(TextView(this).apply {
            text = "Cliente experimental Android + C99. Esta versión permite comprobar el núcleo nativo. La vinculación real todavía no está implementada."
            textSize = 17f
        })
        val output = TextView(this).apply { textSize = 15f; setTextIsSelectable(true) }
        val button = Button(this).apply { text = "Comprobar núcleo C99" }
        layout.addView(button)
        layout.addView(output)
        setContentView(ScrollView(this).apply { addView(layout) })
        button.setOnClickListener {
            button.isEnabled = false
            output.text = "Comprobando…"
            worker.execute {
                val result = try { nativeDiagnostics() } catch (e: Exception) { "Error: ${e.message}" }
                runOnUiThread {
                    if (!isFinishing && !isDestroyed) { output.text = result; button.isEnabled = true }
                }
            }
        }
    }
    override fun onDestroy() { worker.shutdown(); super.onDestroy() }
    companion object { init { System.loadLibrary("wa_legacy") } }
}
