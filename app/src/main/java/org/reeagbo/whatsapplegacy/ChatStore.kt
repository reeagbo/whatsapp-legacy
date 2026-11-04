package org.reeagbo.whatsapplegacy

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper

/** Android conversation storage. Protocol/Signal state belongs to the C core. */
class ChatStore(context: Context) : SQLiteOpenHelper(context, "conversations.db", null, 4) {
    data class Chat(val jid: String, val name: String, val preview: String, val time: Long, val pushName: String)
    data class Message(val id: String, val peer: String, val text: String, val outgoing: Boolean,
                       val status: Int, val time: Long, val note: String)
    override fun onConfigure(db: SQLiteDatabase) {
        db.setForeignKeyConstraintsEnabled(true)
        db.execSQL("PRAGMA synchronous=FULL")
    }
    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE chats(jid TEXT PRIMARY KEY, name TEXT NOT NULL, updated INTEGER NOT NULL)")
        createMessages(db)
        createContacts(db)
        createSync(db)
    }
    private fun createMessages(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE messages(id TEXT NOT NULL, peer TEXT NOT NULL REFERENCES chats(jid), text TEXT NOT NULL, outgoing INTEGER NOT NULL, status INTEGER NOT NULL, time INTEGER NOT NULL, note TEXT NOT NULL, PRIMARY KEY(peer,id,outgoing))")
        db.execSQL("CREATE INDEX messages_peer_time ON messages(peer,time)")
    }
    private fun createContacts(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE aliases(lid TEXT PRIMARY KEY, pn TEXT NOT NULL)")
        db.execSQL("CREATE TABLE contact_names(jid TEXT PRIMARY KEY, name TEXT NOT NULL)")
    }
    private fun createSync(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE message_mutations(peer TEXT NOT NULL,id TEXT NOT NULL,outgoing INTEGER NOT NULL,kind TEXT NOT NULL,changed INTEGER NOT NULL,text TEXT NOT NULL,PRIMARY KEY(peer,id,outgoing))")
        db.execSQL("CREATE TABLE chat_deletions(peer TEXT PRIMARY KEY,cutoff INTEGER NOT NULL,changed INTEGER NOT NULL,remove_chat INTEGER NOT NULL)")
        db.execSQL("CREATE TABLE history_pages(peer TEXT PRIMARY KEY,anchor_id TEXT NOT NULL,anchor_time INTEGER NOT NULL,anchor_outgoing INTEGER NOT NULL,state TEXT NOT NULL,retry_at INTEGER NOT NULL)")
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
        if (oldVersion < 3) createContacts(db)
        if (oldVersion < 4) createSync(db)
    }
    fun addChat(address: String, name: String = "", updated: Long = System.currentTimeMillis()) {
        val jid = resolve(address)
        // execSQL propagates storage failures instead of returning the same -1 as a duplicate.
        writableDatabase.execSQL("INSERT OR IGNORE INTO chats(jid,name,updated) VALUES(?,?,?)",
            arrayOf(jid, name, updated))
        if (name.isNotBlank()) writableDatabase.update("chats", ContentValues().apply { put("name", name) }, "jid=?", arrayOf(jid))
    }
    fun setName(jid: String, name: String) {
        writableDatabase.update("chats", ContentValues().apply { put("name",name) }, "jid=?",arrayOf(resolve(jid)))
    }
    fun containsChat(jid: String): Boolean = readableDatabase.rawQuery(
        "SELECT 1 FROM chats WHERE jid=?",arrayOf(resolve(jid))
    ).use { it.moveToFirst() }
    fun chats(): List<Chat> = queryChats(null)
    fun chat(jid: String): Chat? = queryChats(resolve(jid)).firstOrNull()
    private fun queryChats(jid: String?): List<Chat> = readableDatabase.rawQuery(
        "SELECT c.jid,c.name,COALESCE((SELECT text FROM messages m WHERE m.peer=c.jid ORDER BY time DESC,rowid DESC LIMIT 1),''),c.updated,COALESCE((SELECT name FROM contact_names n WHERE n.jid=c.jid),'') FROM chats c WHERE (?='' AND EXISTS(SELECT 1 FROM messages m WHERE m.peer=c.jid)) OR c.jid=? ORDER BY c.updated DESC", arrayOf(jid.orEmpty(),jid.orEmpty())
    ).use { c -> buildList { while (c.moveToNext()) add(Chat(c.getString(0), c.getString(1), c.getString(2), c.getLong(3), c.getString(4))) } }
    fun messages(jid: String, limit: Int = 100): List<Message> = readableDatabase.rawQuery(
        "SELECT id,peer,text,outgoing,status,time,note FROM messages WHERE peer=? ORDER BY time DESC,id DESC,outgoing DESC LIMIT ?", arrayOf(resolve(jid),limit.coerceIn(1,5000).toString())
    ).use { c -> buildList { while (c.moveToNext()) add(Message(c.getString(0),c.getString(1),c.getString(2),c.getInt(3)!=0,c.getInt(4),c.getLong(5),c.getString(6))) }.reversed() }
    fun hasEarlier(jid: String, first: Message): Boolean = readableDatabase.rawQuery(
        "SELECT 1 FROM messages WHERE peer=? AND (time<? OR (time=? AND (id<? OR (id=? AND outgoing<?)))) LIMIT 1",
        arrayOf(resolve(jid),first.time.toString(),first.time.toString(),first.id,first.id,if(first.outgoing)"1" else "0")
    ).use { it.moveToFirst() }
    fun append(id: String, address: String, text: String, outgoing: Boolean, status: Int, time: Long = System.currentTimeMillis(), historical: Boolean = false) {
        val jid = resolve(address)
        val db = writableDatabase
        db.beginTransaction()
        try {
            if (hiddenByChatDeletion(jid,time)) {
                if(historical)db.execSQL("DELETE FROM messages WHERE peer=? AND id=? AND outgoing=?",arrayOf(jid,id,if(outgoing)1 else 0))
                db.setTransactionSuccessful();return
            }
            var effectiveText=text
            db.rawQuery("SELECT kind,text FROM message_mutations WHERE peer=? AND id=? AND outgoing=?",arrayOf(jid,id,if(outgoing)"1" else "0")).use { c ->
                if(c.moveToFirst()) {
                    if(c.getString(0)=="delete") { db.setTransactionSuccessful();return }
                    effectiveText=c.getString(1)
                }
            }
            addChat(jid,updated=time)
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
                    put("id", id); put("peer", jid); put("text", effectiveText); put("outgoing", if(outgoing) 1 else 0)
                    put("status", status); put("time", time); put("note", "")
                })
                db.execSQL("UPDATE chats SET updated=MAX(updated,?) WHERE jid=?",arrayOf(time,jid))
            } else if (outgoing && status >= 2) {
                db.execSQL("UPDATE messages SET status=MAX(status,?),note='' WHERE peer=? AND id=? AND outgoing=1", arrayOf(status,jid,id))
            }
            if(exists && historical) db.execSQL("UPDATE messages SET time=MIN(time,?) WHERE peer=? AND id=? AND outgoing=?",arrayOf(time,jid,id,if(outgoing)1 else 0))
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
    }
    fun historyTransaction(block: () -> Unit) {
        val db=writableDatabase
        db.beginTransaction()
        try { block();db.setTransactionSuccessful() } finally { db.endTransaction() }
    }
    fun historyChat(jid: String, name: String) {
        if(name.isNotBlank())learnName(jid,name)
    }
    fun historyAnchor(jid: String): Message? = readableDatabase.rawQuery(
        "SELECT id,peer,text,outgoing,status,time,note FROM messages WHERE peer=? ORDER BY time ASC,id ASC,outgoing ASC LIMIT 1",arrayOf(resolve(jid))
    ).use { c -> if(c.moveToFirst()) Message(c.getString(0),c.getString(1),c.getString(2),c.getInt(3)!=0,c.getInt(4),c.getLong(5),c.getString(6)) else null }
    private fun hiddenByChatDeletion(jid: String, time: Long): Boolean = readableDatabase.rawQuery(
        "SELECT 1 FROM chat_deletions WHERE peer=? AND cutoff>=?",arrayOf(jid,time.toString())
    ).use { it.moveToFirst() }
    fun applyMessageMutation(address: String, id: String, outgoing: Boolean, kind: String, changed: Long, text: String = "") {
        require(kind=="edit" || kind=="delete")
        val peer=resolve(address);val db=writableDatabase
        db.beginTransaction()
        try {
            var existingKind="";var existingTime=-1L
            db.rawQuery("SELECT kind,changed FROM message_mutations WHERE peer=? AND id=? AND outgoing=?",arrayOf(peer,id,if(outgoing)"1" else "0")).use { c ->
                if(c.moveToFirst()) { existingKind=c.getString(0);existingTime=c.getLong(1) }
            }
            if(existingKind=="delete" || (kind=="edit" && existingTime>=changed)) { db.setTransactionSuccessful();return }
            db.execSQL("INSERT OR REPLACE INTO message_mutations(peer,id,outgoing,kind,changed,text) VALUES(?,?,?,?,?,?)",arrayOf(peer,id,if(outgoing)1 else 0,kind,changed,if(kind=="delete")"" else text))
            if(kind=="delete") db.execSQL("DELETE FROM messages WHERE peer=? AND id=? AND outgoing=?",arrayOf(peer,id,if(outgoing)1 else 0))
            else db.execSQL("UPDATE messages SET text=? WHERE peer=? AND id=? AND outgoing=?",arrayOf(text,peer,id,if(outgoing)1 else 0))
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
    }
    fun applyChatDeletion(address: String, cutoff: Long, changed: Long, removeChat: Boolean) {
        val peer=resolve(address);val db=writableDatabase
        db.beginTransaction()
        try {
            db.execSQL("INSERT OR IGNORE INTO chat_deletions(peer,cutoff,changed,remove_chat) VALUES(?,0,0,0)",arrayOf(peer))
            db.execSQL("UPDATE chat_deletions SET cutoff=MAX(cutoff,?),changed=MAX(changed,?),remove_chat=MAX(remove_chat,?) WHERE peer=?",arrayOf(cutoff,changed,if(removeChat)1 else 0,peer))
            db.execSQL("DELETE FROM messages WHERE peer=? AND time<=(SELECT cutoff FROM chat_deletions WHERE peer=?)",arrayOf(peer,peer))
            if(removeChat)db.execSQL("DELETE FROM chats WHERE jid=? AND NOT EXISTS(SELECT 1 FROM messages WHERE peer=?)",arrayOf(peer,peer))
            db.execSQL("DELETE FROM history_pages WHERE peer=?",arrayOf(peer))
            db.setTransactionSuccessful()
        } finally { db.endTransaction() }
    }
    fun beginHistoryPage(jid: String, anchor: Message): Boolean {
        val peer=resolve(jid);val now=System.currentTimeMillis();val db=writableDatabase
        var blocked=false
        db.rawQuery("SELECT anchor_id,anchor_time,anchor_outgoing,state,retry_at FROM history_pages WHERE peer=?",arrayOf(peer)).use { c ->
            if(c.moveToFirst()) {
                val same=c.getString(0)==anchor.id && c.getLong(1)==anchor.time && (c.getInt(2)!=0)==anchor.outgoing
                blocked=c.getString(3)=="pending" && c.getLong(4)>now || same && c.getString(3)=="stalled"
            }
        }
        if(blocked)return false
        db.execSQL("INSERT OR REPLACE INTO history_pages(peer,anchor_id,anchor_time,anchor_outgoing,state,retry_at) VALUES(?,?,?,?,?,?)",arrayOf(peer,anchor.id,anchor.time,if(anchor.outgoing)1 else 0,"pending",now+120_000L))
        return true
    }
    fun endHistoryPage(jid: String, succeeded: Boolean) {
        val peer=resolve(jid);val oldest=historyAnchor(peer)
        readableDatabase.rawQuery("SELECT anchor_id,anchor_time,anchor_outgoing FROM history_pages WHERE peer=?",arrayOf(peer)).use { c ->
            if(c.moveToFirst()) {
                val advanced=oldest!=null && (oldest.id!=c.getString(0) || oldest.time!=c.getLong(1) || oldest.outgoing!=(c.getInt(2)!=0))
                val state=if(!succeeded) "error" else if(advanced) "more" else "stalled"
                writableDatabase.execSQL("UPDATE history_pages SET state=?,retry_at=0 WHERE peer=?",arrayOf(state,peer))
            }
        }
    }
    fun status(id: String, next: Int, note: String, peer: String = ""): Boolean {
        val db = writableDatabase
        var matched = false
        db.beginTransaction()
        try {
            db.rawQuery("SELECT status,peer FROM messages WHERE id=? AND outgoing=1 AND (?='' OR peer=?)",
                arrayOf(id,if(peer.isBlank()) "" else resolve(peer),if(peer.isBlank()) "" else resolve(peer))).use { c ->
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
    fun resolve(jid: String): String = readableDatabase.rawQuery(
        "SELECT pn FROM aliases WHERE lid=?", arrayOf(jid)
    ).use { if (it.moveToFirst()) it.getString(0) else jid }

    fun learnName(address: String, proposed: String) {
        val name = proposed.filterNot { Character.isISOControl(it) || Character.getType(it) == Character.FORMAT.toInt() }.trim().take(100)
        if (name.isBlank()) return
        writableDatabase.execSQL("INSERT OR REPLACE INTO contact_names(jid,name) VALUES(?,?)", arrayOf(resolve(address),name))
    }

    /** Merge application records only. Signal sessions keep their original addresses. */
    fun link(lid: String, pn: String): Boolean {
        if (!lid.matches(Regex("^[0-9]{1,40}@lid$")) || !pn.matches(Regex("^[0-9]{1,40}@s\\.whatsapp\\.net$"))) return false
        val db = writableDatabase
        db.beginTransaction()
        try {
            val previous = resolve(lid)
            // Never silently move an already resolved conversation to another person.
            if (previous != lid) { db.setTransactionSuccessful(); return previous == pn }
            db.execSQL("INSERT INTO aliases(lid,pn) VALUES(?,?)", arrayOf(lid,pn))
            db.execSQL("INSERT OR IGNORE INTO chats(jid,name,updated) SELECT ?,name,updated FROM chats WHERE jid=?", arrayOf(pn,lid))
            db.execSQL("UPDATE chats SET name=CASE WHEN name='' THEN COALESCE((SELECT name FROM chats WHERE jid=?),'') ELSE name END,updated=MAX(updated,COALESCE((SELECT updated FROM chats WHERE jid=?),updated)) WHERE jid=?", arrayOf(lid,lid,pn))
            db.execSQL("INSERT OR IGNORE INTO message_mutations(peer,id,outgoing,kind,changed,text) SELECT ?,id,outgoing,kind,changed,text FROM message_mutations WHERE peer=?",arrayOf(pn,lid))
            db.execSQL("INSERT OR REPLACE INTO message_mutations(peer,id,outgoing,kind,changed,text) SELECT ?,old.id,old.outgoing,old.kind,old.changed,old.text FROM message_mutations old JOIN message_mutations current ON current.peer=? AND current.id=old.id AND current.outgoing=old.outgoing WHERE old.peer=? AND current.kind!='delete' AND (old.kind='delete' OR old.changed>current.changed)",arrayOf(pn,pn,lid))
            db.execSQL("INSERT OR IGNORE INTO chat_deletions(peer,cutoff,changed,remove_chat) SELECT ?,cutoff,changed,remove_chat FROM chat_deletions WHERE peer=?",arrayOf(pn,lid))
            db.execSQL("UPDATE chat_deletions SET cutoff=MAX(cutoff,COALESCE((SELECT cutoff FROM chat_deletions WHERE peer=?),0)),changed=MAX(changed,COALESCE((SELECT changed FROM chat_deletions WHERE peer=?),0)),remove_chat=MAX(remove_chat,COALESCE((SELECT remove_chat FROM chat_deletions WHERE peer=?),0)) WHERE peer=?",arrayOf(lid,lid,lid,pn))
            // Duplicate IDs keep their first committed content and the strongest confirmation.
            db.execSQL("INSERT OR IGNORE INTO messages(id,peer,text,outgoing,status,time,note) SELECT id,?,text,outgoing,status,time,note FROM messages WHERE peer=?", arrayOf(pn,lid))
            db.execSQL("UPDATE messages SET status=MAX(status,COALESCE((SELECT old.status FROM messages old WHERE old.peer=? AND old.id=messages.id AND old.outgoing=1),status)) WHERE peer=? AND outgoing=1", arrayOf(lid,pn))
            db.execSQL("UPDATE messages SET note='' WHERE peer=? AND outgoing=1 AND status>=2", arrayOf(pn))
            db.execSQL("DELETE FROM messages WHERE peer=?", arrayOf(lid))
            db.execSQL("DELETE FROM chats WHERE jid=?", arrayOf(lid))
            db.execSQL("DELETE FROM message_mutations WHERE peer=?",arrayOf(lid))
            db.execSQL("DELETE FROM chat_deletions WHERE peer=?",arrayOf(lid))
            db.execSQL("DELETE FROM history_pages WHERE peer=?",arrayOf(lid))
            db.execSQL("DELETE FROM messages WHERE peer=? AND (EXISTS(SELECT 1 FROM message_mutations x WHERE x.peer=messages.peer AND x.id=messages.id AND x.outgoing=messages.outgoing AND x.kind='delete') OR time<=COALESCE((SELECT cutoff FROM chat_deletions WHERE peer=messages.peer),-1))",arrayOf(pn))
            db.execSQL("UPDATE messages SET text=(SELECT text FROM message_mutations x WHERE x.peer=messages.peer AND x.id=messages.id AND x.outgoing=messages.outgoing) WHERE peer=? AND EXISTS(SELECT 1 FROM message_mutations x WHERE x.peer=messages.peer AND x.id=messages.id AND x.outgoing=messages.outgoing AND x.kind='edit')",arrayOf(pn))
            db.execSQL("DELETE FROM chats WHERE jid=? AND EXISTS(SELECT 1 FROM chat_deletions WHERE peer=? AND remove_chat=1) AND NOT EXISTS(SELECT 1 FROM messages WHERE peer=?)",arrayOf(pn,pn,pn))
            db.execSQL("INSERT OR IGNORE INTO contact_names(jid,name) SELECT ?,name FROM contact_names WHERE jid=?", arrayOf(pn,lid))
            db.execSQL("DELETE FROM contact_names WHERE jid=?", arrayOf(lid))
            db.setTransactionSuccessful()
            return true
        } finally { db.endTransaction() }
    }
    fun interrupted() {
        writableDatabase.execSQL("UPDATE messages SET status=-2,note='Conexión interrumpida; no se conoce el resultado' WHERE outgoing=1 AND status IN (0,1)")
    }
}
