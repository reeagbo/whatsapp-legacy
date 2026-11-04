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
tap(wait_for(text('Eliminar conversación')))
wait_for(text('Tu primera conversación'))
adb('shell','am','force-stop',package)
adb('shell','am','start','-n',package+'/.MainActivity')
wait_for(text('Tu primera conversación'))
print('PASS: immediate local chat removal persists after process death')
