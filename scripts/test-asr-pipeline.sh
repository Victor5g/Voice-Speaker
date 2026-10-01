#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/build-vad-library.sh
test -f build/whisper/lib/libwhisper.coreml.a || scripts/build-whisper.sh
test -f build/translation/lib/libtranslation-deps.a || scripts/build-translation.sh
test -f build/sherpa/lib/libspeech-deps.a || scripts/build-sherpa.sh
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations -isystem build/sherpa/_deps/onnxruntime-src/include -isystem build/sherpa/_deps/kaldi_native_fbank-src -isystem third_party/ctranslate2/include -isystem third_party/sentencepiece/src -isystem third_party/sherpa-onnx -Ithird_party/webrtcvad -Ithird_party/whisper.cpp/include -Ithird_party/whisper.cpp/ggml/include tests/cpp/AsrPipelineTests.cpp engine/audio/AudioCore.cpp engine/vad/VadWorker.cpp engine/asr/ASRWorker.cpp engine/speakers/SpeakerEmbeddingEngine.cpp engine/speakers/OverlapDetector.cpp engine/speakers/OnlineDiarizer.cpp engine/separation/SeparationEngine.cpp engine/asr/WhisperASR.cpp engine/conversation/ConversationPipeline.cpp engine/translation/TranslationEngine.cpp engine/tts/TTSEngine.cpp build/translation/lib/libtranslation-deps.a build/sherpa/lib/libspeech-deps.a build/webrtcvad/libwebrtcvad.a -Lbuild/whisper/lib -lwhisper -lwhisper.coreml -framework CoreML -lggml -lggml-cpu -lggml-metal -lggml-blas -lggml-base -framework Foundation -framework Metal -framework MetalKit -framework Accelerate -o build/asr-pipeline-tests
/usr/bin/python3 - <<'PY'
import json
from pathlib import Path
import subprocess
import re
root = Path.cwd()
selection = json.loads((root / 'models/selection.json').read_text())
result = subprocess.run(['scripts/project-local.sh', str(root / 'build/asr-pipeline-tests'), str(root / 'models' / selection['filename']), selection['sha256']], capture_output=True, text=True, timeout=180)
(root / 'artifacts/asr-pipeline-tests.log').write_text(result.stdout + result.stderr)
print(result.stdout + result.stderr)
if result.returncode == 0:
    references = {
        'ptbr': 'Vamos começar a reunião. Eu preciso de cinco minutos para organizar os documentos. Depois podemos conversar sobre o projeto.',
        'enus': 'Let us begin the meeting. I need five minutes to organize the documents. Then we can talk about the project.',
        'engb': 'I think we should wait. Please give me five minutes to check the documents before we start the meeting.'}
    for name, expected in references.items():
        actual = ' '.join(line.split(' text=', 1)[1] for line in result.stdout.splitlines() if line.startswith('FIXTURE ' + name + ' '))
        a, b = re.findall(r'\w+', expected.lower()), re.findall(r'\w+', actual.lower())
        row = list(range(len(b) + 1))
        for i, word in enumerate(a, 1):
            current = [i]
            for j, other in enumerate(b, 1):
                current.append(min(current[-1] + 1, row[j] + 1, row[j - 1] + (word != other)))
            row = current
        error = row[-1] / len(a)
        status = 'PASS' if error <= 0.25 else 'FAIL'
        message = f'{status} full VAD-segmented transcript {name}: literal WER={error:.4f} (fixture gate <= 0.25)'
        print(message)
        with (root / 'artifacts/asr-pipeline-tests.log').open('a') as log: log.write(message + '\n')
        if status == 'FAIL': raise SystemExit(1)
raise SystemExit(result.returncode)
PY
