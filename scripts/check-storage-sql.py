"""Execute production SQL literals without compiling C, Kotlin or an APK.

Covers schema upgrades, reopen/crash rollback and LID/phone conversation merges.
It does not validate Kotlin control flow or Android runtime permissions.
"""
from pathlib import Path
import re
import sqlite3
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'app/src/main/java/org/reeagbo/whatsapplegacy/ChatStore.kt').read_text()
def literals(block):
    return re.findall(r'db\.execSQL\("([^"]+)"', block)
group_schema = literals(source.split('private fun createGroups',1)[1].split('fun updateGroup',1)[0])
new_schema = literals(source.split('private fun createMessages',1)[1].split('private fun createContacts',1)[0])
contact_schema = literals(source.split('private fun createContacts',1)[1].split('private fun createSync',1)[0])
sync_schema = literals(source.split('private fun createSync',1)[1].split('private fun addArchiveColumn',1)[0])
archive_schema = literals(source.split('private fun addArchiveColumn',1)[1].split('override fun onUpgrade',1)[0])
upgrade = source.split('override fun onUpgrade',1)[1].split('if (oldVersion < 3)',1)[0]
migration2 = []
for match in re.finditer(r'db\.execSQL\("([^"]+)"\)|createMessages\(db\)', upgrade):
    migration2.extend([match.group(1)] if match.group(1) else new_schema)
assert len(migration2) == 6 and len(contact_schema) == 2
old_messages = 'CREATE TABLE messages(id TEXT PRIMARY KEY, peer TEXT NOT NULL REFERENCES chats(jid), text TEXT NOT NULL, outgoing INTEGER NOT NULL, status INTEGER NOT NULL, time INTEGER NOT NULL, note TEXT NOT NULL)'
chats = literals(source.split('override fun onCreate',1)[1].split('private fun createMessages',1)[0])[0]
initial = [('out-1','a@s.whatsapp.net','Texto conservado: café 🌍',1,3,100,'Entregado'),
           ('in-1','b@s.whatsapp.net','Respuesta',0,0,101,'')]
