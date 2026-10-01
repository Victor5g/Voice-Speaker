#!/usr/bin/env python3
"""Development feasibility probe, not a native pipeline or quality benchmark.

Uses the existing converter's ORT (reported explicitly), synthetic tensors and
profiles provider assignment. Never selects a production backend from this probe.
"""
import gc
import json
from pathlib import Path
import statistics
import time
import numpy as np
import onnxruntime as ort

root = Path(__file__).resolve().parent.parent
output = root / 'artifacts/coreml-comparison/onnx-probe'
output.mkdir(parents=True, exist_ok=True)
ort.disable_telemetry_events()
ort.set_default_logger_severity(4)
manifest = json.loads((root / 'models/manifest.json').read_text())['models']
rng = np.random.default_rng(123)
results = {'runtime': ort.__version__, 'scope': 'Synthetic graph feasibility only; not production runtime, speech quality, or ANE placement.', 'results': []}
for kind, shape in [('embedding', (1, 298, 80)), ('overlap', (1, 1, 160000)), ('separation', (1, 48000))]:
    entry = next(m for m in manifest if m['type'] == kind)
    values = rng.normal(0, .1, shape).astype(np.float32)
    reference = None
    for backend in ('cpu', 'coreml'):
        row = {'model': kind, 'backend': backend, 'inputShape': shape}
        print('Probe ' + kind + '/' + backend, flush=True)
        session = None
        try:
            options = ort.SessionOptions()
            options.intra_op_num_threads = 2
            options.inter_op_num_threads = 1
            options.log_severity_level = 4
            options.enable_profiling = True
            options.profile_file_prefix = str(output / (kind + '-' + backend))
            providers = ['CPUExecutionProvider'] if backend == 'cpu' else [('CoreMLExecutionProvider', {'coreml_flags': '16'}), 'CPUExecutionProvider']
            started = time.perf_counter()
            session = ort.InferenceSession(str(root / 'models' / entry['filename']), options, providers=providers)
            session.disable_fallback()
            if backend == 'coreml' and 'CoreMLExecutionProvider' not in session.get_providers():
                raise RuntimeError('Core ML initialization fell back to CPU')
            row['loadMs'] = (time.perf_counter() - started) * 1000
            feed = {session.get_inputs()[0].name: values}
            actual = session.run(None, feed)[0]
            if not np.isfinite(actual).all(): raise RuntimeError('Non-finite inference result')
            if reference is None: reference = actual.copy()
            row['maxAbsErrorVsCPU'] = float(np.max(np.abs(actual - reference)))
            elapsed = []
            for _ in range(30):
                started = time.perf_counter()
                session.run(None, feed)
                elapsed.append((time.perf_counter() - started) * 1000)
            row.update(samples=len(elapsed), medianMs=statistics.median(elapsed), p95Ms=float(np.percentile(elapsed, 95)))
            profile = Path(session.end_profiling())
            assignments = {}
            for event in json.loads(profile.read_text()):
                provider = event.get('args', {}).get('provider')
                if provider: assignments.setdefault(provider, set()).add(event.get('name'))
            row['profiledNodesByProvider'] = {p: len(nodes) for p, nodes in assignments.items()}
            row['status'] = 'measured' if backend == 'cpu' or assignments.get('CoreMLExecutionProvider') else 'no-coreml-nodes'
        except Exception as error:
            row.update(status='failed', error=str(error))
        finally:
            del session
            gc.collect()
        results['results'].append(row)
        (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        print(json.dumps(row), flush=True)
