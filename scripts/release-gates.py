#!/usr/bin/python3
"""A personal package requires current automated evidence. Manual acceptance is separate."""
from pathlib import Path
import hashlib,json,sys,datetime
from acceptance_evidence import require_stable_conversation
root=Path(__file__).resolve().parent.parent
report=root/'artifacts/automated-acceptance.json'
def fingerprint():
    files=[p for folder in ('app','bridge','engine','scripts','tests/cpp','tests/swift','tests/data') for p in (root/folder).rglob('*') if p.is_file()]
    files += [root/p for p in ('models/manifest.json','models/selection.json','models/translation-selection.json','third_party/dependencies.lock.json')]
    h=hashlib.sha256()
    for path in sorted(files): h.update(str(path.relative_to(root)).encode());h.update(path.read_bytes())
    return h.hexdigest()
def engine_fingerprint():
    h=hashlib.sha256()
    for path in sorted(p for folder in ('engine','bridge') for p in (root/folder).rglob('*') if p.is_file()):
        h.update(str(path.relative_to(root)).encode());h.update(path.read_bytes())
    return h.hexdigest()
def last_json(path):
    rows=[json.loads(line) for line in path.read_text().splitlines() if line.startswith('{')]
    return rows[-1] if rows else {}
def soak_gate():
    path=root/'artifacts/soak-30min.jsonl'
    if not path.is_file(): return 'PENDING'
    try: result=require_stable_conversation(path)
    except (json.JSONDecodeError, ValueError): return 'FAIL'
    if result.get('result')!='PASS': return 'PENDING'
    provenance=json.loads((root/'artifacts/soak-provenance.json').read_text())
    if provenance['engineFingerprint']!=engine_fingerprint(): return 'STALE'
    if result.get('durationSeconds',0)<1800 or result.get('firstAudioSamples',0)<30 or result.get('peakRSSBytes',2**64)>8*1024**3:
        return 'FAIL'
    return 'PASS'
def call_soak_gate():
    path=root/'artifacts/call-soak-30min.jsonl'
    if not path.is_file(): return 'PENDING'
    try: result=require_stable_conversation(path)
    except (json.JSONDecodeError, ValueError): return 'FAIL'
    provenance=json.loads((root/'artifacts/call-soak-provenance.json').read_text())
    if provenance['engineFingerprint']!=engine_fingerprint(): return 'STALE'
    error=root/'artifacts/call-soak-30min.err'
    if error.is_file() and 'FAIL ' in error.read_text(): return 'FAIL'
    if result.get('result')!='PASS': return 'PENDING'
    if result.get('durationSeconds',0)<1800 or min(result.get('outgoingFirstAudioSamples',0),result.get('incomingFirstAudioSamples',0))<30 or result.get('peakFootprintBytes',2**64)>8*1024**3:
        return 'FAIL'
    return 'PASS'
