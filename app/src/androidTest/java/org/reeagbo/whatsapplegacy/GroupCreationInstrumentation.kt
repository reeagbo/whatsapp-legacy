package org.reeagbo.whatsapplegacy

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import java.util.concurrent.atomic.AtomicReference

/** Drive the real event/UI boundary with synthetic results; never create a real group. */
class GroupCreationInstrumentation : Instrumentation() {
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        start()
    }
    override fun onStart() {
        val result=Bundle()
        var activity: MainActivity?=null
        try {
            activity=startActivitySync(Intent(targetContext,MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
            waitForIdleSync()
            val target=activity
            val failure=AtomicReference<Throwable?>()
            runOnMainSync {
                try {
                    fun set(name: String,value: Any) {
                        MainActivity::class.java.getDeclaredField(name).apply { isAccessible=true }.set(target,value)
                    }
                    fun get(name: String): Any? = MainActivity::class.java.getDeclaredField(name)
                        .apply { isAccessible=true }.get(target)
                    val update=MainActivity::class.java.getDeclaredMethod("updateStatusLabel").apply { isAccessible=true }
                    val events=MainActivity::class.java.getDeclaredMethod("applyEvents",List::class.java).apply { isAccessible=true }
                    val connected="Conectado · texto cifrado"
                    fun begin() {
                        set("connected",true);set("statusText",connected);set("creatingGroup",true)
                        update.invoke(target)
                        check((get("statusLabel") as TextView).text.toString()=="Creando grupo…")
                    }
                    fun completed() {
                        check(get("creatingGroup")==false)
                        check((get("statusLabel") as TextView).text.toString()==connected)
                    }
                    begin()
                    val jid="120363000000099@g.us"
                    events.invoke(target,listOf(arrayOf("group","created",jid,"Grupo sintético","1","0","")))
                    completed()
                    check(get("conversation")==jid)
                    // Error and timeout must also restore connection status.
                    for(state in listOf("-1","-2")) {
                        begin()
                        events.invoke(target,listOf(arrayOf("group-result","","","Resultado sintético",state,"0","")))
                        completed()
                    }
                    begin();set("connected",false)
                    MainActivity::class.java.getDeclaredMethod("render",String::class.java,String::class.java,Boolean::class.javaPrimitiveType)
                        .apply { isAccessible=true }.invoke(target,"Sin conexión","",false)
                    check(get("creatingGroup")==false)
                    check((get("statusLabel") as TextView).text.toString()=="Sin conexión")
                } catch(error: Throwable) { failure.set(error) }
            }
            failure.get()?.let { throw it }
            result.putString("stream","\nPASS: group creation confirmation opens its chat and clears progress; errors, timeout and disconnect clear progress\n")
            finish(Activity.RESULT_OK,result)
        } catch(error: Throwable) {
            result.putString("stream","\nFAIL: group creation UI: $error\n")
            finish(Activity.RESULT_CANCELED,result)
        }
    }
}
