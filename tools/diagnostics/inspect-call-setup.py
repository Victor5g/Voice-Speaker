"""Capture actual first-use SwiftUI button presses. No microphone/meeting started."""
from pathlib import Path
import json, subprocess, time
root = Path(__file__).resolve().parents[2]
folder = root / 'artifacts/call-setup-review'
folder.mkdir(exist_ok=True)
out, err = folder / 'ui.stdout.log', folder / 'ui.stderr.log'
out.write_text(''); err.write_text('')
app = root / 'build/DerivedData/Build/Products/Debug/LocalVoiceTranslator.app'
process = subprocess.Popen(['/usr/bin/open', '-n', '-W', str(app), '--stdout', str(out), '--stderr', str(err), '--args', '--ui-diagnostic', '--show-audio', '--call-setup-ui-diagnostic'])
seen = set()
start = time.monotonic()
while process.poll() is None and time.monotonic() - start < 160:
    for line in out.read_text().splitlines():
        if not line.startswith('LVT_DIAGNOSTIC '): continue
        event = json.loads(line.split(' ', 1)[1])
        if event.get('event') == 'setup_stage' and event['stage'] not in seen:
            subprocess.run(['/usr/sbin/screencapture', '-x', '-l', str(event['windowNumber']), str(folder / (event['stage'] + '.png'))], check=True, timeout=10)
            seen.add(event['stage'])
    time.sleep(.05)
events = [json.loads(line.split(' ', 1)[1]) for line in out.read_text().splitlines() if line.startswith('LVT_DIAGNOSTIC ')]
success = process.poll() == 0 and any(e.get('event') == 'setup_result' and e.get('result') == 'PASS' for e in events)
result = dict(result='PASS' if success else 'FAIL', events=events, capturedStages=sorted(seen), visualInspection='PENDING')
(folder / 'ui.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
raise SystemExit(0 if success else 1)