def validate_evidence():
    checks={}
    for name, count in [('latency-timing-tests', 21), ('voice-cache-tests', 10), ('streaming-policy-tests', 24), ('inference-arbiter-tests', 18), ('boundary-evidence-tests', 33), ('corpus-drain-tracker-tests', 13), ('asr-controls-tests', 56), ('processing-options-tests',57)]:
        if f'RESULT {count} passed, 0 failed' not in (root/'artifacts'/f'{name}.log').read_text():
            raise SystemExit('FAIL latency regression: '+name)
        checks[name]='PASS'
    improvements=root/'artifacts/realtime-improvements'
    if 'PASS 37 glossary rules/alignment checks' not in (improvements/'glossary-tests.log').read_text():
        raise SystemExit('FAIL glossary rule regressions')
    glossary=[json.loads(line) for line in (improvements/'glossary-native.jsonl').read_text().splitlines() if line.startswith('{')]
    if len(glossary)<19 or any(not row.get('pass') for row in glossary):
        raise SystemExit('FAIL native glossary translation regressions')
    checks['glossary']='PASS'
    protections=json.loads((improvements/'audio-protection-results.json').read_text())
    expected={name+'-'+mode for name in ('silence','noise-only','instrumental-tone','digital-echo') for mode in ('auto','manual')}
    if {row['case'] for row in protections}!=expected or any(row['exitCode'] for row in protections):
        raise SystemExit('FAIL silence/noise/tone/echo routes')
    checks['audio-protections']='PASS'
    for name in ('asr-controls-native','processing-routes-native','playback-backlog-native','glossary-voice-native'):
        if last_json(improvements/(name+'.jsonl')).get('result')!='PASS':
            raise SystemExit('FAIL native processing regression: '+name)
        checks[name]='PASS'
    for name,path,needle in [('cpp','cpp-tests.log','RESULT 60 passed, 0 failed'),('swift','swift-tests.log','** TEST SUCCEEDED **'),('conversation-units','conversation-unit-tests.log','RESULT 48 passed, 0 failed'),('asr','asr-pipeline-tests.log','RESULT 5 passed, 0 failed'),('cleanup','cleanup-tests.log','OK')]:
        content=(root/'artifacts'/path).read_text()
        if needle not in content: raise SystemExit('FAIL missing successful evidence: '+name)
        checks[name]='PASS'
    if 'RESULT 18 passed, 0 failed' not in (root/'artifacts/conversation-drain-tests.log').read_text():
        raise SystemExit('FAIL coordinator completion regression')
    checks['coordinator-completion']='PASS'
    if '\nOK\n' not in (root/'artifacts/project-local-tests.log').read_text():
        raise SystemExit('FAIL concurrent project-local launches')
    checks['project-local-concurrency']='PASS'
    if 'RESULT 9 passed, 0 failed' not in (root/'artifacts/separated-echo-regression.log').read_text():
        raise SystemExit('FAIL recorded separated echo regression')
    checks['separated-echo-regression']='PASS'
    if 'PASS single human voice across revisions' not in (root/'artifacts/speaker-revision-tests.log').read_text():
        raise SystemExit('FAIL single speaker revision regression')
    checks['single-speaker-revisions']='PASS'
    alternation=last_json(root/'artifacts/speaker-alternation-tests.jsonl')
    if alternation.get('result')!='PASS' or alternation.get('verifiedIdentities')!=2 or alternation.get('sourceType')!='synthetic-development':
        raise SystemExit('FAIL synthetic speaker alternation regression')
    checks['synthetic-speaker-alternation']='PASS'
    for name in ('conversation-golden','human-conversation-golden','digital-echo','call-routing-golden'):
        try: require_stable_conversation(root/('artifacts/'+name+'.jsonl'))
        except ValueError as error: raise SystemExit('FAIL '+str(error))
        checks[name]='PASS'
    reliability=last_json(root/'artifacts/reliability-tests.log')
    if reliability.get('result')!='PASS' or reliability.get('passed',0)<49:raise SystemExit('FAIL native cancellation/model reliability')
    checks['native-reliability']='PASS'
    scenarios=json.loads((root/'artifacts/scenario-results.json').read_text())
    if len(scenarios)!=7 or any(r['exitCode'] for r in scenarios):raise SystemExit('FAIL conversation scenarios')
    for scenario in scenarios:
        try: require_stable_conversation(root/'artifacts'/(scenario['scenario']+'.jsonl'))
        except ValueError as error: raise SystemExit('FAIL '+str(error))
    checks['synthetic-scenarios']='PASS'
    segmented=(root/'artifacts/asr-pipeline-tests.log').read_text()
    if segmented.count('PASS full VAD-segmented transcript')!=3:raise SystemExit('FAIL segmented ASR accuracy')
    checks['segmented-asr-wer']='PASS'
    checks['continuous30min']=soak_gate()
    checks['callContinuous30min']=call_soak_gate()
    return checks
if '--fingerprint' in sys.argv:
    print(fingerprint()); raise SystemExit(0)
if '--finalize-soak' in sys.argv:
    data=json.loads(report.read_text())
    if data['sourceFingerprint']!=fingerprint():raise SystemExit('FAIL sources changed after automated validation')
    if soak_gate()!='PASS' or call_soak_gate()!='PASS':raise SystemExit('FAIL current 30 minute evidence (in-person and duplex) is not complete')
    data['automated']['continuous30min']='PASS'
    data['automated']['callContinuous30min']='PASS'
    report.write_text(json.dumps(data,indent=2)+'\n')
    print('PASS current in-person and duplex 30-minute stability gates');raise SystemExit(0)
if '--record' in sys.argv:
    data=dict(schemaVersion=1,sourceFingerprint=fingerprint(),engineFingerprint=engine_fingerprint(),checkedAt=datetime.datetime.now(datetime.timezone.utc).isoformat(),automated=validate_evidence(),manualAcceptance='PENDING: microphone, speakers, double-talk, device change/disconnect and user-disconnected network',completeTranslator=False)
    report.write_text(json.dumps(data,indent=2)+'\n')
    print('Recorded current automated gates; 30 minute status: '+data['automated']['continuous30min']+'; physical/offline acceptance remains pending.')
else:
    data=json.loads(report.read_text())
    if data['sourceFingerprint']!=fingerprint():raise SystemExit('FAIL source changed after validation; run scripts/validate-personal.sh')
    if not data['automated'] or any(value!='PASS' for value in data['automated'].values()):raise SystemExit('FAIL automated acceptance')
    print('PASS current personal-build gates. Complete-translator acceptance: '+data['manualAcceptance'])
