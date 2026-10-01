#!/bin/bash
source "$(dirname "$0")/local-env.sh"
MODE="${1:---ui-diagnostic}"
case "$MODE" in --ui-diagnostic|--audio-diagnostic) ;; *) echo 'Expected --ui-diagnostic or --audio-diagnostic'; exit 2;; esac
APP="$ROOT/build/DerivedData/Build/Products/Debug/LocalVoiceTranslator.app"
test -x "$APP/Contents/MacOS/LocalVoiceTranslator" || { echo 'Build Debug first'; exit 2; }
# LaunchServices initializes the app's own App Sandbox. Do not stack the toolchain
# write sandbox over macOS secinit; a nested launch was observed to terminate in secinit.
/usr/bin/python3 - "$APP" "$MODE" <<'PY'
import json
from pathlib import Path
import subprocess
import sys
import time

app, mode = sys.argv[1:]
name = mode.removeprefix('--')
root = Path.cwd()
events = []
stdout_path = root / 'artifacts' / (name + '.stdout.log')
stderr_path = root / 'artifacts' / (name + '.stderr.log')
stdout_path.write_text('')
stderr_path.write_text('')
with stderr_path.open('a') as err:
    process = subprocess.Popen(['/usr/bin/open', '-n', '-W', app, '--stdout', str(stdout_path), '--stderr', str(stderr_path), '--args', mode], stdout=subprocess.PIPE, stderr=err, text=True)
    try:
        # Capture only the app's window, using the compositor rather than a partial
        # NSView bitmap. Failure to obtain a screenshot does not imply visual QA passed.
        for _ in range(100):
            lines = stdout_path.read_text().splitlines()
            window = next((json.loads(l.split(' ', 1)[1]) for l in lines if l.startswith('LVT_DIAGNOSTIC ') and '"windowNumber"' in l), None)
            if window:
                if window.get('windowNumber', 0) > 0:
                    shot = subprocess.run(['/usr/sbin/screencapture', '-x', '-l', str(window['windowNumber']), str(root / 'artifacts' / (name + '-window.png'))], capture_output=True, timeout=10)
                    print('Window screenshot: ' + ('PASS' if shot.returncode == 0 else 'NOT VERIFIED'), flush=True)
                break
            if process.poll() is not None: break
            time.sleep(0.05)
        process.communicate(timeout=180)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.communicate(timeout=10)
        print('FAIL: diagnostic timed out, possibly waiting for microphone consent')
        events.append({'event': 'timeout'})
    for line in stdout_path.read_text().splitlines():
        if line.startswith('LVT_DIAGNOSTIC '):
            event = json.loads(line.split(' ', 1)[1])
            events.append(event)
            print(json.dumps(event), flush=True)
    audio_event = next((e for e in events if e.get('event') == 'audio'), {})
    passed = any(e.get('event') == 'window' and e.get('visible') for e in events)
    if mode == '--audio-diagnostic':
        passed = passed and audio_event.get('captureContinuedDuringPlayback') and audio_event.get('outputCallbacksAdvanced') and audio_event.get('peakRenderedRMS', 0) > 0 and any(e.get('event') == 'stop' and e.get('cleared') for e in events)
        asr_event = next((e for e in events if e.get('event') == 'asr_model'), {})
        passed = passed and asr_event.get('state') == 2 and asr_event.get('metalActive') == 1 and asr_event.get('running') is True
        passed = passed and any(e.get('event') == 'auxiliary_window_close' and e.get('keptSession') for e in events)
    result = 0 if process.returncode == 0 and passed else 2
    (root / 'artifacts' / (name + '.json')).write_text(json.dumps({'exitCode': result, 'launcherExitCode': process.returncode, 'events': events}, indent=2) + '\n')
    raise SystemExit(result)
PY
