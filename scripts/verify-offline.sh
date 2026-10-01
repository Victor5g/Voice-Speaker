#!/bin/bash
source "$(dirname "$0")/local-env.sh"
APP="${1:-$ROOT/artifacts/LocalVoiceTranslator.app}"
BIN="$APP/Contents/MacOS/LocalVoiceTranslator"
test -f "$BIN" || { echo 'FAIL: app binary missing; build has not passed.'; exit 2; }
export LVT_AUDIT_APP="$APP"
/usr/bin/python3 - <<'PY'
import os
import pathlib
import plistlib
import re
import subprocess

app = pathlib.Path(os.environ['LVT_AUDIT_APP']).resolve()
root = pathlib.Path.cwd().resolve()
if root not in app.parents:
    raise SystemExit('ABORT: artifact outside project')
binary = app / 'Contents/MacOS/LocalVoiceTranslator'
links = subprocess.check_output(['/usr/bin/otool', '-L', str(binary)], text=True)
symbols = subprocess.check_output(['/usr/bin/nm', '-u', str(binary)], text=True)
strings = subprocess.check_output(['/usr/bin/strings', str(binary)], text=True)
entitlements_raw = subprocess.check_output(['/usr/bin/codesign', '-d', '--entitlements', ':-', str(app)], stderr=subprocess.DEVNULL)
entitlements = plistlib.loads(entitlements_raw)
network_entitlements = [key for key, value in entitlements.items() if 'network' in key and value]
network_symbols = [line for line in symbols.splitlines() if re.search(r'\b_(socket|connect|getaddrinfo|sendto|curl_\w+)\b|NSURLSession|CFHTTP|CFStreamCreatePairWithSocket', line)]
urls = sorted(set(re.findall(r'(?:https?|wss?)://[^\s<>"\x00]+', strings)))
for source in sorted((root / 'app').rglob('*.swift')):
    if re.search(r'URLSession|NWConnection|import Network|WKWebView', source.read_text()):
        network_symbols.append(str(source.relative_to(root)))
print('LINKED FRAMEWORKS:\n' + links)
print('App Sandbox:', entitlements.get('com.apple.security.app-sandbox', False))
print('Audio input:', entitlements.get('com.apple.security.device.audio-input', False))
print('Network entitlements:', network_entitlements or 'NONE')
print('Direct network symbols/source APIs:', network_symbols or 'NONE')
print('Binary URL strings:', urls or 'NONE')
print('Runtime network calls: NOT MEASURED')
print('Physical network-disconnected test: NOT RUN; global network settings were not changed')
print('Static audit is not evidence that a complete offline translator works.')
if network_entitlements or network_symbols or not entitlements.get('com.apple.security.app-sandbox'):
    raise SystemExit(1)
PY
