"""Exercise the APK UI on a clean emulator; never send or link an account."""
from pathlib import Path
import re
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
    raise AssertionError('Expected UI element did not appear')
def text(value):
    return lambda a: a.get('text', '').casefold() == value.casefold()
def tap(node):
    bounds = list(map(int, re.findall(r'\d+', node.attrib['bounds'])))
    adb('shell', 'input', 'tap', str((bounds[0]+bounds[2])//2), str((bounds[1]+bounds[3])//2))
def screenshot(name):
    (out/name).write_bytes(adb('exec-out', 'screencap', '-p'))

adb('shell', 'am', 'start', '-n', package+'/.MainActivity')
tap(wait_for(lambda a: a.get('content-desc') == 'Nueva conversación'))
wait_for(text('Nueva conversación'))
root = tree()
edits = [n for n in root.iter('node') if n.attrib.get('class') == 'android.widget.EditText']
assert len(edits) == 2
# Invalid number must not dismiss the dialog.
tap(edits[0]); adb('shell', 'input', 'text', '123')
tap(wait_for(text('Abrir')))
wait_for(text('Nueva conversación'))
tap(edits[0]); adb('shell', 'input', 'keyevent', 'KEYCODE_MOVE_END')
for _ in range(3): adb('shell', 'input', 'keyevent', 'KEYCODE_DEL')
adb('shell', 'input', 'text', '34123456789')
tap(edits[1]); adb('shell', 'input', 'text', 'Prueba%sUI')
tap(wait_for(text('Abrir')))
wait_for(text('Prueba UI'))
wait_for(lambda a: a.get('content-desc') == 'Enviar mensaje' and a.get('enabled') == 'false')
screenshot('chat.png')
tap(wait_for(lambda a: a.get('content-desc') == 'Volver a conversaciones'))
wait_for(text('Prueba UI'))
screenshot('conversations.png')
# SQLite conversation list survives process death.
adb('shell', 'am', 'force-stop', package)
adb('shell', 'am', 'start', '-n', package+'/.MainActivity')
tap(wait_for(lambda a: a.get('content-desc') == 'Abrir conversación con Prueba UI'))
wait_for(lambda a: a.get('content-desc') == 'Enviar mensaje' and a.get('enabled') == 'false')
print('PASS: new-conversation +, phone validation, chat composer, back navigation, persistent list and offline send guard')
