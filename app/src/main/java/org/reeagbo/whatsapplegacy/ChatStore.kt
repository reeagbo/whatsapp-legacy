package org.reeagbo.whatsapplegacy

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper

/** Android conversation storage. Protocol/Signal state belongs to the C core. */
class ChatStore(context: Context) : SQLiteOpenHelper(context, "conversations.db", null, 1) {
    data class Chat(val jid: String, val name: String, val preview: String, val time: Long)
    data class Message(val id: String, val peer: String, val text: String, val outgoing: Boolean,
                       val status: Int, val time: Long, val note: String)
    override fun onConfigure(db: SQLiteDatabase) { db.setForeignKeyConstraintsEnabled(true) }
    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE chats(jid TEXT PRIMARY KEY, name TEXT NOT NULL, updated INTEGER NOT NULL)")
        db.execSQL("CREATE TABLE messages(id TEXT PRIMARY KEY, peer TEXT NOT NULL REFERENCES chats(jid), text TEXT NOT NULL, outgoing INTEGER NOT NULL, status INTEGER NOT NULL, time INTEGER NOT NULL, note TEXT NOT NULL)")
        db.execSQL("CREATE INDEX messages_peer_time ON messages(peer,time)")
    }
    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) = Unit
    fun addChat(jid: String, name: String = "") {
        writableDatabase.insertWithOnConflict("chats", null, ContentValues().apply {
            put("jid", jid); put("name", name); put("updated", System.currentTimeMillis())
        }, SQLiteDatabase.CONFLICT_IGNORE)
        if (name.isNotBlank()) writableDatabase.update("chats", ContentValues().apply { put("name", name) }, "jid=?", arrayOf(jid))
    }
    fun chats(): List<Chat> = readableDatabase.rawQuery(
        "SELECT c.jid,c.name,COALESCE((SELECT text FROM messages m WHERE m.peer=c.jid ORDER BY time DESC,rowid DESC LIMIT 1),''),c.updated FROM chats c ORDER BY c.updated DESC", null
    ).use { c -> buildList { while (c.moveToNext()) add(Chat(c.getString(0), c.getString(1), c.getString(2), c.getLong(3))) } }
    fun messages(jid: String): List<Message> = readableDatabase.rawQuery(
        "SELECT id,peer,text,outgoing,status,time,note FROM messages WHERE peer=? ORDER BY time DESC,rowid DESC LIMIT 200", arrayOf(jid)
    ).use { c -> buildList { while (c.moveToNext()) add(Message(c.getString(0),c.getString(1),c.getString(2),c.getInt(3)!=0,c.getInt(4),c.getLong(5),c.getString(6))) }.reversed() }
    fun append(id: String, jid: String, text: String, outgoing: Boolean, status: Int) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            addChat(jid)
            val time = System.currentTimeMillis()
            val row = db.insertWithOnConflict("messages", null, ContentValues().apply {
                put("id", id); put("peer", jid); put("text", text); put("outgoing", if(outgoing) 1 else 0)
                put("status", status); put("time", time); put("note", "")
            }, SQLiteDatabase.CONFLICT_IGNORE)
            if (row != -1L) db.update("chats", ContentValues().apply { put("updated", time) }, "jid=?", arrayOf(jid))
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
    }
    fun status(id: String, next: Int, note: String) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            db.rawQuery("SELECT status FROM messages WHERE id=? AND outgoing=1", arrayOf(id)).use { c ->
                if (c.moveToFirst()) {
                    val old = c.getInt(0)
                    // Late acknowledgements must not downgrade delivered/read messages.
                    if (next > old || (next < 0 && old < 2)) db.update("messages", ContentValues().apply {
                        put("status", next); put("note", note)
                    }, "id=?", arrayOf(id))
                }
            }
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
    }
    fun interrupted() {
        writableDatabase.execSQL("UPDATE messages SET status=-2,note='Conexión interrumpida; no se conoce el resultado' WHERE outgoing=1 AND status IN (0,1)")
    }
}
