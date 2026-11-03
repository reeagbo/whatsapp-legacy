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
new_schema = literals(source.split('private fun createMessages',1)[1].split('private fun createContacts',1)[0])
contact_schema = literals(source.split('private fun createContacts',1)[1].split('override fun onUpgrade',1)[0])
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
    for version in (1,2):
        path = Path(folder)/f'v{version}.db'
        db = sqlite3.connect(path); db.execute('PRAGMA foreign_keys=ON')
        db.execute(chats)
        if version == 1:
            db.execute(old_messages);db.execute('CREATE INDEX messages_peer_time ON messages(peer,time)')
        else:
            for sql in new_schema: db.execute(sql)
        db.executemany('INSERT INTO chats VALUES(?,?,?)',[('a@s.whatsapp.net','A',100),('b@s.whatsapp.net','B',101)])
        db.executemany('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',initial);db.commit()
        migration = (migration2 if version == 1 else []) + contact_schema
        db.execute('BEGIN')
        for sql in migration[:-1]: db.execute(sql)
        db.rollback()
        assert db.execute('SELECT * FROM messages ORDER BY id').fetchall() == sorted(initial)
        assert db.execute("SELECT 1 FROM sqlite_master WHERE name IN ('messages_v1','aliases')").fetchone() is None
        db.execute('BEGIN')
        for sql in migration: db.execute(sql)
        db.commit();db.close()
        db=sqlite3.connect(path);db.execute('PRAGMA foreign_keys=ON')
        assert db.execute('SELECT * FROM messages ORDER BY id').fetchall() == sorted(initial)
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
    args=[(lid,pn),(pn,lid),(lid,lid,pn),(pn,lid),(lid,pn),(pn,),
          (lid,),(lid,),(pn,lid),(lid,)]
    assert len(merge)==len(args)==10
    path=Path(folder)/'merge.db'
    db=sqlite3.connect(path);db.execute('PRAGMA foreign_keys=ON')
    for sql in [chats]+new_schema+contact_schema: db.execute(sql)
    db.executemany('INSERT INTO chats VALUES(?,?,?)',[(lid,'Alias local',200),(pn,'Nombre manual',100)])
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
    assert db.execute('SELECT * FROM chats').fetchall()==[(pn,'Nombre manual',200)]
    assert db.execute('SELECT pn FROM aliases WHERE lid=?',(lid,)).fetchone()==(pn,)
    assert db.execute('SELECT name FROM contact_names WHERE jid=?',(pn,)).fetchone()==('Nombre recibido',)
    assert db.execute('SELECT COUNT(*) FROM messages').fetchone()[0]==3
    assert db.execute('SELECT text,status,note FROM messages WHERE id=? AND outgoing=1',('same',)).fetchone()==('First committed content',4,'')
    assert db.execute('SELECT text FROM messages WHERE id=? AND outgoing=0',('same',)).fetchone()==('Incoming independent',)
    assert not db.execute('PRAGMA foreign_key_check').fetchall()
    # An alias with no PN chat preserves its manual name and original timestamps.
    second='987654321@lid';phone='34987654321@s.whatsapp.net'
    db.execute('INSERT INTO chats VALUES(?,?,?)',(second,'Conservado',50));db.commit()
    db.execute('BEGIN')
    for sql,bound in zip(merge,args):db.execute(sql,tuple(second if v==lid else phone if v==pn else v for v in bound))
    db.commit()
    assert db.execute('SELECT name,updated FROM chats WHERE jid=?',(phone,)).fetchone()==('Conservado',50)
    # Replaying an inbox event under its old LID resolves to the canonical dedup key.
    resolve=re.search(r'"(SELECT pn FROM aliases[^"]+)"',source).group(1)
    assert db.execute(resolve,(lid,)).fetchone()[0]==pn
    db.close()
print('PASS: v1/v2 upgrades to v3, rollback, reopen, Unicode, peer/direction dedup, receipt attribution, atomic LID/PN merges, names and delivery preservation. No compilation performed.')
