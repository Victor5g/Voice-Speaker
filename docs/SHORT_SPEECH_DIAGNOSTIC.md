# Diagnóstico de fala curta — 2026-09-27

**Estado: falha de reconhecimento de “Sim” ainda aberta. Foi corrigida uma leitura incorreta dos logits de ausência de fala no backend, comprovada por código e PCM idêntico; isso não resolve toda alucinação.**

## Teste funcional e falha observada

Antes da correção dos logits SOT descrita abaixo, a suíte terminou com **37 verificações aprovadas e 1 reprovada**. Essa execução está preservada em `artifacts/realtime-improvements/asr-controls-native-before-sot-fix.jsonl/.err`. Os arquivos canônicos `asr-controls-native.jsonl/.err` agora contêm a execução posterior à correção, também 37/38, descrita no fim deste documento. Nenhuma das execuções é uma suíte integralmente aprovada.

As expressões foram sintetizadas localmente com Sherpa/Piper, usando Faber em português e Lessac em inglês. São regressões de desenvolvimento, sem prova de qualidade em gravação humana. O teste conserva comparação de palavras esperadas, além da verificação de que o áudio chegou ao reconhecimento.

| Entrada sintetizada | Duração do recorte | Reconhecimento | Comparação |
| --- | ---: | --- | --- |
| Obrigado. | 704,625 ms | Obrigado. | Compatível |
| Sim. | 376 ms | Até mais! | **Falhou** |
| Não. | 371,875 ms | não. | Compatível |
| Ok. | 397 ms | Ok. | Compatível |
| Thanks. | 542,25 ms | Thanks. | Compatível |

“Sim” **não foi bloqueado** pela proteção contra silêncio: `rejectedNoSpeech=false`; houve inferência real, com saída incorreta. A probabilidade `no_speech` era aproximadamente 0,00001927, posteriormente identificada como distribuição uniforme resultante de logits não preenchidos. A média do log das probabilidades de tokens textuais era −1,3435. Esses números não são confiança semântica calibrada e não permitem inventar uma regra que converta “Até mais” em “Sim”.

Entradas de 100–499 ms recebem padding local até 500 ms para entrar no modelo, sem aguardar novas amostras de captura. A duração original permanece em `ASRResult.durationMs`. Entradas abaixo de 100 ms continuam fora do contrato atual. Isso garante tentativa de reconhecimento de falas breves, **não garante palavras corretas**.

## Experimento de padding e contexto

Código: `tests/cpp/ASRShortSpeechProbe.cpp`.

Comandos executados:

```sh
bash scripts/build-production-test.sh tests/cpp/ASRShortSpeechProbe.cpp build/asr-short-speech-probe
scripts/project-local.sh build/asr-short-speech-probe models > artifacts/realtime-improvements/short-speech-probe.jsonl 2> artifacts/realtime-improvements/short-speech-probe.err
```

Compilação e execução terminaram com código zero. Isso significa **experimento concluído**, não aceitação de qualidade: a última linha registra `EXPERIMENT_COMPLETED`, `productionChanged=false` e `qualityAccepted=false`.

Foram **60 reconhecimentos**: cinco expressões × áudio original ou recortado × duração mínima de 500/1000/2000 ms × prefixo silencioso de 0/200 ms. Cada condição usou estado Whisper independente, sem reconhecimento anterior, prompt textual ou mudança de amostragem. Para cada expressão, todas as condições receberam a mesma origem PCM; o teste não ressintetiza uma palavra para favorecer uma condição.

**Todos os 12 grupos ficaram em 4/5 palavras compatíveis.** “Sim” falhou nas 12 condições: sem prefixo, virou “Tchau tchau.”; com 200 ms de prefixo, virou “Tchau.”. “Obrigado”, “Não”, “Ok” e “Thanks” foram compatíveis em todas as condições.

Portanto, aumentar padding para um ou dois segundos ou restaurar as bordas originais **não resolveu esta falha**. Não foi promovida mudança de padding ou de reconhecimento.

