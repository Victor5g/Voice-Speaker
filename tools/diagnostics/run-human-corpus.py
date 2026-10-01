#!/usr/bin/python3
"""Compare source accuracy on development humans; keep acoustic/meaning gates open."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--limit',type=int,default=6)
parser.add_argument('--tag',default='screen')
parser.add_argument('--comparison',choices=('streaming','comma-boundary'),default='streaming')
parser.add_argument('--binary',type=Path,default=Path('build/latency-policy/human-corpus'))
args=parser.parse_args()
if not 1<=args.limit<=30 or Path(args.tag).name!=args.tag or args.tag in ('.','..'):
    raise SystemExit('Expected 1..30 development samples and one directory tag')
directory=root/'artifacts/latency-policy'/('human-'+args.tag)
if directory.exists(): raise SystemExit('Use a new tag; existing evidence must not be overwritten')
binary=(root/args.binary).resolve()
if root/'build' not in binary.parents or not binary.is_file(): raise SystemExit('Expected a compiled native binary inside build/')
directory.mkdir(parents=True)
engine_hash=hashlib.sha256()
for p in sorted(p for folder in ('engine','bridge') for p in (root/folder).rglob('*') if p.is_file()):
    engine_hash.update(str(p.relative_to(root)).encode());engine_hash.update(p.read_bytes())
provenance=dict(engineFingerprint=engine_hash.hexdigest(),binarySHA256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                corpusSHA256=hashlib.sha256((root/'tests/data/latency-human/manifest.json').read_bytes()).hexdigest(),
                scope='Development split only, isolated native route, output drained between recordings')
policies={'control':dict(partialIntervalMs=1500,agreements=3,stableAudioMs=0,adaptive=False,trimSpeakerEdges=False),
          'fast-trim':dict(partialIntervalMs=750,agreements=2,stableAudioMs=600,adaptive=True,trimSpeakerEdges=True)}
if args.comparison=='comma-boundary':
    production=policies['fast-trim']
    policies={'control':production,'comma-boundary':dict(production,minimumBoundaryProbability=.3,
        consecutiveBoundaryEvidence=False,allowCommaToPeriodBoundary=True)}
provenance.update(comparison=args.comparison,policies=policies,
    percentiles='Not computed from this run; variable inter-recording silence and meaning require separate review')
def tokens(text):return re.findall(r"[^\W_]+(?:['’][^\W_]+)*",text.casefold())
def distance(a,b):
    previous=list(range(len(b)+1))
    for i,word in enumerate(a,1):
        current=[i]
        for j,other in enumerate(b,1):current.append(min(previous[j]+1,current[-1]+1,previous[j-1]+(word!=other)))
        previous=current
    return previous[-1]
results=[]
for language in ('pt','en'):
    # Reverse the order for EN so the candidate is not invariably last.
    ordered=list(policies.items())
    if args.comparison=='comma-boundary' and language=='en': ordered.reverse()
    for name,policy in ordered:
        config=directory/f'{language}-{name}-config.json'
        config.write_text(json.dumps(dict(language=language,limit=args.limit,streaming=policy),indent=2)+'\n')
        trace=directory/f'{language}-{name}.jsonl';error=directory/f'{language}-{name}.err'
        print(f'Starting {language} {name}: {args.limit} development recordings',flush=True)
        with trace.open('w') as out,error.open('w') as err:
            run=subprocess.run(['scripts/project-local.sh',str(binary.relative_to(root)),'models',str(config.relative_to(root))],cwd=root,stdout=out,stderr=err,timeout=7200)
        rows=[json.loads(line) for line in trace.read_text().splitlines() if line.startswith('{')]
        records=[r for r in rows if r.get('recordingResult')]
        words=sum(len(tokens(r['reference'])) for r in records)
        edits=sum(distance(tokens(r['reference']),tokens(r['original'])) for r in records)
        result=dict(language=language,policy=name,exitCode=run.returncode,recordings=len(records),referenceWords=words,
                    wordEdits=edits,sourceWER=edits/words if words else None,records=records,
                    summary=next((r for r in reversed(rows) if 'result' in r),None),
                    acousticAcceptance='PENDING: no acoustic annotations',meaningReview='PENDING')
        results.append(result)
        (directory/'comparison.json').write_text(json.dumps(dict(provenance=provenance,runs=results),indent=2,ensure_ascii=False)+'\n')
        (directory/f'{language}-{name}-result.json').write_text(json.dumps(dict(provenance=provenance,**result),indent=2,ensure_ascii=False)+'\n')
        print(json.dumps({k:result[k] for k in ('language','policy','exitCode','recordings','sourceWER')}),flush=True)
(directory/'comparison.json').write_text(json.dumps(dict(provenance=provenance,runs=results),indent=2,ensure_ascii=False)+'\n')
if any(r['exitCode'] or r['recordings']!=args.limit or not r['summary'] or r['summary'].get('result')!='PASS' for r in results):
    raise SystemExit(1)
