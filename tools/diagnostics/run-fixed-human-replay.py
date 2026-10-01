#!/usr/bin/python3
"""Replay identical PCM, including silence history, through both policy variants."""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import subprocess

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--language',choices=['pt','en'],default='pt')
parser.add_argument('--schedule',choices=['control','fast-trim'],default='fast-trim')
args=parser.parse_args()
name=f'fixed-{args.language}-{args.schedule}'
fixture=root/f'build/latency-policy/{name}.wav'
records=json.loads((root/f'build/latency-policy/{name}-records.json').read_text())
starts=[r['captureFirstFrame'] for r in records]
directory=root/f'artifacts/latency-policy/{name}';directory.mkdir(exist_ok=True)
binary=root/'build/latency-policy/corpus-conversation'
provenance=dict(fixtureSHA256=hashlib.sha256(fixture.read_bytes()).hexdigest(),binarySHA256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                scope='Identical human PCM including fixed silence schedule, continuous production core, development data only')
policies={'control':{},'fast-trim':dict(partialIntervalMs=750,agreements=2,stableAudioMs=600,adaptive=True,trimSpeakerEdges=True)}
def tokens(text):return re.findall(r"[^\W_]+(?:['’][^\W_]+)*",text.casefold())
def distance(a,b):
    previous=list(range(len(b)+1))
    for i,word in enumerate(a,1):
        current=[i]
        for j,other in enumerate(b,1):current.append(min(previous[j]+1,current[-1]+1,previous[j-1]+(word!=other)))
        previous=current
    return previous[-1]
results=[]
for policy,settings in policies.items():
    config=root/f'build/latency-policy/{name}-{policy}.json'
    config.write_text(json.dumps(dict(fixture=str(fixture.relative_to(root)),expectedLanguages=[args.language],minimumPlayed=len(records),
                                     output=str((directory/f'{policy}.wav').relative_to(root)),streaming=settings,
                                     fixtureType=provenance['scope']),indent=2)+'\n')
    trace=directory/f'{policy}.jsonl';error=directory/f'{policy}.err'
    with trace.open('w') as out,error.open('w') as err:
        run=subprocess.run(['scripts/project-local.sh',str(binary.relative_to(root)),'models',str(config.relative_to(root))],cwd=root,stdout=out,stderr=err)
    rows=[json.loads(line) for line in trace.read_text().splitlines() if line.startswith('{')]
    grouped=[dict(recording=r['recording'],reference=r['reference'],translationReference=r['translationReference'],original=[],translation=[],speakers=[]) for r in records]
    for row in rows:
        if row.get('state')!='translated':continue
        index=bisect.bisect_right(starts,row['firstFrame']+8000)-1
        if index<0:raise RuntimeError('Unmapped translated recording')
        grouped[index]['original'].append(row['original']);grouped[index]['translation'].append(row['translation']);grouped[index]['speakers'].append(row['speaker'])
    words=sum(len(tokens(r['reference'])) for r in grouped)
    edits=sum(distance(tokens(r['reference']),tokens(' '.join(r['original']))) for r in grouped)
    result=dict(policy=policy,exitCode=run.returncode,sourceWER=edits/words,referenceWords=words,wordEdits=edits,records=grouped,
                summary=next((r for r in reversed(rows) if 'result' in r),None),meaningReview='PENDING',acousticAcceptance='PENDING')
    results.append(result)
    (directory/f'{policy}-result.json').write_text(json.dumps(dict(provenance=provenance,**result),indent=2,ensure_ascii=False)+'\n')
    print(json.dumps({k:result[k] for k in ('policy','exitCode','sourceWER')}),flush=True)
(directory/'comparison.json').write_text(json.dumps(dict(provenance=provenance,runs=results),indent=2,ensure_ascii=False)+'\n')
