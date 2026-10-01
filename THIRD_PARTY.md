# Componentes de terceiros

Pins completos, arquivos e hashes: `third_party/dependencies.lock.json`. Modelos e
auxiliares: `models/manifest.json`. Os notices de runtime estão em `app/Resources/` e
no bundle; os fontes vendorizados ficam disponíveis neste projeto.

| Componente | Versão / commit | Licença | Uso |
|---|---|---|---|
| WebRTC VAD | py-webrtcvad 2.0.10 / ba13ceea9dd3f3a068e869b2e94413205bd87f7d | BSD-3-Clause; wrapper MIT | Subconjunto C, sem Python |
| whisper.cpp | 1.8.3 / 2eeeba56e9edd762b4b38467bab96c2517163158 | MIT | ASR CPU/Metal/Accelerate e encoder Core ML opcional |
| CTranslate2 | 4.8.0 / 54a546cec4262f9770d4674a0bfb4ac3c4f05698 | MIT | Tradução CPU ARM64, Ruy INT8 / Accelerate FP32 |
| SentencePiece | 0.2.0 / 17d7580d6407802f85855d2cc9190634e2c95624 | Apache-2.0 | Tokenização nativa |
| Sherpa-ONNX | 1.13.6 / 1cb484af5e69d3c7803c1eb0b3b5ab8041e0e911 | Apache-2.0 | Piper/VITS; APIs nativas |
| ONNX Runtime | 1.27.1, distribuição ARM64 estática com SHA-256 fixo | MIT + ThirdPartyNotices | TTS, embeddings, segmentação e separação por CPU |
| piper-phonemize | f3ff95afc03640bc1399e113e83361192a2fafb4 | MIT + uni-algo | Fonemização local |
| espeak-ng | ed530aa113046142eb5115cf2fc9157854d0ffe1 | GPL-3.0-or-later + notices de dados | Pronúncia e dados incluídos |
| Kaldi native fbank | 1.22.3 | Apache-2.0 | Frontend de embeddings |
| CMake | 3.31.8, archive fixo | BSD-3-Clause + notices | Ferramenta de desenvolvimento local |
| Python/torch/transformers/CT2 converter | `scripts/conversion-requirements.lock` | Licenças próprias dos pacotes | Conversão de desenvolvimento; nunca runtime |
| SDK/toolchain Apple | Xcode 27.0 / macOS SDK 27.0 | Termos Apple | SwiftUI, AVFoundation, Metal, Core ML, Accelerate, CommonCrypto |

Upstreams principais: [Whisper](https://github.com/ggml-org/whisper.cpp),
[CTranslate2](https://github.com/OpenNMT/CTranslate2),
[SentencePiece](https://github.com/google/sentencepiece),
[Sherpa](https://github.com/k2-fsa/sherpa-onnx),
[ONNX Runtime](https://github.com/microsoft/onnxruntime/releases/tag/v1.27.1).
As dependências transitivas do Sherpa são fixadas por URL/hash nos arquivos CMake do
commit vendorizado; o lock também registra o hash dessas definições. Ruy, cpuinfo,
cpu_features e spdlog têm commits de submódulo registrados. Notices adicionais de
Eigen, JSON, OpenFST/Kaldi, KissFFT e bibliotecas de tokenização acompanham o bundle.

## Modificações locais

- `third_party/patches/sherpa-private-runtime.patch`: logger silencioso, telemetria ORT
  desativada e exceções tratáveis no lugar de encerramento de processo. Também corrige dois symlinks absolutos de exemplos não utilizados. Hash no lock.
- SentencePiece recebe `-Dgoogle=lvt_sentencepiece_google` na compilação para isolar
  seu protobuf interno do protobuf ligado pelo ONNX Runtime. Fontes upstream intactos.
- Whisper não usa servidor, curl, SDL, FFmpeg ou OpenMP. O shader Metal é embutido e
  compilado pelo runtime Apple; não se instalou o componente global opcional do Metal.
- CTranslate2 usa bibliotecas estáticas, sem CUDA/MKL/DNNL/CLI. Sherpa desativa Python,
  PortAudio, WebSocket, exemplos, executáveis e serviços. O provider usado é CPU.

Arquivo ONNX Runtime:
`onnxruntime-osx-arm64-static_lib-1.27.1.zip`, SHA-256
`b9a84d5d1770818a8bb2a12d9adb45fc2cf5062b930176914cd4e7150ce3fcd2`.
A versão upstream e os notices estão fixados também pelo commit no lock.

A entrega é pessoal e inclui componentes GPL e vozes com termos específicos.
Não representa autorização para redistribuição comercial. Fontes, patches e comandos
de compilação são mantidos no projeto; nenhuma conta ou aceite de condições gated
foi feito em nome do usuário. Veja [MODEL_LICENSES.md](MODEL_LICENSES.md).

Whisper possui o patch local `third_party/patches/whisper-coreml-selection.patch`,
fixado em `dependencies.lock.json`: seleção Core ML por contexto, validação da
saída, reutilização apenas do encoder correspondente ao mesmo mel e leitura dos
logits atuais do token SOT para a probabilidade de ausência de fala (inclusive
com contexto textual anterior). Essa correção não executa detecção de idioma
no modo manual nem altera os limiares do Whisper. Ao restaurar
as fontes upstream, aplique esse patch antes de `scripts/build-whisper.sh`.
Não há runtime MLX ou Python no aplicativo.
