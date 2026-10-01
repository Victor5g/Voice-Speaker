#!/usr/bin/python3
"""Build isolated Whisper sampling experiments; never replace app libraries."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('variant', choices=['reset-rng', 'deterministic-beam'])
parser.add_argument('--beam-size', type=int, choices=[5, 8], default=5)
parser.add_argument('--output', required=True, help='New directory inside build/')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
destination = (root / args.output).resolve()
if (root / 'build').resolve() not in destination.parents or destination.exists():
    parser.error('--output must be a new directory inside this project build/')
if not (root / 'build/whisper/lib/libwhisper.coreml.a').is_file():
    parser.error('Build the project-local Whisper dependency first')
destination.mkdir(parents=True)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(command, **kwargs):
    subprocess.run([str(root / 'scripts/project-local.sh'), *command], cwd=root, check=True, **kwargs)

source = root / 'third_party/whisper.cpp/src/whisper.cpp'
patch = root / 'tools/experiments/asr-state' / (args.variant + '.patch')
candidate = destination / 'whisper.cpp'
shutil.copyfile(source, candidate)
with patch.open() as stream:
    run(['/usr/bin/patch', '--batch', '--fuzz=0', str(candidate)], stdin=stream)
asr_source = root / 'engine/asr/WhisperASR.cpp'
asr = destination / 'WhisperASR.cpp'
text = asr_source.read_text()
needle = 'params.beam_search.beam_size = 5;'
if text.count(needle) != 1:
    raise SystemExit('Unsupported production decoder configuration')
asr.write_text(text.replace(needle, f'params.beam_search.beam_size = {args.beam_size};'))

includes = ['-isystem', 'third_party/ctranslate2/include', '-Iengine/asr',
            '-Ithird_party/whisper.cpp/src', '-Ithird_party/whisper.cpp/include',
            '-Ithird_party/whisper.cpp/ggml/include']
run(['/usr/bin/xcrun', 'clang++', '-std=c++17', '-O3', '-DNDEBUG', '-arch', 'arm64',
     '-mmacosx-version-min=14.0', '-DGGML_USE_BLAS', '-DGGML_USE_CPU', '-DGGML_USE_METAL',
     '-DWHISPER_VERSION="1.8.3"', '-DWHISPER_USE_COREML', *includes,
     '-c', str(candidate), '-o', str(destination / 'whisper.o')])
run(['/usr/bin/ar', 'rcs', str(destination / 'libwhisper.a'), str(destination / 'whisper.o')])
links = ['-L' + str(destination), '-Lbuild/whisper/lib', '-lwhisper', '-lwhisper.coreml',
         '-framework', 'CoreML', '-lggml', '-lggml-cpu', '-lggml-metal', '-lggml-blas', '-lggml-base',
         '-framework', 'Foundation', '-framework', 'Metal', '-framework', 'MetalKit', '-framework', 'Accelerate']
for test, name in [('AsrBenchmark.mm', 'benchmark'), ('ASRPreemptionTests.cpp', 'preemption-test')]:
    run(['/usr/bin/xcrun', 'clang++', '-std=c++20', '-O2', '-fobjc-arc', *includes,
         'tests/cpp/' + test, str(asr), *links, '-o', str(destination / name)])
provenance = {
    'scope': 'Experimental native binaries only; production dependency and app are unchanged',
    'variant': args.variant, 'beamSize': args.beam_size,
    'sourceSHA256': {str(p.relative_to(root)): sha(p) for p in
                    [source, patch, asr_source, root / 'tests/cpp/AsrBenchmark.mm',
                     root / 'tests/cpp/ASRPreemptionTests.cpp', Path(__file__).resolve()]},
    'candidateSHA256': {p.name: sha(p) for p in [candidate, asr, destination / 'libwhisper.a',
                                               destination / 'benchmark', destination / 'preemption-test']},
    'linkedDependencySHA256': {p.name: sha(p) for p in sorted((root / 'build/whisper/lib').glob('*.a'))},
    'qualityAcceptance': 'NOT ESTABLISHED; deterministic output does not prove correctness',
}
(destination / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
print('Built isolated experiment:', destination)
