#!/usr/bin/env python3
"""Development only: deterministic local Marian conversion with all input hashes recorded."""
import os
from pathlib import Path
root = Path(__file__).resolve().parent.parent
os.environ.update(HF_HUB_OFFLINE='1', TRANSFORMERS_OFFLINE='1', HF_HUB_DISABLE_TELEMETRY='1',
                  HF_HOME=str(root / 'cache/huggingface'), TORCH_HOME=str(root / 'cache/torch'),
                  TOKENIZERS_PARALLELISM='false', OMP_NUM_THREADS='4')
import hashlib
import json
import shutil
import ctranslate2
import torch

assert ctranslate2.__version__ == '4.8.0'
torch.set_num_threads(4)
class LocalMarianConverter(ctranslate2.converters.TransformersConverter):
    def load_model(self, model_class, model_name_or_path, **kwargs):
        # CT2 4.8 uses the new dtype keyword; pinned Transformers 4.46 uses torch_dtype.
        kwargs['torch_dtype'] = kwargs.pop('dtype', None)
        return model_class.from_pretrained(model_name_or_path, local_files_only=True, **kwargs)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''): h.update(chunk)
    return h.hexdigest()

manifest_path = root / 'models/manifest.json'
catalog = json.loads(manifest_path.read_text())
for direction in ('pt-en', 'en-pt'):
    source = root / 'cache/model-sources' / direction
    provenance = json.loads((source / 'source-manifest.json').read_text())
    for item in provenance['files']:
        path = source / item['filename']
        if path.stat().st_size != item['expectedSize'] or digest(path) != item['sha256']:
            raise SystemExit('Conversion input integrity failed')
    for precision in ('float32', 'int8'):
        output = root / 'models/optional/translation' / (direction + '-' + precision)
        if not (output / 'provenance.json').is_file():
            LocalMarianConverter(str(source), low_cpu_mem_usage=False).convert(
                str(output), quantization=precision, force=True)
            for name in ('source.spm', 'target.spm', 'README.md'):
                shutil.copyfile(source / name, output / name)
            (output / 'provenance.json').write_text(json.dumps(dict(provenance, converter='CTranslate2 4.8.0',
                precision=precision, sourceLanguage='pt-BR' if direction == 'pt-en' else 'en',
                targetLanguage='en' if direction == 'pt-en' else 'pt-BR',
                sourcePrefix='' if direction == 'pt-en' else '>>pob<<'), indent=2) + '\n')
        assets = [{'filename': str(path.relative_to(root / 'models')), 'sha256': digest(path),
                   'expectedSize': path.stat().st_size} for path in sorted(output.iterdir()) if path.is_file()]
        weights = next(f for f in assets if f['filename'].endswith('/model.bin'))
        entry = dict(weights, name='Marian ' + direction + ' ' + precision, type='translation',
                     version=provenance['revision'], license=provenance['license'],
                     source='https://huggingface.co/' + provenance['repository'] + '/tree/' + provenance['revision'],
                     backend='CTranslate2 4.8.0 / CPU', quantization=precision, direction=direction, files=assets)
        catalog['models'] = [m for m in catalog['models'] if m['filename'] != entry['filename']] + [entry]
        manifest_path.write_text(json.dumps(catalog, indent=2) + '\n')
        print('Converted and verified ' + entry['name'], flush=True)
