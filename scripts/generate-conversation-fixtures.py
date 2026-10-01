#!/usr/bin/python3
"""Deterministic synthetic scenarios. Human recordings have a separate provenance manifest."""
from pathlib import Path
import wave,array,math,json,random,hashlib
root=Path(__file__).resolve().parent.parent
base=root/'tests/data/scenarios';base.mkdir(parents=True,exist_ok=True)
(root/'build').mkdir(exist_ok=True)
def read(path):
    with wave.open(str(root/path)) as stream: return [v/32768 for v in array.array('h',stream.readframes(stream.getnframes()))]
def write(name,values):
    path=base/(name+'.wav')
    with wave.open(str(path),'wb') as stream:
        stream.setnchannels(1);stream.setsampwidth(2);stream.setframerate(16000)
        stream.writeframes(array.array('h',[int(max(-1,min(.999969,v))*32768) for v in values]).tobytes())
    return dict(filename=str(path.relative_to(root)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),expectedSize=path.stat().st_size)
pt=read('tests/data/generated/ptbr.wav');us=read('tests/data/generated/enus.wav');gb=read('tests/data/generated/engb.wav');silence=[0.]*32000
# Restore build-only configurations after clean-all; fixture sources stay separate.
golden=root/'tests/data/conversation_ptbr_en.wav'
if not golden.exists():
    combined=write('conversation-restore',pt+silence+us+silence)
    golden.write_bytes((root/combined['filename']).read_bytes())
    (root/'tests/data/conversation_ptbr_en.provenance.json').write_text(json.dumps(dict(type='synthetic development speech',sources=['tests/data/generated/ptbr.wav','tests/data/generated/enus.wav'],sha256=hashlib.sha256(golden.read_bytes()).hexdigest()),indent=2)+'\n')
human=root/'tests/data/human/conversation_ptbr_en.wav'
if human.exists():
    (root/'build/human-golden-config.json').write_text(json.dumps(dict(fixture=str(human.relative_to(root)),output='artifacts/human-conversation-translated.wav',fixtureType='Human FLEURS recordings, CC-BY-4.0; no physical microphone/speaker validation'),indent=2)+'\n')
(root/'build/echo-config.json').write_text(json.dumps(dict(fixture='tests/data/generated/ptbr.wav',output='artifacts/digital-echo-translated.wav',injectEcho=True,expectedLanguages=['pt'],minimumPlayed=1,requiredOriginalTerms={'pt':['reunião','documentos','conversar','projeto']},fixtureType='exact digital 60 ms delayed playback; physical AEC separate'),indent=2)+'\n')
fixtures={}
fixtures['three-speakers-return']=write('three-speakers-return',pt+silence+us+silence+gb+silence*3+pt+silence)
fixtures['english-accents']=write('english-accents',us+silence+gb+silence)
count=min(len(us),len(gb),64000)
fixtures['overlap-two-english']=write('overlap-two-english',silence+[.5*(us[i]+gb[i]) for i in range(count)]+silence)
fixtures['silence']=write('silence',[0.]*160000)
rng=random.Random(914)
fixtures['noise-only']=write('noise-only',[rng.gauss(0,.008) for _ in range(160000)])
fixtures['speech-noise']=write('speech-noise',[max(-1,min(.9999,v+rng.gauss(0,.01))) for v in pt+silence+us+silence])
values=[]
for frequency in [261.63,329.63,392.,349.23]:
    for i in range(32000):
        envelope=min(1,i/800)*min(1,(32000-i)/1600)
        values.append(.025*envelope*(math.sin(2*math.pi*frequency*i/16000)+.3*math.sin(4*math.pi*frequency*i/16000)))
fixtures['instrumental-tone']=write('instrumental-tone',values+silence)
for name,entry in fixtures.items():
    config=dict(fixture=entry['filename'],output='artifacts/scenario-'+name+'.wav',fixtureType='synthetic development scenario: '+name)
    if name in ('silence','noise-only','instrumental-tone'):config.update(expectNoSpeech=True)
    if name=='silence':config.update(requireNoVadSegments=True)
    if name=='english-accents':config.update(expectedLanguages=['en'],minimumSpeakers=2)
    if name=='three-speakers-return':config.update(minimumSpeakers=3)
    if name=='overlap-two-english':config.update(expectedLanguages=['en'],minimumSpeakers=2,minimumOverlapWindows=1,minimumSeparationCalls=1)
    (root/('build/scenario-'+name+'.json')).write_text(json.dumps(config,indent=2)+'\n')
(root/'tests/data/scenarios/manifest.json').write_text(json.dumps(dict(type='synthetic development fixtures; no human quality claim',seed=914,fixtures=fixtures),indent=2)+'\n')
