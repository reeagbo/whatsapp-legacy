"""Check the literal SQLite migration from ChatStore.kt without compiling Android.

This verifies SQL/data preservation only, not Kotlin or Android runtime behavior.
"""
from pathlib import Path
import re
import sqlite3
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'app/src/main/java/org/reeagbo/whatsapplegacy/ChatStore.kt').read_text()
def literals(block):
    return re.findall(r'db\.execSQL\("([^"]+)"', block)
create_block=source.split('private fun createMessages',1)[1].split('override fun onUpgrade',1)[0]
new_schema=literals(create_block)
upgrade=source.split('override fun onUpgrade',1)[1].split('fun addChat',1)[0]
migration=[]
for match in re.finditer(r'db\.execSQL\("([^"]+)"\)|createMessages\(db\)',upgrade):
    migration.extend([match.group(1)] if match.group(1) else new_schema)
assert len(migration)==6
old_messages='CREATE TABLE messages(id TEXT PRIMARY KEY, peer TEXT NOT NULL REFERENCES chats(jid), text TEXT NOT NULL, outgoing INTEGER NOT NULL, status INTEGER NOT NULL, time INTEGER NOT NULL, note TEXT NOT NULL)'
chats=literals(source.split('override fun onCreate',1)[1].split('private fun createMessages',1)[0])[0]
initial=[('out-1','a@s.whatsapp.net','Texto conservado: café 🌍',1,3,100,'Entregado'),
         ('in-1','b@s.whatsapp.net','Respuesta',0,0,101,'')]
with tempfile.TemporaryDirectory(prefix='wa-sql-') as folder:
    path=Path(folder)/'conversations.db'
    db=sqlite3.connect(path)
    db.execute('PRAGMA foreign_keys=ON')
    db.execute(chats);db.execute(old_messages)
    db.execute('CREATE INDEX messages_peer_time ON messages(peer,time)')
    db.executemany('INSERT INTO chats VALUES(?,?,?)',[('a@s.whatsapp.net','A',100),('b@s.whatsapp.net','B',101)])
    db.executemany('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',initial);db.commit()
    # Interrupted migration must leave the old schema and data intact.
    db.execute('BEGIN')
    for sql in migration[:-1]:db.execute(sql)
    db.rollback()
    assert db.execute('SELECT * FROM messages ORDER BY id').fetchall()==sorted(initial)
    assert db.execute("SELECT 1 FROM sqlite_master WHERE name='messages_v1'").fetchone() is None
    db.execute('BEGIN')
    for sql in migration:db.execute(sql)
    db.commit();db.close()
    db=sqlite3.connect(path);db.execute('PRAGMA foreign_keys=ON')
    assert db.execute('SELECT * FROM messages ORDER BY id').fetchall()==sorted(initial)
    # IDs from another conversation or direction must not erase a message.
    db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',('out-1','b@s.whatsapp.net','Another peer',1,0,102,''))
    db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',('out-1','a@s.whatsapp.net','Incoming',0,0,103,''))
    query=re.search(r'"(SELECT status,peer FROM messages[^"]+)"',source).group(1)
    assert db.execute(query,('out-1','self@s.whatsapp.net','self@s.whatsapp.net')).fetchall()==[]
    assert db.execute(query,('out-1','a@s.whatsapp.net','a@s.whatsapp.net')).fetchall()==[(3,'a@s.whatsapp.net')]
    assert len(db.execute(query,('out-1','','')).fetchall())==2
    try:
        db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',initial[0])
    except sqlite3.IntegrityError:pass
    else:raise AssertionError('Duplicate message key accepted')
    assert not db.execute('PRAGMA foreign_key_check').fetchall()
    db.close()
print('PASS: literal SQLite migration, rollback, reopen, Unicode preservation, peer/direction deduplication and receipt attribution. No compilation performed.')
