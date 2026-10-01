from pathlib import Path
import json,subprocess,time
root=Path.cwd();app=root/'build/DerivedData/Build/Products/Debug/LocalVoiceTranslator.app'
for name,flag in [('audio-routing','--show-audio'),('settings','--show-settings')]:
 out=root/f'artifacts/{name}-ui.stdout.log';err=root/f'artifacts/{name}-ui.stderr.log';out.write_text('');err.write_text('')
 process=subprocess.Popen(['/usr/bin/open','-n','-W',str(app),'--stdout',str(out),'--stderr',str(err),'--args','--ui-diagnostic',flag])
 window=None;windows=[]
 for _ in range(200):
  for line in out.read_text().splitlines():
   if line.startswith('LVT_DIAGNOSTIC '):
    event=json.loads(line.split(' ',1)[1])
    if event.get('event')=='window':window=event
    if event.get('event')=='visible_windows':windows=event['windows']
  if window or process.poll() is not None:break
  time.sleep(.05)
 if not window or not window.get('visible'):raise SystemExit(f'FAIL actual {name} window not visible')
 subprocess.run(['/usr/sbin/screencapture','-x','-l',str(window['windowNumber']),str(root/f'artifacts/{name}-ui-window.png')],check=True,timeout=10)
 code=process.wait(timeout=30)
 result=dict(result='CAPTURED' if code==0 else 'FAIL',window=window,windows=windows,scope='Actual SwiftUI window; requires visual inspection; no hardware or driver validation')
 (root/f'artifacts/{name}-ui-verification.json').write_text(json.dumps(result,indent=2)+'\n')
 print(name,json.dumps(result))
 if code:raise SystemExit(code)