with tempfile.TemporaryDirectory(prefix='wa-sql-') as folder:
    for version in (1,2,3,4,5):
        path = Path(folder)/f'v{version}.db'
        db = sqlite3.connect(path); db.execute('PRAGMA foreign_keys=ON')
        db.execute(chats)
        if version == 1:
            db.execute(old_messages);db.execute('CREATE INDEX messages_peer_time ON messages(peer,time)')
        else:
            for sql in new_schema: db.execute(sql)
        if version >= 3:
            for sql in contact_schema: db.execute(sql)
        if version >= 4:
            for sql in sync_schema: db.execute(sql)
        if version == 5: db.execute(archive_schema[0])
        db.executemany('INSERT INTO chats(jid,name,updated) VALUES(?,?,?)',[('a@s.whatsapp.net','A',100),('b@s.whatsapp.net','B',101)])
        db.executemany('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',initial);db.commit()
        migration = (migration2 if version == 1 else []) + (contact_schema if version < 3 else []) + (sync_schema if version < 4 else []) + (archive_schema if version < 5 else archive_schema[1:])
        db.execute('BEGIN')
        for sql in migration[:-1]: db.execute(sql)
        db.rollback()
        assert db.execute('SELECT * FROM messages ORDER BY id').fetchall() == sorted(initial)
        assert not any(row[1] == "archived_changed" for row in db.execute("PRAGMA table_info(chats)"))
        db.execute('BEGIN')
        for sql in migration: db.execute(sql)
        db.commit();db.close()
        db=sqlite3.connect(path);db.execute('PRAGMA foreign_keys=ON')
        assert db.execute('SELECT * FROM messages ORDER BY id').fetchall() == sorted(initial)
        assert db.execute('SELECT DISTINCT archived FROM chats').fetchall() == [(0,)]
        db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',('out-1','b@s.whatsapp.net','Another peer',1,0,102,''))
        db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',('out-1','a@s.whatsapp.net','Incoming',0,0,103,''))
        query=re.search(r'"(SELECT status,peer FROM messages[^"]+)"',source).group(1)
        assert db.execute(query,('out-1','self@s.whatsapp.net','self@s.whatsapp.net')).fetchall()==[]
        assert db.execute(query,('out-1','a@s.whatsapp.net','a@s.whatsapp.net')).fetchall()==[(3,'a@s.whatsapp.net')]
        assert len(db.execute(query,('out-1','','')).fetchall())==2
        try: db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',initial[0])
        except sqlite3.IntegrityError: pass
        else: raise AssertionError('Duplicate message key accepted')
        db.commit();db.close()

    # Exercise every production merge statement, with bound arguments in source order.
    merge = literals(source.split('fun link(',1)[1].split('fun interrupted()',1)[0])
    lid,pn='123456789@lid','34123456789@s.whatsapp.net'
    args=[(lid,pn),(pn,lid),(lid,lid,lid,lid,lid,lid,pn),(pn,lid),(pn,pn,lid),(pn,lid),(lid,lid,lid,pn),
          (pn,lid),(lid,pn),(pn,),(lid,),(lid,),(lid,),(lid,),(lid,),
          (pn,),(pn,),(pn,pn,pn),(pn,lid),(lid,)]
    assert len(merge)==len(args)==20
    path=Path(folder)/'merge.db'
    db=sqlite3.connect(path);db.execute('PRAGMA foreign_keys=ON')
    for sql in [chats]+new_schema+contact_schema+sync_schema+archive_schema+group_schema: db.execute(sql)
    db.executemany('INSERT INTO chats(jid,name,updated) VALUES(?,?,?)',[(lid,'Alias local',200),(pn,'Nombre manual',100)])
    db.execute('UPDATE chats SET archived=1 WHERE jid=?',(lid,))
    rows=[('same',lid,'Duplicate alias',1,4,200,''),('same',pn,'First committed content',1,1,100,'Esperando'),
          ('same',lid,'Incoming independent',0,0,201,''),('other',lid,'Sólo LID 🌍',0,0,202,'')]
    db.executemany('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',rows)
    db.execute('INSERT INTO contact_names VALUES(?,?)',(lid,'Nombre recibido'));db.commit()
    # Any interruption, including after deleting old messages, rolls everything back.
    for step in range(1,len(merge)+1):
        db.execute('BEGIN')
        for sql,bound in list(zip(merge,args))[:step]:db.execute(sql,bound)
        db.rollback()
        assert db.execute('SELECT COUNT(*) FROM chats').fetchone()[0]==2
        assert db.execute('SELECT COUNT(*) FROM messages').fetchone()[0]==4
        assert db.execute('SELECT COUNT(*) FROM aliases').fetchone()[0]==0
    db.execute('BEGIN')
    for sql,bound in zip(merge,args): db.execute(sql,bound)
    db.commit();db.close()
    db=sqlite3.connect(path);db.execute('PRAGMA foreign_keys=ON')
    assert db.execute('SELECT * FROM chats').fetchall()==[(pn,'Nombre manual',200,1,0)]
    assert db.execute('SELECT pn FROM aliases WHERE lid=?',(lid,)).fetchone()==(pn,)
    assert db.execute('SELECT name FROM contact_names WHERE jid=?',(pn,)).fetchone()==('Nombre recibido',)
    assert db.execute('SELECT COUNT(*) FROM messages').fetchone()[0]==3
    assert db.execute('SELECT text,status,note FROM messages WHERE id=? AND outgoing=1',('same',)).fetchone()==('First committed content',4,'')
    assert db.execute('SELECT text FROM messages WHERE id=? AND outgoing=0',('same',)).fetchone()==('Incoming independent',)
    assert not db.execute('PRAGMA foreign_key_check').fetchall()
    # An alias with no PN chat preserves its manual name and original timestamps.
    second='987654321@lid';phone='34987654321@s.whatsapp.net'
    db.execute('INSERT INTO chats(jid,name,updated) VALUES(?,?,?)',(second,'Conservado',50));db.commit()
    db.execute('BEGIN')
    for sql,bound in zip(merge,args):db.execute(sql,tuple(second if v==lid else phone if v==pn else v for v in bound))
    db.commit()
    assert db.execute('SELECT name,updated FROM chats WHERE jid=?',(phone,)).fetchone()==('Conservado',50)
    # Replaying an inbox event under its old LID resolves to the canonical dedup key.
    resolve=re.search(r'"(SELECT pn FROM aliases[^"]+)"',source).group(1)
    assert db.execute(resolve,(lid,)).fetchone()[0]==pn
    # Re-imported history retains its earliest timestamp and delivery state.
    historic=re.search(r'"(UPDATE messages SET time=MIN\(time,[^"]+)"',source).group(1)
    db.execute(historic,(25,pn,'same',1));db.commit();db.close()
    db=sqlite3.connect(path)
    assert db.execute('SELECT text,status,time FROM messages WHERE peer=? AND id=? AND outgoing=1',(pn,'same')).fetchone()==('First committed content',4,25)
    db.close()
    db=sqlite3.connect(path)
    db.execute('PRAGMA foreign_keys=ON')
    delete = literals(source.split('fun applyChatDeletion',1)[1].split('fun beginHistoryPage',1)[0])
    for sql,bound in zip(delete,[(pn,),(100,500,0,pn),(pn,pn),(pn,),(pn,pn),(pn,)]):
        if not sql.startswith("DELETE FROM chats"):
            db.execute(sql,bound)
    assert db.execute('SELECT COUNT(*) FROM chats WHERE jid=?',(pn,)).fetchone()==(1,)
    assert db.execute('SELECT COUNT(*) FROM messages WHERE peer=? AND time<=100',(pn,)).fetchone()==(0,)
    cutoff=re.search(r'"(SELECT 1 FROM chat_deletions WHERE peer=\? AND cutoff>=\?)"',source).group(1)
    assert db.execute(cutoff,(pn,'100')).fetchone()==(1,)
    assert db.execute(cutoff,(pn,'101')).fetchone() is None
    # Delete and edit markers survive a LID merge even when the PN already has an edit.
    mutations=literals(source.split('fun applyMessageMutation',1)[1].split('fun applyChatDeletion',1)[0])
    db.execute(mutations[0],(phone,'gone',0,'edit',7,'stale'))
    db.execute(mutations[0],(second,'gone',0,'delete',8,''))
    db.execute(mutations[0],(phone,'edited',1,'edit',5,'older'))
    db.execute(mutations[0],(second,'edited',1,'edit',8,'newer'))
    db.execute('DELETE FROM aliases WHERE lid=?',(second,))
    for sql,bound in zip(merge,args):
        db.execute(sql,tuple(second if v==lid else phone if v==pn else v for v in bound))
    assert db.execute('SELECT kind,text FROM message_mutations WHERE peer=? AND id=? AND outgoing=0',(phone,'gone')).fetchone()==('delete','')
    assert db.execute('SELECT kind,text FROM message_mutations WHERE peer=? AND id=? AND outgoing=1',(phone,'edited')).fetchone()==('edit','newer')
    assert db.execute('SELECT 1 FROM message_mutations WHERE peer=? AND id=? AND outgoing=1',(phone,'gone')).fetchone() is None
    visible = re.search(r'"(SELECT c.jid,c.name,[^"]+)"', source).group(1)
    db.execute('DELETE FROM messages')
    assert db.execute(visible, ('', '-1', '')).fetchall() == []
    assert db.execute(visible, (pn, '-1', pn)).fetchone()[0] == pn
    db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)', ('new', pn, 'Visible', 0, 0, 101, ''))
    assert [row[0] for row in db.execute(visible, ('', '-1', ''))] == [pn]
    assert db.execute(visible, ('', '0', '')).fetchall() == []
    assert [row[0] for row in db.execute(visible, ('', '1', ''))] == [pn]
    archive_update = literals(source.split('fun setArchived',1)[1].split('fun chat(',1)[0])[0]
    db.execute(archive_update,(0,300,pn,300))
    db.execute(archive_update,(1,299,pn,299))
    assert [row[0] for row in db.execute(visible, ('', '0', ''))] == [pn]
    assert db.execute(visible, ('', '1', '')).fetchall() == []
    # A replayed remote delete removes the old rows, preserving later traffic.
    for _ in range(2):
        for sql,bound in zip(delete,[(pn,),(100,500,1,pn),(pn,pn),(pn,),(pn,pn),(pn,)]):
            db.execute(sql,bound)
    assert [row[0] for row in db.execute(visible, ('', '-1', ''))] == [pn]
    # A local delete advances the cutoff and removes the whole visible chat.
    for sql,bound in zip(delete,[(pn,),(200,200,1,pn),(pn,pn),(pn,),(pn,pn),(pn,)]):
        db.execute(sql,bound)
    assert db.execute(visible, ('', '-1', '')).fetchall() == []
    assert db.execute(visible, (pn, '-1', pn)).fetchone() is None
    assert db.execute(cutoff,(pn,'101')).fetchone() == (1,)
    assert db.execute(cutoff,(pn,'201')).fetchone() is None
    assert db.execute(cutoff,(phone,'101')).fetchone() is None
    db.commit()
    db.close()