O PCM deste experimento está em `artifacts/realtime-improvements/short-speech-probe/run-516008376667/`, em float32 little-endian, mono, 16 kHz. A entrada “Sim” é `generated-1.f32`, com 6316 frames e SHA-256 `4ff9123fa52ea5c684e52862fb30ee4f75f09fe8602cc8ea8e97689d8ca55185`. O JSONL registra os hashes, limites e condições de todas as entradas. Essa síntese difere da execução funcional anterior; não se deve atribuir a diferença “Até mais”/“Tchau” exclusivamente ao padding entre execuções. A comparação válida de geometria é dentro deste lote com PCM idêntico.

Não foram publicados P50/P95: cada condição tem cinco expressões sintéticas diferentes, sem amostra suficiente ou desenho de benchmark de latência. As latências individuais permanecem no artefato como diagnóstico.

## Ruído: limites distintos de validação

Na execução **anterior à correção SOT**, o teste direto do Whisper com `tests/data/scenarios/noise-only.wav` produziu `Se inscreva e se inscreva no canal!`, com score incorreto `no_speech` de aproximadamente 0,00000265. O filtro adicional do ASR não rejeitou essa saída; `rawASRNonSpeechFailures=1` permanece preservado no artefato `asr-controls-native-before-sot-fix.jsonl`. Na execução **atual**, o mesmo teste retorna vazio e registra `rawASRNonSpeechFailures=0`, tanto no arquivo canônico quanto em `asr-controls-native-lexical.jsonl`. Isso comprova a correção deste fixture, sem estabelecer proteção universal contra ruído.

O **VadWorker de produção**, executado sobre o mesmo fixture sintético, publicou zero finais, zero parciais e entregou zero segmentos para reconhecimento. Detectou quatro frames como voz, mas eles não formaram um trecho aceito. Silêncio digital também entregou zero segmentos. O bypass de idioma/participantes mantém esse VAD; portanto esses testes comprovam a rejeição na entrada do pipeline para esses fixtures, sem tornar o Whisper isolado um detector confiável de não fala.

O tom instrumental precisa de validação posterior ao VAD. Evidência histórica contém segmentos admitidos pelo VAD e rejeitados depois pelo reconhecimento; exigir zero segmentos seria um critério incorreto para esse caso. Música real, ruído variável, gravações físicas e realimentação acústica não são comprovados por silêncio/ruído sintético.

Diagnóstico acústico descritivo reforça o cuidado: o RMS máximo em blocos de 20 ms do ruído foi 0,00899, enquanto uma gravação humana inglesa válida teve máximo de apenas 0,00231. Um limite de volume isolado eliminaria fala legítima. Não foi adicionado filtro de estacionaridade, frequência ou lista de expressões para ajustar artificialmente o resultado.

## Revisão do modo manual e das métricas

- `WhisperASR.recognize(..., forcedLanguage)` aceita PT/EN (normaliza `pt-BR` para `pt`) e deixa os argumentos anteriores compatíveis. O modo manual não chama `whisper_lang_auto_detect_with_state` nem inventa probabilidade 1 para o idioma configurado.
- O mel é preparado uma vez; `whisper_full_with_state` recebe PCM nulo e usa esse mel. Como a preparação do novo mel invalida o cache, o encoder necessário executa dentro do reconhecimento completo. O código nativo só autodetecta idioma quando o parâmetro está vazio/`auto` ou `detect_language` está ativo; o modo manual fornece idioma explícito e mantém esse flag desativado.
- `languageDetections` é contado dentro da propriedade exclusiva do reconhecedor, sob o mesmo arbiter que protege a inferência compartilhada. Cada worker registra sua diferença de contador. STOP encerra o worker antes de limpar os contadores; parâmetros de idioma/identificação são capturados por valor por sessão.
- O worker manual não adiciona observação em `language_`; a bridge publica `asrLanguageMs=null` quando não há amostras, enquanto `languageDetections=0` prova o bypass no teste funcional. Os testes PT/EN também mantêm a autodetecção operacional quando configurada.
- **A identificação automática de idioma inclui trabalho do encoder. No modo manual esse trabalho aparece em `asrDecodeMs`.** Não interpretar a transferência entre colunas como economia integral dos 120–150 ms observados na identificação automática. Comparar `asrLatencyMs`, filas e resposta total sob as mesmas condições.
- `rejectedNoSpeech` conta rejeições implementadas pelo wrapper, não todos os trechos recusados pelo VAD ou internamente pelo Whisper. `rejectedDigitalSilence` identifica o subconjunto de silêncio/DC. A bridge expõe ambos; exportações devem conservar essa distinção se incluírem o segundo contador.

