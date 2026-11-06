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
                    val store=get("store") as ChatStore
                    store.leaveGroup(jid)
                    events.invoke(target,listOf(arrayOf("group","summary",jid,"Resumen tardío","2","0","")))
                    check(store.groupLeft(jid))
                    store.setArchived(jid,true,100)
                    store.historyArchived(jid,false)
                    check(store.chat(jid)?.archived==true)
                    val bootstrap="120363000000098@g.us"
                    store.historyArchived(bootstrap,true)
                    check(store.chat(bootstrap)?.archived==true)
                    events.invoke(target,listOf(arrayOf("group","",jid,"Miembros actuales","1","0","")))
                    check(!store.groupLeft(jid))
                    // Participating summaries are chats even before text history arrives.
                    val summary="120363000000097@g.us"
                    events.invoke(target,listOf(arrayOf("group","summary",summary,"Grupo sincronizado","2","0","")))
                    check(store.chats(false).any { it.jid==summary })
                    check(store.messages(summary).isEmpty())
                    store.historyPinned(summary,200)
                    check(store.chats(false).first().jid==summary)
                    store.setPinned(summary,0,300)
                    store.historyPinned(summary,200)
                    check(store.chat(summary)?.pinned==0L)
                    store.setPinned(summary,400,400)
                    check(store.chats(false).first().jid==summary)
                    store.historyArchived(summary,true)
                    check(store.chats(false).none { it.jid==summary })
                    check(store.chats(true).any { it.jid==summary && it.pinned==400L })
                    store.deleteChatLocally(summary)
                    events.invoke(target,listOf(arrayOf("group","summary",summary,"Resumen repetido","2","0","")))
                    store.historyChat(summary,"Historial anterior",100)
                    store.historyPinned(summary,200)
                    check(store.chats().none { it.jid==summary })
                    // Reopening an actual v8 database repairs existing hidden group rows.
                    val restored="120363000000096@g.us"
                    val deleted="120363000000095@g.us"
                    val db=store.writableDatabase
                    db.execSQL("INSERT INTO chats(jid,name,updated,pinned) VALUES(?,?,?,?)",arrayOf(restored,"Grupo recuperado",100,200))
                    db.execSQL("INSERT INTO group_metadata(jid,visible) VALUES(?,0)",arrayOf(restored))
                    db.execSQL("INSERT INTO chats(jid,name,updated,pinned) VALUES(?,?,?,?)",arrayOf(deleted,"Borrado",100,200))
                    db.execSQL("INSERT INTO chat_deletions VALUES(?,?,?,1)",arrayOf(deleted,500,500))
                    db.version=8
                    store.close()
                    check(store.chats(false).first().jid==restored)
                    check(store.messages(restored).isEmpty())
                    check(store.chats().none { it.jid==deleted || it.jid==summary })
                    check(store.writableDatabase.version==9)
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
            result.putString("stream","\nPASS: group creation confirmation opens its chat and clears progress; errors, timeout and disconnect clear progress; group summaries/pins without text, deleted replay and v8 migration verified\n")
            finish(Activity.RESULT_OK,result)
        } catch(error: Throwable) {
            result.putString("stream","\nFAIL: group creation UI: $error\n")
            finish(Activity.RESULT_CANCELED,result)
        }
    }
}
