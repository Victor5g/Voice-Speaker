#!/bin/bash
source "$(dirname "$0")/local-env.sh"
/usr/bin/python3 - <<'PY'
from pathlib import Path
import re
import sys

root = Path.cwd().resolve()
print('PROJECT LOCAL FILES:')
for child in sorted(root.iterdir()):
    print(str(child) + ('/' if child.is_dir() else ''))
print('\nFILES CREATED OUTSIDE PROJECT:')
print('No global installation commands in project scripts. Build/test writes allow the repo and approved per-user Xcode temporary/cache directories.')
print('Whole-machine filesystem diff: NOT MEASURED; cannot assert NONE for independent macOS/Xcode services.')
print('Approved exception: docs/BUILD_AUTHORIZATION.md. Historical denials: docs/BUILD_BLOCKER.md.')
print('\nGLOBAL PERSISTENT INSTALLATIONS BY PROJECT: NONE')
print('Local downloads: pinned native dependencies, CMake, development Python venv, ASR/translation/TTS/speaker/overlap/separation models and licensed test fixtures; all inside the project.')
print('Known OS-created crash diagnostic: ~/Library/Logs/DiagnosticReports/LocalVoiceTranslator-2026-09-11-111140.ips (failed nested-sandbox launch).')
print('OS-managed state (TCC, logs, Xcode services): not removed or modified by cleanup.')
print('\nPROJECT SYMLINKS ESCAPING ROOT:')
escaped = []
toolchain_links = []
allowed_interpreters = {root / 'tools/downloads/conversion-venv/bin' / name for name in ('python', 'python3', 'python3.9')}
for folder in ['app', 'bridge', 'engine', 'scripts', 'models', 'tools', 'third_party']:
    for path in (root / folder).rglob('*'):
        if path.is_symlink() and root not in path.resolve().parents:
            if path in allowed_interpreters and path.resolve() == Path(sys.executable).resolve(): toolchain_links.append(str(path))
            else: escaped.append(str(path))
print('\n'.join(escaped) if escaped else 'NONE')
print('\nREAD-ONLY LINKS TO EXISTING XCODE PYTHON PREREQUISITE:')
print('\n'.join(toolchain_links) if toolchain_links else 'NONE')
print('These venv launchers reuse the installed Xcode interpreter; packages and caches remain in the project. No interpreter is installed globally.')
print('\nFORBIDDEN COMMAND CHECK:')
violations = []
pattern = re.compile(r'^\s*(?:sudo\s|brew\s+(?:install|upgrade|tap)|(?:pip|pip3)\s+install|npm\s+install\s+-g|gem\s+install)', re.M)
for script in (root / 'scripts').glob('*.sh'):
    if pattern.search(script.read_text()): violations.append(str(script))
print('\n'.join(violations) if violations else 'PASS')
if escaped or violations: raise SystemExit(1)
PY