# Version 6 -> 7 preserves messages while adding group metadata and authors.
db=sqlite3.connect(':memory:')
for sql in [chats]+new_schema+contact_schema+sync_schema+archive_schema:db.execute(sql)
db.execute('INSERT INTO chats(jid,name,updated) VALUES(?,?,?)',('120363000000001@g.us','',100))
db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',('g1','120363000000001@g.us','Group text',0,0,100,''))
db.commit();db.execute('BEGIN')
for sql in group_schema:db.execute(sql)
db.rollback()
assert db.execute('SELECT text FROM messages').fetchone()==('Group text',)
for sql in group_schema:db.execute(sql)
db.execute('INSERT INTO message_authors VALUES(?,?,?,?)',('120363000000001@g.us','g1',0,'34123456789@s.whatsapp.net'))
message_query=re.search(r'"(SELECT id,peer,text,outgoing,status,time,note,COALESCE[^"]+)"',source).group(1)
assert db.execute(message_query,('120363000000001@g.us','100')).fetchone()[7]=='34123456789@s.whatsapp.net'
print('PASS: v6 -> v7 group/author migration, transactional rollback and original message preservation')
print('PASS: v1–v5 upgrades to v6, rollback, reopen, Unicode, peer/direction dedup, receipt attribution, alias merges, archive filtering and persistent deletion markers. No compilation performed.')
