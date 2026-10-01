# Modelos e licenças

`models/manifest.json` fixa nome, tipo, revisão, arquivo, tamanho, SHA-256, origem,
licença e backend. `files` inclui tokenizadores, dados de pronúncia, configuração e
model cards. `verify-models.py --release` verifica todos os candidatos e a presença do
conjunto selecionado; `prepare-model-resources.py` embute apenas o conjunto de runtime.
A verificação C++ repete os hashes antes de usar cada modelo. Nenhum download ocorre
no aplicativo.

| Componente | Origem imutável | Termos registrados |
|---|---|---|
| Whisper tiny/base/small | ggerganov/whisper.cpp, 5359861c739e955e79d9a303bcbc70fb988958b1 | MIT; notice OpenAI incluído |
| Whisper large-v3-turbo Q5_0, candidato de desenvolvimento | ggerganov/whisper.cpp, 5359861c739e955e79d9a303bcbc70fb988958b1; ggml-large-v3-turbo-q5_0.bin | MIT; mesmo notice OpenAI; vocabulário incorporado; não selecionado no aplicativo |
| Whisper small encoder Core ML | Mesmo repositório/commit; ggml-small-encoder.mlmodelc.zip | MIT; SHA-256 do ZIP e dos cinco arquivos extraídos no manifesto |
| PT→EN Marian | Helsinki-NLP/opus-mt-ROMANCE-en, e9ca9975e3972afd80732f08ce01d3a1339f47f8 | Apache-2.0 |
| EN→PT Marian | Helsinki-NLP/opus-mt-tc-big-en-pt, 9f2863d807ecf91a374bdbecb8d01e402e90622e | CC-BY-4.0; marcador `>>pob<<` obrigatório |
| ECAPA512 | Wespeaker/wespeaker-voxceleb-ecapa-tdnn512, 89426e111abc9ffcc40279baf808e087693c6e9b | CC-BY-4.0 |
| ResNet34 de referência | Wespeaker/wespeaker-voxceleb-resnet34, ff1ac5bca8ef11e90662b879aa923979e0bd277b | CC-BY-4.0; apenas avaliação |
| Segmentação Pyannote 3.0 | Archive público Sherpa, asset 197666131, SHA-256 no manifest | MIT no archive disponibilizado; sem obtenção da variante gated |
| Conv-TasNet Libri2Mix 16 kHz | welcomyou/convtasnet-libri2mix-16k-onnx, da50e0fa7789356790994bc898290134fef5d42d | CC-BY-SA-4.0 |

Fontes dos modelos e seus cards:
[Whisper large-v3-turbo](https://huggingface.co/openai/whisper-large-v3-turbo),
[PT→EN](https://huggingface.co/Helsinki-NLP/opus-mt-ROMANCE-en),
[EN→PT](https://huggingface.co/Helsinki-NLP/opus-mt-tc-big-en-pt),
[ECAPA512](https://huggingface.co/Wespeaker/wespeaker-voxceleb-ecapa-tdnn512),
[ResNet34](https://huggingface.co/Wespeaker/wespeaker-voxceleb-resnet34),
[segmentação Sherpa](https://k2-fsa.github.io/sherpa/onnx/speaker-diarization/models.html),
[Conv-TasNet](https://huggingface.co/welcomyou/convtasnet-libri2mix-16k-onnx).

As duas variantes Marian FP32/INT8 são convertidas somente no ambiente Python local,
com arquivos de origem verificados e `provenance.json`. O bundle contém INT8/CPU;
não se atribui aceleração Metal ao CTranslate2. PT→EN é restrito a português pelo
roteador embora o modelo de origem suporte outras línguas românicas.

## Vozes

Os seis archives Sherpa/Piper têm asset IDs, tamanhos e SHA-256 em
`models/tts-downloads.lock.json`. Os cards upstream são fixados na revisão
`1162a9173d0ce503555aed757976b7a9912eae4c` de `rhasspy/piper-voices` e em
`models/tts-cards.lock.json`. O repositório Piper identifica licença MIT para modelos;
os cards preservam separadamente os termos dos datasets e do pré-treinamento:

| Par | PT-BR | EN-US | Proveniência do dataset nos cards |
|---|---|---|---|
| 1 | Faber medium | Lessac medium | Faber: CC0; Lessac: termos específicos Blizzard/Lessac |
| 2 | Jeff medium | Ryan medium | Jeff: CC0; Ryan: CC-BY-NC-SA-4.0 |
| 3 | Edresson low | Amy medium | Edresson: CC-BY-4.0; Amy: remete a Mimic3 Voices, CC-BY-SA-4.0 |

Os cards também registram pré-treinamento Lessac/Ryan. Os termos Lessac relativos
às gravações de treinamento restringem seu uso a pesquisa; não se baixaram essas
gravações. O arquivo de condições upstream e os notices CC/Mimic3 acompanham os
recursos. Estes termos não são reduzidos a uma declaração genérica de licença MIT.
A entrega selecionada é pessoal, local, sem publicação comercial. Consulte os
[model cards Piper](https://huggingface.co/rhasspy/piper-voices/tree/1162a9173d0ce503555aed757976b7a9912eae4c),
[Lessac](https://www.cstr.ed.ac.uk/projects/blizzard/2013/lessac_blizzard2013/license.html)
e [Mimic3 Voices](https://github.com/MycroftAI/mimic3-voices).

A fonemização espeak-ng e seus dados têm GPL-3.0-or-later e notices adicionais,
incluídos em `app/Resources` e nos diretórios das vozes. Não há entrada para clonar
uma voz do usuário: cada participante recebe apenas um dos três pares existentes.

## Fixtures humanas e sintéticas

Google FLEURS, revisão `70bb2e84b976b7e960aa89f1c648e09c59f894dd`, CC-BY-4.0:
12 gravações de PT-BR/EN-US com referências, hashes originais/convertidos e archives
fixados em `tests/data/human/manifest.json`. Atribuição: Google FLEURS e autores do
dataset, [fonte](https://huggingface.co/datasets/google/fleurs). Conversas concatenadas
e misturas de avaliação são derivados identificados; não entram no app. O conjunto
não fornece identidade de participante para validar reidentificação bilíngue.

Fixtures sintetizados por `say` durante desenvolvimento são rotulados como tais e
separados das gravações humanas. Re-reconhecer o próprio Piper testa integração,
mas não é prova independente de qualidade acústica ou de sotaques humanos.