## O que falta

É necessário distinguir erro do ASR, articulação sintética ambígua e sensibilidade do modelo em áudio muito curto. O corpus humano disponível não contém “sim” nas referências consultadas. São necessários exemplos humanos licenciados/fornecidos e avaliação de outras articulações/vozes, mantendo esta falha como regressão explícita. Não foram usados vocabulário forçado, blacklist, substituição de texto ou remoção da asserção semântica.


## Correção comprovada do SOT no Whisper nativo

O instrumento sem voz expôs uma diferença real entre AUTO e idioma manual: o mesmo recorte de PCM retornava vazio em AUTO e palavras inventadas em EN/PT manual. AUTO também havia escolhido EN; portanto a causa não era simplesmente o idioma escolhido.

No upstream fixado, `whisper_batch_prep_legacy` solicita cópia dos logits somente da última posição do prompt. O cálculo de `no_speech_prob` lia a primeira posição de `state->logits`, que permanecia zerada em um estado manual novo (softmax uniforme, aproximadamente 1/51865) ou preservava dados de uma inferência anterior. Com contexto textual, SOT sequer está necessariamente na posição zero. A [implementação de referência OpenAI](https://github.com/openai/whisper/blob/main/whisper/decoding.py) calcula a probabilidade na posição explícita `sot_index`, antes dos filtros de logits.

O patch local agora registra a posição onde começa `prompt_init`, solicita também essa linha ao decoder e calcula softmax exclusivamente sobre ela. Mantém a linha final para a geração do próximo token. Não adiciona passagem de encoder/decoder, não reativa identificação de idioma no modo manual e não altera os limiares nativos. Há apenas a cópia adicional de uma linha de logits e sua normalização no batch já executado.

O patch existente `third_party/patches/whisper-coreml-selection.patch` foi regenerado contra as fontes upstream e passou por `patch --dry-run`. O hash fixado em `third_party/dependencies.lock.json` é `128197866d29275d588f2f086fdeef5865221631d9e89adfb53c9da90ed0f6d2`. A biblioteca anterior foi conservada em `artifacts/realtime-improvements/whisper-before-sot-fix/libwhisper.a`, SHA256 `0a50c5a4f159f0aefe4025e4f6abf172247592d47ea0bdb24cbe9f98a5204039`.

### Comparação com PCM idêntico

Artefatos: `tone-raw-language-probe.jsonl` (antes), `tone-sot-fixed-probe.jsonl` (depois), ambos em `artifacts/realtime-improvements/`. Modelo `ggml-small.bin`, Metal, mesmos hashes de áudio/modelo, estado independente por condição. Recortes exatos do fixture instrumental: `[0,16000)` e `[56640,101120)` em 16 kHz.

| Recorte | Modo | Antes | Depois |
| --- | --- | --- | --- |
| Primeiro | AUTO | vazio | vazio |
| Primeiro | EN manual | You | vazio |
| Primeiro | PT manual | Música | vazio |
| Segundo | AUTO | vazio | vazio |
| Segundo | EN manual | You | vazio |
| Segundo | PT manual | Tchau tchau | vazio |

O probe `ASRNoSpeechRegression.cpp` amplia a comparação para ruído, silêncio, dois WAVs humanos e os cinco PCM sintéticos curtos **retidos do experimento anterior**, sem sintetizá-los novamente. Usa `build/asr-no-speech-regression-inputs.json` com hashes e intervalos; compile com `bash scripts/build-asr-no-speech-regression.sh` e execute com `scripts/project-local.sh build/asr-no-speech-regression models build/asr-no-speech-regression-inputs.json`.

Resultados em `asr-no-speech-fixed.jsonl`:

- Ruído bruto e silêncio: saída vazia em AUTO, EN e PT manual.
- Humanos PT/EN: mesmos textos AUTO/manual; probabilidades de ausência de fala 0,04362 e 0,15014 respectivamente, em vez da distribuição uniforme manual anterior. Isso confirma a recuperação do score; não é avaliação de qualidade completa.
- Obrigado, Não, Ok/Okay e Thanks: preservados. Probabilidades 0,00472, 0,25550, 0,07857 e 0,00690 respectivamente.
- Mesmo PCM de “Sim”: ainda “Tchau tchau.”, probabilidade 0,28811. A falha semântica continua explícita.
- Segundo recorte instrumental, com contexto “Earlier we discussed the meeting.”: AUTO e EN manual ainda produzem “EDITED-MGN-DQ”, probabilidade de ausência de fala 0,80099. O primeiro recorte permanece vazio com esse contexto; PT manual também permanece vazio. Não há aceitação geral de música/TV ou de contexto após esta correção.

A primeira execução da regressão terminou com 23 aprovações e 4 falhas. Três são reais: duas saídas indevidas com contexto e o erro “Sim”. A quarta era uma asserção incorreta de que AUTO sempre deve executar LID, mesmo quando a proteção de silêncio digital retorna antes da inferência. Essa asserção foi corrigida no código para respeitar `rejectedDigitalSilence`; o artefato histórico não foi reescrito nem apresentado como nova execução aprovada.

Quando Whisper retorna zero segmentos, a API usada não fornece score por segmento: `speechConfidenceMeasured=false` e o zero serializado não significa probabilidade medida zero. Não substituir essa ausência por uma estimativa.

### Detector Pyannote: hipótese descartada pelos resultados

O probe de desenvolvimento `SpeechGateProbe.cpp` avaliou a probabilidade bruta de fala `1 - P(classe sem voz)` do mesmo modelo Pyannote já incluído. Evidências frame a frame em `speech-gate-probe.jsonl`, entradas/hashes em `build/speech-gate-probe-inputs.json`.

| Entrada | Máximo de probabilidade de fala | Média |
| --- | ---: | ---: |
| Primeiro instrumental | 0,98950 | 0,98095 |
| Segundo instrumental | 0,57402 | 0,21335 |
| Silêncio | 0,000039 | 0,0000012 |
| Ruído branco | 0,12133 | 0,04691 |
| Humano PT | 0,99983 | 0,77285 |
| Humano EN | 0,99997 | 0,77745 |
| Sim sintético | 0,97496 | 0,82451 |
| Ok sintético | 0,94378 | 0,88529 |

O primeiro instrumental teve 60/60 frames classificados como fala acima de 0,5. Aumentar o limiar para rejeitá-lo eliminaria palavras curtas legítimas antes de eliminar esse tom. Portanto **não foi acrescentado um gate Pyannote** nem atribuído benefício de proteção inexistente. Inferências isoladas custaram aproximadamente 28–60 ms nestas entradas; isso não estabelece P50/P95 ou latência E2E.


## Guard lexical e limite de contexto — validação posterior

Após a correção SOT, o wrapper passou a usar `params.no_speech_thold` e `params.logprob_thold` no guard adicional, exigindo simultaneamente `minimumNoSpeechProbability > no_speech_thold` e média lexical abaixo de `logprob_thold`. Esses parâmetros mantêm os padrões nativos 0,6 e −1,0; não foram ajustados para este fixture. Os antigos critérios adicionais 0,95/−0,5 e RMS inferior a 0,01 foram retirados. A proteção exata de silêncio digital/DC permanece independente.

**Aplicar o limiar à média dos tokens lexicais é uma extensão de política**, não uma equivalência matemática ao critério nativo que inclui tokens especiais. A justificativa é impedir que confiança de timestamp/EOT transforme palavras improváveis em fala confiável. Sua adequação precisa de avaliação com corpus humano; scores não são confiança semântica calibrada.

O mesmo probe, reconstruído com essa extensão e sem mudar nenhum PCM ou expressão esperada, terminou com **26/27 verificações aprovadas**, arquivo `artifacts/realtime-improvements/asr-no-speech-lexical.jsonl`. Agora os 12 casos instrumentais (dois recortes × AUTO/EN/PT × contexto vazio/presente) retornam vazio. Ruído, silêncio, os dois exemplos humanos e Obrigado/Não/Ok/Thanks preservam os resultados esperados. “Sim” ainda resulta em “Tchau tchau.”: a suíte conserva uma falha real e não foi declarada aprovada. Os tempos desta regressão não são um benchmark controlado de desempenho.

Separadamente, `ASRContext.hpp` corrige um defeito de escopo do prompt: o worker anterior acumulava todos os finais e aceitava um intervalo de até 500 ms. Isso podia condicionar uma nova fala após silêncio com o texto de outra fala, especialmente pelo preroll. Agora só conserva uma unidade que termina junto ao fim do segmento e cujo endpoint é `windowLimit`. Todo final substitui o snapshot; `silence`, ausência de resultado ou eco deixam um snapshot vazio. Só concatena texto do mesmo participante/idioma em janelas contínuas. A tolerância é de uma amostra a 16 kHz, pela diferença entre truncar posição absoluta e arredondar tamanho no resampler; não é uma tolerância para pausa.

`ASRControlsTests.cpp` contém 56 verificações, incluindo as fronteiras do guard lexical, corte contínuo em 9,8 s, silêncio seguido de nova fala, pausa de 200 ms, mudança de idioma/participante, unidade diarizada antes do fim e ausência de identidade. Validação nativa do pipeline completo e corpus ampliado é coordenada separadamente; esta seção não substitui esses resultados.


A execução posterior de `ASRControlsNative` também terminou com **37 aprovadas e 1 reprovada**, em `asr-controls-native-lexical.jsonl/.err`. `rawASRNonSpeechFailures=0` nesta execução. O “Sim” recém-sintetizado (372,6875 ms) virou “Até a próxima.”, não foi rejeitado (`noSpeechProbability=0,20602`) e continua como falha. As quatro outras expressões foram preservadas. Esse PCM é diferente do arquivo retido usado no probe: novamente não atribuir diferenças entre frases erradas ao guard ou ao padding. O teste de VAD real não entregou segmentos para silêncio/ruído; trabalhadores manuais, bypass de identificação e STOP passaram. Os 56 checks leves passaram com AddressSanitizer e UndefinedBehaviorSanitizer; evidência em `artifacts/realtime-improvements/asr-controls-unit-lexical.log`.


Proveniência final: os canônicos `artifacts/realtime-improvements/asr-controls-native.jsonl/.err` foram promovidos dos arquivos `asr-controls-native-lexical.jsonl/.err`, preservando integralmente o estado anterior em `asr-controls-native-before-sot-fix.jsonl/.err`. Resultado atual: **37/38** na suíte funcional e **26/27** no probe de PCM retido; em ambos a falha restante é a expressão sintética “Sim”. O patch nativo fixado por SHA256 `128197866d29275d588f2f086fdeef5865221631d9e89adfb53c9da90ed0f6d2` segue a posição SOT da referência OpenAI e permanece combinado com as modificações Core ML locais já existentes; o guard lexical e o limite de contexto são mudanças separadas do wrapper/worker, não atribuídas ao upstream. A validação de pipeline, corpus ampliado e estabilidade prolongada pertence aos relatórios correspondentes.
