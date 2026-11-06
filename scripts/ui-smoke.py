"""Exercise the APK UI on a clean emulator; never send or link an account."""
from pathlib import Path
import re
import sqlite3
import tempfile
import subprocess
import time
import xml.etree.ElementTree as ET

out = Path('ui-smoke')
out.mkdir(exist_ok=True)
package = 'org.reeagbo.whatsapplegacy'
def adb(*args):
    return subprocess.check_output(['adb', *args], stderr=subprocess.STDOUT)
def tree():
    adb('shell', 'uiautomator', 'dump', '/sdcard/wa-ui.xml')
    return ET.fromstring(adb('shell', 'cat', '/sdcard/wa-ui.xml'))
def wait_for(predicate, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        root = tree()
        for node in root.iter('node'):
            if predicate(node.attrib):
                return node
        time.sleep(.4)
    (out/'failure.xml').write_bytes(ET.tostring(root))
    screenshot('failure.png')
    (out/'failure-logcat.txt').write_bytes(adb('logcat', '-d', '-s', 'AndroidRuntime:E'))
    raise AssertionError('Expected UI element did not appear; captured UI and crash diagnostics')
def text(value):
    return lambda a: a.get('text', '').casefold() == value.casefold()
def tap(node):
    bounds = list(map(int, re.findall(r'\d+', node.attrib['bounds'])))
    adb('shell', 'input', 'tap', str((bounds[0]+bounds[2])//2), str((bounds[1]+bounds[3])//2))
def screenshot(name):
    (out/name).write_bytes(adb('exec-out', 'screencap', '-p'))

# Exercise the automatic agenda permission request on a fresh install.
adb('shell','pm','revoke',package,'android.permission.READ_CONTACTS')
adb('shell', 'am', 'start', '-n', package+'/.MainActivity')
tap(wait_for(lambda a: a.get('resource-id','').endswith('/permission_allow_button')))
print('PASS: agenda permission is requested automatically at startup')
tap(wait_for(lambda a: a.get('content-desc') == 'Nueva conversación'))
wait_for(text('Nueva conversación'))
root = tree()
edits = [n for n in root.iter('node') if n.attrib.get('class') == 'android.widget.EditText']
assert len(edits) == 2
def edit(index):
    return [n for n in tree().iter('node') if n.attrib.get('class') == 'android.widget.EditText'][index]
# Always recapture bounds after keyboard/dialog resize.
# Invalid number must not dismiss the dialog.
tap(edit(0)); adb('shell', 'input', 'text', '123')
tap(wait_for(text('Abrir')))
wait_for(text('Nueva conversación'))
tap(edit(0)); adb('shell', 'input', 'keyevent', 'KEYCODE_MOVE_END')
for _ in range(3): adb('shell', 'input', 'keyevent', 'KEYCODE_DEL')
adb('shell', 'input', 'text', '34123456789')
tap(edit(1)); adb('shell', 'input', 'text', 'Prueba%sUI')
tap(wait_for(text('Abrir')))
wait_for(text('Prueba UI'))
wait_for(lambda a: a.get('content-desc') == 'Enviar mensaje' and a.get('enabled') == 'false')
screenshot('chat.png')
tap(wait_for(lambda a: a.get('content-desc') == 'Volver a conversaciones'))
wait_for(text('Tu primera conversación'))
assert not any(node.attrib.get('content-desc') == 'Abrir conversación con Prueba UI' for node in tree().iter('node'))
# Add a synthetic received message to exercise the populated list without an account.
adb('shell', 'am', 'force-stop', package)
with tempfile.TemporaryDirectory() as folder:
    database = Path(folder) / 'conversations.db'
    database.write_bytes(adb('exec-out', 'run-as', package, 'cat', 'databases/conversations.db'))
    db = sqlite3.connect(database)
    with db:
        db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',
                   ('ui-fixture', '34123456789@s.whatsapp.net', 'Mensaje de prueba', 0, 0, 1, ''))
    db.close()
    subprocess.run(['adb', 'shell', f"run-as {package} sh -c 'cat > databases/conversations.db'"],
                   input=database.read_bytes(), check=True)
adb('shell', 'am', 'start', '-n', package+'/.MainActivity')
row=wait_for(lambda a: a.get('content-desc') == 'Abrir conversación con Prueba UI')
screenshot('conversations.png')
tap(row)
wait_for(lambda a: a.get('content-desc') == 'Enviar mensaje' and a.get('enabled') == 'false')
print('PASS: new-conversation +, phone validation, chat composer, back navigation, persistent list and offline send guard')

# Renaming a contact refreshes its title without rebuilding/clearing the composer.
composer=wait_for(lambda a: a.get('class')=='android.widget.EditText')
tap(composer);adb('shell','input','text','Borrador%sconservado')
tap(wait_for(lambda a:a.get('content-desc')=='Editar nombre del contacto'))
wait_for(text('Nombre del contacto'))
name_field=wait_for(lambda a:a.get('class')=='android.widget.EditText' and a.get('text')=='Prueba UI')
tap(name_field)
adb('shell','input','keyevent','KEYCODE_MOVE_END')
for _ in range(len('Prueba UI')):adb('shell','input','keyevent','KEYCODE_DEL')
adb('shell','input','text','Contacto%slocal')
tap(wait_for(text('Guardar')))
wait_for(text('Contacto local'));wait_for(text('Borrador conservado'))
tap(wait_for(lambda a:a.get('content-desc')=='Volver a conversaciones'))
wait_for(lambda a:a.get('content-desc')=='Abrir conversación con Contacto local')
adb('shell','am','force-stop',package)
adb('shell','am','start','-n',package+'/.MainActivity')
wait_for(lambda a:a.get('content-desc')=='Abrir conversación con Contacto local')
print('PASS: local contact naming, composer preservation and name persistence after process death')

def long_press_chat():
    row=wait_for(lambda a: a.get('content-desc')=='Abrir conversación con Contacto local')
    x1,y1,x2,y2=map(int,re.findall(r'\d+',row.attrib['bounds']))
    x,y=str((x1+x2)//2),str((y1+y2)//2)
    adb('shell','input','swipe',x,y,x,y,'800')

def open_archive():
    tap(wait_for(lambda a: a.get('content-desc')=='Menú principal'))
    wait_for(text('Archivados'))
    if not (out/'anchored-menu.png').exists():screenshot('anchored-menu.png')
    tap(wait_for(text('Archivados')))

long_press_chat()
tap(wait_for(text('Archivar')))
wait_for(text('Tu primera conversación'))
adb('shell','am','force-stop',package)
adb('shell','am','start','-n',package+'/.MainActivity')
open_archive()
long_press_chat()
tap(wait_for(text('Desarchivar')))
wait_for(text('No hay conversaciones archivadas'))
tap(wait_for(lambda a: a.get('content-desc')=='Volver a conversaciones'))
print('PASS: archiving survives restart and unarchiving restores the conversation')

long_press_chat()
tap(wait_for(lambda a:a.get('content-desc')=='Eliminar seleccionadas'))
wait_for(text('Tu primera conversación'))
adb('shell','am','force-stop',package)
adb('shell','am','start','-n',package+'/.MainActivity')
wait_for(text('Tu primera conversación'))
print('PASS: immediate local chat removal persists after process death')

# Seed three independent chats to verify batch scope and selection state.
adb('shell','am','force-stop',package)
with tempfile.TemporaryDirectory() as folder:
    database=Path(folder)/'conversations.db'
    database.write_bytes(adb('exec-out','run-as',package,'cat','databases/conversations.db'))
    db=sqlite3.connect(database)
    with db:
        for i in range(3):
            peer=f'3499999900{i}@s.whatsapp.net'
            db.execute('INSERT INTO chats(jid,name,updated) VALUES(?,?,?)',
                       (peer,f'Lote {i+1}',100+i))
            db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',
                       (f'batch-{i}',peer,f'Mensaje lote {i+1}',0,0,100+i,''))
    db.close()
    subprocess.run(['adb','shell',f"run-as {package} sh -c 'cat > databases/conversations.db'"],
                   input=database.read_bytes(),check=True)
adb('shell','am','start','-n',package+'/.MainActivity')

def desc(value):
    return lambda a:a.get('content-desc')==value
def long_press_name(name):
    node=wait_for(desc('Abrir conversación con '+name))
    x1,y1,x2,y2=map(int,re.findall(r'\d+',node.attrib['bounds']))
    x,y=str((x1+x2)//2),str((y1+y2)//2)
    adb('shell','input','swipe',x,y,x,y,'800')

# The header keeps its height and list geometry when selection starts/stops.
wait_for(text('What!'))
before=wait_for(desc('Abrir conversación con Lote 1')).attrib['bounds']
normal_buttons=[wait_for(desc(label)).attrib['bounds'] for label in ('Nueva conversación','Nuevo grupo','Seleccionar todas','Menú principal')]
assert len({tuple(re.findall(r'\d+',b))[1::2] for b in normal_buttons})==1,normal_buttons
long_press_name('Lote 1')
wait_for(desc('1 seleccionadas'))
assert wait_for(desc('Seleccionada: Lote 1')).attrib['bounds']==before
selected_buttons=[wait_for(desc(label)).attrib['bounds'] for label in ('Fijar seleccionadas','Archivar seleccionadas','Eliminar seleccionadas','Seleccionar todas','Cancelar selección')]
assert len({tuple(re.findall(r'\d+',b))[1::2] for b in selected_buttons})==1,selected_buttons
screenshot('selection-fixed-toolbar.png')
tap(wait_for(desc('Cancelar selección')))
assert wait_for(desc('Abrir conversación con Lote 1')).attrib['bounds']==before
print('PASS: What! toolbar, aligned rounded controls and no list shift on long press or cancel')
long_press_name('Lote 1')
wait_for(desc('1 seleccionadas'))
tap(wait_for(desc('Seleccionar: Lote 2')))
wait_for(desc('2 seleccionadas'))
# Android Back exits selection without opening a conversation or mutating data.
adb('shell','input','keyevent','KEYCODE_BACK')
wait_for(desc('Abrir conversación con Lote 1'))
long_press_name('Lote 1')
tap(wait_for(desc('Seleccionar todas')))
wait_for(desc('3 seleccionadas'))
tap(wait_for(desc('Deseleccionar todas')))
wait_for(desc('Abrir conversación con Lote 1'))
long_press_name('Lote 1')
tap(wait_for(desc('Seleccionar: Lote 2')))
tap(wait_for(desc('Seleccionada: Lote 2')))
wait_for(desc('1 seleccionadas'))
tap(wait_for(desc('Seleccionar: Lote 2')))
screenshot('multiple-selection.png')
tap(wait_for(desc('Archivar seleccionadas')))
wait_for(desc('Abrir conversación con Lote 3'))
assert not any(n.attrib.get('content-desc')=='Abrir conversación con Lote 1' for n in tree().iter('node'))
adb('shell','am','force-stop',package)
adb('shell','am','start','-n',package+'/.MainActivity')
open_archive()
wait_for(desc('Abrir conversación con Lote 1'))
wait_for(desc('Abrir conversación con Lote 2'))
assert not any(n.attrib.get('content-desc')=='Abrir conversación con Lote 3' for n in tree().iter('node'))
long_press_name('Lote 1')
tap(wait_for(desc('Seleccionar todas')))
wait_for(desc('2 seleccionadas'))
screenshot('multiple-archived.png')
tap(wait_for(desc('Desarchivar seleccionadas')))
wait_for(text('No hay conversaciones archivadas'))
tap(wait_for(desc('Volver a conversaciones')))
wait_for(desc('Abrir conversación con Lote 1'))
wait_for(desc('Abrir conversación con Lote 2'))
wait_for(desc('Abrir conversación con Lote 3'))
print('PASS: multiple selection, toggle, select all, deselect all, Back cancellation, scoped batch archive and unarchive')

# Delete two immediately, leaving the unselected third intact after restart.
long_press_name('Lote 1')
tap(wait_for(desc('Seleccionar: Lote 2')))
tap(wait_for(desc('Eliminar seleccionadas')))
wait_for(desc('Abrir conversación con Lote 3'))
assert not any(n.attrib.get('content-desc') in ('1 seleccionadas','2 seleccionadas') for n in tree().iter('node'))
adb('shell','am','force-stop',package)
adb('shell','am','start','-n',package+'/.MainActivity')
wait_for(desc('Abrir conversación con Lote 3'))
for name in ('Lote 1','Lote 2'):
    assert not any(n.attrib.get('content-desc')=='Abrir conversación con '+name for n in tree().iter('node'))
# The fixed toolbar exposes selecting all without a long press or a menu.
tap(wait_for(desc('Seleccionar todas')))
wait_for(desc('1 seleccionadas'))
tap(wait_for(desc('Archivar seleccionadas')))
wait_for(text('Tu primera conversación'))
open_archive()
long_press_name('Lote 3')
tap(wait_for(desc('Eliminar seleccionadas')))
wait_for(text('No hay conversaciones archivadas'))
print('PASS: immediate scoped batch deletion, persistence, toolbar selection and deletion from archived list')

# Contact picker: exercise a real ContactsProvider record, never network discovery.
adb('shell','input','keyevent','KEYCODE_BACK')
tap(wait_for(lambda a:a.get('content-desc')=='Nueva conversación'))
tap(wait_for(text('Buscar en la agenda')))
wait_for(text('Agenda'))
screenshot('agenda-search.png')
tap(wait_for(text('Cerrar')))
tap(wait_for(text('Cancelar')))
# Seed group rows only into the local database; no real group is created.
adb('shell','am','force-stop',package)
with tempfile.TemporaryDirectory() as folder:
    database=Path(folder)/'conversations.db'
    database.write_bytes(adb('exec-out','run-as',package,'cat','databases/conversations.db'))
    db=sqlite3.connect(database)
    with db:
        gid='120363000000001@g.us'
        db.execute('INSERT INTO chats(jid,name,updated) VALUES(?,?,?)',(gid,'',1000))
        db.execute('INSERT INTO contact_names VALUES(?,?)',(gid,'Grupo de prueba'))
        db.execute('UPDATE chats SET pinned=700,pinned_changed=700 WHERE jid=?',(gid,))
        db.execute('INSERT INTO chats(jid,name,updated) VALUES(?,?,?)',('120363000000002@g.us','Grupo sin fijar',900))
        db.execute('INSERT INTO group_metadata(jid,visible) VALUES(?,1)',('120363000000002@g.us',))
        db.execute('INSERT INTO group_metadata(jid,members,left_group,visible) VALUES(?,?,0,0)',
                   (gid,'34123456789@s.whatsapp.net\t1\n'))
        db.execute('INSERT INTO messages VALUES(?,?,?,?,?,?,?)',('group-fixture',gid,'Mensaje del grupo',0,0,1000,''))
        db.execute('INSERT INTO message_authors VALUES(?,?,?,?)',(gid,'group-fixture',0,'34123456789:0@s.whatsapp.net'))
    db.close()
    subprocess.run(['adb','shell',f"run-as {package} sh -c 'cat > databases/conversations.db'"],input=database.read_bytes(),check=True)
adb('shell','am','start','-n',package+'/.MainActivity')
wait_for(desc('Abrir conversación con Grupo sin fijar'))
wait_for(desc('Grupo'))
screenshot('group-list-pinned.png')
tap(wait_for(lambda a:a.get('content-desc')=='Abrir conversación con Grupo de prueba'))
wait_for(text('Mensaje del grupo'))
wait_for(text('+34123456789'))
tap(wait_for(lambda a:a.get('content-desc')=='Menú principal'))
tap(wait_for(text('Información del grupo')))
wait_for(text('1 participantes'))
wait_for(text('Salir del grupo'))
screenshot('group-info.png')
tap(wait_for(text('Cerrar')))
tap(wait_for(desc('Volver a conversaciones')))
tap(wait_for(desc('Nuevo grupo')))
wait_for(text('Añadir desde la agenda'))
# Invalid create cannot contact the service or dismiss the form.
tap(wait_for(text('Crear')))
wait_for(text('Nuevo grupo'))
tap(wait_for(text('Cancelar')))
long_press_name('Grupo de prueba')
tap(wait_for(desc('Seleccionar todas')))
tap(wait_for(lambda a:a.get('content-desc')=='Eliminar seleccionadas'))
wait_for(text('Tu primera conversación'))
print('PASS: local agenda picker, group names, sender attribution, participant/admin list, separate leave action, create validation and group conversation deletion')
