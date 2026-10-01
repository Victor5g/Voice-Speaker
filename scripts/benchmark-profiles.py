#!/usr/bin/python3
"""Measure complete native candidate configurations; never assign profiles by size/name."""
import json,subprocess,re,statistics
from pathlib import Path
root=Path(__file__).resolve().parent.parent
models=json.loads((root/'models/manifest.json').read_text())['models']
human=json.loads((root/'tests/data/human/manifest.json').read_text())['recordings']
def wer(reference,actual):
    a=re.findall(r'\w+',reference.lower());b=re.findall(r'\w+',actual.lower());row=list(range(len(b)+1))
    for i,word in enumerate(a,1):
        current=[i]
        for j,other in enumerate(b,1):current.append(min(current[-1]+1,row[j]+1,row[j-1]+(word!=other)))
        row=current
    return row[-1]/max(1,len(a))
results=[]
for model in [m for m in models if m['type']=='asr']:
    name=Path(model['filename']).stem
    output=root/('artifacts/profile-'+name+'.jsonl')
    config=root/('build/profile-'+name+'.json')
    config.write_text(json.dumps(dict(asr=dict(filename=model['filename'],sha256=model['sha256']),output='artifacts/profile-'+name+'.wav',fixtureType='complete synthetic PT/EN pipeline; development machine, other tests may run'))+'\n')
    with output.open('w') as out, (root/('artifacts/profile-'+name+'.err')).open('w') as err:
        run=subprocess.run(['scripts/project-local.sh','build/conversation-evaluation','models',str(config)],cwd=root,stdout=out,stderr=err)
    records=[json.loads(line) for line in output.read_text().splitlines()]
    summary=records[-1] if records and records[-1].get('result')=='PASS' else dict(result='FAIL')
    asrOutput=root/('artifacts/profile-human-'+name+'.jsonl')
    with asrOutput.open('w') as out, (root/('artifacts/profile-human-'+name+'.err')).open('w') as err:
        subprocess.run(['scripts/project-local.sh','build/asr-benchmark','models/'+model['filename'],model['sha256'],'1',*[item['filename'] for item in human]],cwd=root,stdout=out,stderr=err,check=True)
    references={str(Path(r['filename']).name):r for r in human}
    accuracy=[]
    for line in asrOutput.read_text().splitlines():
        result=json.loads(line);reference=references[Path(result['fixture']).name]
        accuracy.append(dict(fixture=reference['filename'],language=reference['language'],wer=wer(reference['reference'],result['sourceText']),actual=result['sourceText']))
    record=dict(model=model['filename'],fullPipelineExitCode=run.returncode,fullPipeline=summary,humanMeanWER=statistics.mean(r['wer'] for r in accuracy),human=accuracy,translation='CPU INT8',speaker='ECAPA512',overlap='Pyannote3.0',tts='six paired Piper voices, one loaded at a time')
    results.append(record)
    (root/'artifacts/profile-comparison.json').write_text(json.dumps(results,indent=2)+'\n')
    print(name,'complete pipeline',run.returncode,'human mean WER',record['humanMeanWER'],flush=True)
