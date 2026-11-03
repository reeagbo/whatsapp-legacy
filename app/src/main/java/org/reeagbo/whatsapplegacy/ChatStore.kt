package org.reeagbo.whatsapplegacy

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper

/** Android conversation storage. Protocol/Signal state belongs to the C core. */
class ChatStore(context: Context) : SQLiteOpenHelper(context, "conversations.db", null, 2) {
    data class Chat(val jid: String, val name: String, val preview: String, val time: Long)
    data class Message(val id: String, val peer: String, val text: String, val outgoing: Boolean,
                       val status: Int, val time: Long, val note: String)
    override fun onConfigure(db: SQLiteDatabase) {
        db.setForeignKeyConstraintsEnabled(true)
        db.execSQL("PRAGMA synchronous=FULL")
    }
    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE chats(jid TEXT PRIMARY KEY, name TEXT NOT NULL, updated INTEGER NOT NULL)")
        createMessages(db)
    }
    private fun createMessages(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE messages(id TEXT NOT NULL, peer TEXT NOT NULL REFERENCES chats(jid), text TEXT NOT NULL, outgoing INTEGER NOT NULL, status INTEGER NOT NULL, time INTEGER NOT NULL, note TEXT NOT NULL, PRIMARY KEY(peer,id,outgoing))")
        db.execSQL("CREATE INDEX messages_peer_time ON messages(peer,time)")
    }
    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
        if (oldVersion < 2) {
            // SQLiteOpenHelper wraps the migration in one transaction.
            db.execSQL("ALTER TABLE messages RENAME TO messages_v1")
            db.execSQL("DROP INDEX IF EXISTS messages_peer_time")
            createMessages(db)
            db.execSQL("INSERT INTO messages(id,peer,text,outgoing,status,time,note) SELECT id,peer,text,outgoing,status,time,note FROM messages_v1")
            db.execSQL("DROP TABLE messages_v1")
        }
    }
    fun addChat(jid: String, name: String = "") {
        // execSQL propagates storage failures instead of returning the same -1 as a duplicate.
        writableDatabase.execSQL("INSERT OR IGNORE INTO chats(jid,name,updated) VALUES(?,?,?)",
            arrayOf(jid, name, System.currentTimeMillis()))
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
            var exists = false
            db.rawQuery("SELECT text FROM messages WHERE peer=? AND id=? AND outgoing=?",
                arrayOf(jid, id, if (outgoing) "1" else "0")).use { c ->
                if (c.moveToFirst()) {
                    // Delivery can repeat after a crash; retain the first committed copy.
                    exists = true
                }
            }
            if (!exists) {
                db.insertOrThrow("messages", null, ContentValues().apply {
                    put("id", id); put("peer", jid); put("text", text); put("outgoing", if(outgoing) 1 else 0)
                    put("status", status); put("time", time); put("note", "")
                })
                db.update("chats", ContentValues().apply { put("updated", time) }, "jid=?", arrayOf(jid))
            } else if (outgoing && status >= 2) {
                db.execSQL("UPDATE messages SET status=MAX(status,?),note='' WHERE peer=? AND id=? AND outgoing=1", arrayOf(status,jid,id))
            }
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
    }
    fun status(id: String, next: Int, note: String, peer: String = ""): Boolean {
        val db = writableDatabase
        var matched = false
        db.beginTransaction()
        try {
            db.rawQuery("SELECT status,peer FROM messages WHERE id=? AND outgoing=1 AND (?='' OR peer=?)",
                arrayOf(id,peer,peer)).use { c ->
                while (c.moveToNext()) {
                    matched = true
                    val old = c.getInt(0)
                    val actualPeer = c.getString(1)
                    val resolved = MessageState.advance(old, next)
                    if (resolved == next) db.update("messages", ContentValues().apply {
                        put("status", resolved); put("note", note)
                    }, "id=? AND peer=? AND outgoing=1", arrayOf(id,actualPeer))
                }
            }
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
        return matched
    }
    fun interrupted() {
        writableDatabase.execSQL("UPDATE messages SET status=-2,note='Conexión interrumpida; no se conoce el resultado' WHERE outgoing=1 AND status IN (0,1)")
    }
}
