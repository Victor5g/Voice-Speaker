#!/usr/bin/python3
"""Build an optional personal-use HAL installer. Never install or restart anything."""
from pathlib import Path
import hashlib, json, plistlib, shutil, subprocess, tarfile, uuid

root = Path(__file__).resolve().parent.parent
source = root / 'third_party/blackhole'
commit = 'e2b22aaaba4e507a097131704bf96dabc004d9cf'
assert subprocess.check_output(['/usr/bin/git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() == commit
assert not subprocess.check_output(['/usr/bin/git', '-C', str(source), 'status', '--porcelain'], text=True).strip(), 'Upstream source changed'
stage = root / 'build/call-audio-driver'
payload = stage / 'payload'
payload.mkdir(parents=True, exist_ok=True)
devices = []
for role, suffix in [('Microphone', 'Microphone'), ('Speakers', 'Speakers')]:
    product = 'LocalVoiceTranslator' + role
    identifier = 'local.localvoicetranslator.audio.' + role.lower()
    bundle = payload / (product + '.driver')
    macos = bundle / 'Contents/MacOS'
    resources = bundle / 'Contents/Resources'
    macos.mkdir(parents=True, exist_ok=True); resources.mkdir(parents=True, exist_ok=True)
    definitions = [f'-DkDriver_Name="{product}"', f'-DkDevice_Name="LocalVoiceTranslator {suffix}"',
        f'-DkPlugIn_BundleID="{identifier}"', '-DkHas_Driver_Name_Format=false',
        '-DkNumber_Of_Channels=2', '-DDEBUG=0']
    subprocess.run(['/usr/bin/xcrun', 'clang', '-O2', '-arch', 'arm64', '-mmacosx-version-min=14.0',
        '-bundle', *definitions, str(source / 'BlackHole/BlackHole.c'), '-framework', 'CoreAudio',
        '-framework', 'CoreFoundation', '-framework', 'Accelerate', '-o', str(macos / product)], check=True)
    info = plistlib.loads((source / 'BlackHole/BlackHole.plist').read_bytes())
    factory = str(uuid.uuid5(uuid.NAMESPACE_DNS, identifier)).upper()
    info.update(CFBundleExecutable=product, CFBundleIdentifier=identifier, CFBundleName=product,
        CFBundleShortVersionString='0.7.1', CFBundleVersion='1',
        CFPlugInFactories={factory:'BlackHole_Create'},
        CFPlugInTypes={'443ABAB8-E7B3-491A-B985-BEB9187030DB':[factory]})
    (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    shutil.copyfile(source / 'LICENSE', resources / 'BlackHole-GPL-3.0.txt')
    shutil.copyfile(source / 'BlackHole/BlackHole.icns', resources / 'BlackHole.icns')
    subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', str(bundle)], check=True)
    subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(bundle)], check=True)
    devices.append(dict(name=f'LocalVoiceTranslator {suffix}', uid=product+'_UID', bundle=bundle.name,
        bundleID=identifier, factoryUUID=factory, binarySHA256=hashlib.sha256((macos/product).read_bytes()).hexdigest()))
component = stage / 'LocalVoiceTranslator-Audio-component.pkg'
subprocess.run(['/usr/bin/pkgbuild', '--root', str(payload), '--identifier', 'local.localvoicetranslator.audio',
    '--version', '0.7.1.1', '--install-location', '/Library/Audio/Plug-Ins/HAL', str(component)], check=True)
distribution = stage / 'distribution.xml'
distribution.write_text('''<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
<title>LocalVoiceTranslator Audio</title>
<options customize="never" require-scripts="false" hostArchitectures="arm64"/>
<domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
<choices-outline><line choice="audio"/></choices-outline>
<choice id="audio" visible="false"><pkg-ref id="local.localvoicetranslator.audio"/></choice>
<pkg-ref id="local.localvoicetranslator.audio" version="0.7.1.1" onConclusion="RequireRestart">LocalVoiceTranslator-Audio-component.pkg</pkg-ref>
</installer-gui-script>
''')
artifact = root / 'artifacts/LocalVoiceTranslator-Audio.pkg'
subprocess.run(['/usr/bin/productbuild', '--distribution', str(distribution), '--package-path', str(stage), str(artifact)], check=True)
# Corresponding source and exact build recipe accompany this separate GPL driver.
with tarfile.open(root / 'artifacts/LocalVoiceTranslator-Audio-source.tar.gz', 'w:gz') as archive:
    for name in ('LICENSE', 'BlackHole/BlackHole.c', 'BlackHole/BlackHole.plist', 'BlackHole/BlackHole.icns', 'README.md'):
        archive.add(source/name, arcname='blackhole/'+name)
    archive.add(Path(__file__), arcname='build-call-audio-driver.py')
manifest = dict(upstream='https://github.com/ExistentialAudio/BlackHole', commit=commit, version='0.7.1',
    license='GPL-3.0', scope='Separate optional personal-use HAL plugins; never linked into translator',
    installed=False, runtimeValidation='PENDING: install, restart, enumerate and test real call',
    signing='Driver bundles ad-hoc; installer unsigned, not notarized',
    installLocation='/Library/Audio/Plug-Ins/HAL', restartRequired=True, installerScripts=False,
    devices=devices, packageSHA256=hashlib.sha256(artifact.read_bytes()).hexdigest())
(root / 'artifacts/call-audio-driver.json').write_text(json.dumps(manifest, indent=2)+'\n')
print('PASS built optional installer and corresponding source; nothing installed.')
