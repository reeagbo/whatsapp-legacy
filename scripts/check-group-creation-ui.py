"""Run synthetic group-result UI regression checks on the existing emulator."""
from pathlib import Path
import subprocess

result = subprocess.run([
    'adb', 'shell', 'am', 'instrument', '-w',
    'org.reeagbo.whatsapplegacy.test/org.reeagbo.whatsapplegacy.GroupCreationInstrumentation',
], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
print(result.stdout)
Path('ui-smoke/group-creation-instrumentation.txt').write_text(result.stdout)
assert 'PASS: group creation confirmation opens its chat' in result.stdout, result.stdout
