# Aceleração Apple: implementação e medição

Atualizado em 25/09/2026. Hardware medido: Apple M2 Pro. Esta entrega adiciona
Core ML ao Whisper e elimina trabalho repetido no reconhecimento. Não demonstra
tradução completa em 45–75 ms nem substitui a aceitação física/offline.

**Correção de aceitação em 27/09/2026:** o PASS histórico de `noise-metal` abaixo
não comprova estabilidade. O JSONL contém `revision-preserved`: o prefixo
confirmado foi alterado pelo ASR final. O teste antigo tolerava esse evento.
O teste e o gate de release agora o rejeitam; a revisão atual reproduziu uma
divergência e reprovou corretamente. Os benchmarks isolados de ASR/ANE mantêm
seu escopo original. Veja [STABILITY_REVIEW.md](STABILITY_REVIEW.md).

## O que foi implementado

- Encoder selecionável por contexto: Metal, Core ML CPU/ANE, Core ML automático
  ou Core ML CPU/GPU. O decoder continua em Metal e a preparação usa CPU.
- Seleção em Ajustes, somente com a sessão parada, aplicada aos dois caminhos da
  chamada. A escolha é mantida em memória, sem alterar o bundle assinado.
- Painel Engine mostra o backend carregado. “CPU/ANE permitidos” descreve a
  configuração: não afirma que todos os operadores executaram no Neural Engine.
- Encoder oficial multilíngue `small` empacotado, com commit, licença MIT,
  tamanho e SHA-256 do arquivo compactado e de cada recurso no manifesto.
- Falhas de carregamento, arquivos ausentes/adicionais/corrompidos e saídas
  inválidas produzem erro explícito. Não há fallback silencioso para outro
  backend, serviço externo ou download durante o uso.
- Reaproveitamento do encoder e da memória de atenção entre identificação do
  idioma e transcrição **do mesmo mel**. Nova entrada PCM/mel, offset diferente,
  contexto de áudio diferente ou inferência incompleta invalidam o resultado.
  Não se reutiliza o encoder de uma janela anterior que cresceu.
- Recursos, modelos e ferramentas continuam dentro do projeto. Core ML é um
  framework do sistema; esta alteração não instala drivers ou serviços.

O patch local do Whisper está em
`third_party/patches/whisper-coreml-selection.patch`, fixado por hash em
`third_party/dependencies.lock.json`. A configuração por contexto evita variáveis
globais de backend em chamadas concorrentes. O arbiter existente continua
serializando o uso do contexto compartilhado.

## Resultados do reconhecimento isolado

Mesmo modelo small, beam 5, quatro threads CPU, idioma automático, sem mudar
os limiares de estabilidade. Doze gravações humanas FLEURS já licenciadas no
projeto, seis por idioma, cinco repetições: **30 amostras por idioma/backend**.
Um aquecimento por gravação excluído; carga medida separadamente. Repetições
medem variação de execução, não representam 30 pessoas ou 30 frases diferentes.

| Encoder / condição | PT P50 / P95 (ms) | EN P50 / P95 (ms) | WER médio PT / EN |
|---|---:|---:|---:|
| Metal, antes de reutilizar encoder | 392 / 536 | 353 / 527 | 9,44% / 8,13% |
| Metal, com reutilização | 256 / 312 | 245 / 321 | 9,44% / 8,13% |
| Core ML CPU/ANE + Metal, com reutilização | 271 / 327 | 271 / 381 | 10,04% / 9,04% |
| Core ML automático + Metal, com reutilização | 273 / 329 | 295 / 351 | 10,04% / 9,04% |
| Core ML CPU/GPU + Metal, com reutilização | 235 / 290 | 224 / 303 | 9,81% / 9,85% |

Metal com reutilização reduziu a mediana aproximadamente **35% em PT e 31% em
EN**, mantendo o WER desse conjunto. Core ML CPU/GPU reduziu mais ~8%, mas
piorou o WER inglês em 1,72 ponto percentual frente ao Metal. Houve mudança
de nome próprio, por exemplo `Bishkek` → `Beech Keck`. Por isso **Metal permanece
o padrão; Core ML está disponível como opção experimental**.

CPU/ANE não venceu Metal neste encoder/modelo. O resultado não permite concluir
que ANE seja sempre lento, nem que outro modelo Core ML ou MLX teria o mesmo
resultado. O pico de footprint registrado durante as amostras isoladas ficou
abaixo de 902 MB; isso não é o pico de toda a conversa nem mede memória de
serviços do sistema usados pelo Core ML.

Evidência: `artifacts/coreml-comparison/summary.json`, os JSONL por backend e
`without-reuse/summary.json`. Tempos incluem mel, identificação de idioma e
transcrição; excluem espera para acumular fala, diarização, tradução, TTS, fila
de playback e dispositivo. Portanto, **não são latências de fala traduzida**.

## Neural Engine: execução confirmada por Instruments

Em 25/09/2026, foi confirmado uso **real do ANE pelo encoder Whisper small**
no modo `coreml-ane`, no M2 Pro com macOS 27.0 e Xcode 27.0. A medição usou o
`WhisperASR` de produção no benchmark nativo, com o mesmo binário das comparações
anteriores (SHA-256 conferido), modelo e gravação humana verificados.

O Instruments gravou o template `Core AI` com os instrumentos `Core ML` e
`Neural Engine`, lançando somente o benchmark como alvo. Um aquecimento mais
20 transcrições da gravação `pt_br-1265054059510035430.wav` produziram:

| Configuração | Eventos de predição do encoder no hardware ANE | Carregamento no ANE |
|---|---:|---:|
| Core ML CPU/ANE + decoder Metal | **21** | 1, em cache |
| Core ML CPU/GPU + decoder Metal, controle | **0** | 0 |

A tabela `ane-hw-intervals` identifica `Apple Neural Engine`, estado `Active` e
o modelo `ggml-small-encoder_main__Op0_AneInference Prediction`. Isso é evidência
de execução do encoder no hardware, além da mera configuração de `computeUnits`.
Os 21 intervalos somam 2.278,34 ms. **Não são latência E2E nem novo benchmark de
velocidade**: houve instrumentação, estado térmico 1, uma única gravação e menos
de 30 amostras. Não foram calculados P50/P95 desta captura.

A tabela de hardware não exporta PID por linha. A atribuição usa o nome específico
do encoder, o alvo lançado com saída 0, a correspondência entre 21 predições e
21 eventos e o controle negativo CPU/GPU. Os signposts de alto nível do Core ML
vieram vazios nessa versão do Instruments; a evidência vem da trilha de hardware
Neural Engine, não desses signposts. Não se afirma rastreamento individual de
cada operador nem execução exclusiva de todo o reconhecimento no ANE.

Como verificação complementar, `MLComputePlan` atribuiu preferência pelo ANE às
**297 operações de cálculo**: 72 linear, 49 transpose, 48 reshape, 25 add,
25 layer_norm, 24 mul, 24 matmul, 14 gelu, 12 softmax, 2 conv e 2 cast.
As 444 constantes não informam dispositivo. No controle, as mesmas 297 operações
preferem GPU. **Esse inventário é o plano antecipado**, separado da confirmação
de execução acima; não são 297 medições independentes de hardware.

Evidências locais:

- `artifacts/coreml-comparison/ane-verification.json`: resultado, contagens,
  hashes dos arquivos exportados e limitações de atribuição.
- `ane-runtime.trace` e `gpu-runtime.trace`, na mesma pasta: rastros originais
  para abrir no Instruments; `*-runtime-events.xml`: eventos exportados.
- `ane-compute-plan.json` e `gpu-compute-plan.json`: inventário por operação.

Reprodução, mantendo todas as saídas dentro do projeto:

```sh
# Usa o benchmark já compilado; cria novas capturas sem sobrescrever anteriores.
scripts/project-local.sh /usr/bin/python3 scripts/verify-ane-runtime.py

# Revalida somente as evidências desta rodada.
scripts/project-local.sh /usr/bin/python3 scripts/verify-ane-runtime.py \
  --analyze-only --output artifacts/coreml-comparison
```

A ferramenta nativa `tests/cpp/CoreMLComputePlan.mm` compila com
`bash scripts/build-coreml-compute-plan.sh` e verifica o manifesto antes de
consultar o plano. Nenhum driver, serviço ou dependência global foi instalado.
Esta rodada alterou ferramentas de diagnóstico e documentação; o aplicativo
entregue e seu padrão Metal foram preservados. Para usar o caminho confirmado,
abra `artifacts/LocalVoiceTranslator-Diagnostic.app`, pare a sessão e selecione
**Ajustes → Aceleração do reconhecimento → Core ML + Metal · CPU/ANE**.

A confirmação cobre o encoder no benchmark de produção. Não certifica uma
chamada ao vivo, o microfone, o comportamento térmico prolongado ou uso de ANE
em tradução/TTS. A opção CPU/ANE continua experimental e não superou o padrão
Metal no conjunto de velocidade/precisão medido anteriormente.

## Demais etapas e MLX

| Etapa | Decisão nesta entrega | Evidência / limite |
|---|---|---|
| Captura, VAD, playback | CPU | Operações pequenas e prazo do callback; inferência continua fora do callback |
| Reconhecimento | Metal padrão; Core ML + Metal selecionável | Medição nativa acima |
| Embeddings ECAPA | CPU no app; Core ML candidato | Sondagem sintética: mediana 19,43 → 12,83 ms |
| Overlap Pyannote | CPU no app | Sondagem: mediana 30,69 → 28,57 ms, P95 31,53 → 32,59 ms |
| Separação Conv-TasNet | CPU no app | Sondagem: mediana 300,52 → 307,08 ms; grande diferença numérica de saída |
| Tradução | CTranslate2 INT8/Ruy CPU | Backend atual não oferece Metal/ANE; trocar exige integração e validação próprias |
| TTS | Sherpa/Piper CPU | Sem migração de backend validada nesta entrega |

A sondagem ONNX usou **ORT 1.19.2 do ambiente de conversão**, diferente do
runtime nativo 1.27.1. Usou tensores sintéticos e 30 execuções por configuração,
com profiling ligado em ambas. Confirmou partições Core ML e CPU, mas não
identificou o hardware interno escolhido pelo Core ML. Não é validação de
identidade vocal, SI-SDR, qualidade de separação ou ganho no pipeline real.
Detalhes em `artifacts/coreml-comparison/onnx-probe/summary.json`.

MLX não foi adicionado ao runtime nem medido: é uma alternativa CPU/GPU, não
um acesso ao ANE. A integração aqui usa o Metal já existente. Não se afirma
que essa escolha supera todas as implementações MLX. A promoção de embeddings
para Core ML ainda exige runtime nativo compatível, validação com áudio humano,
cancelamento, pressão de memória e comparação no pipeline completo.

## Verificação e reprodução

```sh
# Restore de desenvolvimento, com hashes; não executado pelo app.
scripts/project-local.sh /usr/bin/python3 scripts/fetch-coreml-model.py
scripts/build-whisper.sh
scripts/build-asr-benchmark.sh
scripts/test-coreml.sh --inference
scripts/project-local.sh /usr/bin/python3 scripts/compare-asr-backends.py
scripts/build-production-test.sh tests/cpp/ConversationGolden.cpp build/coreml-conversation-golden
scripts/project-local.sh /usr/bin/python3 scripts/test-asr-acceleration-pipeline.py
scripts/run-tests.sh
```

O build do Whisper resolve o SDK atual e recria o cache CMake para evitar
referências ao SDK removido pelo upgrade do Xcode. Duas propriedades SwiftUI
usam armazenamento `State` explícito, equivalente ao backing storage anterior,
evitando a macro nova do SDK 27 que tenta aplicar um sandbox aninhado. A
contenção de escrita do projeto e o App Sandbox permanecem habilitados.

Testes específicos: 11 verificações de integridade/seleção com ASan/UBSan;
36 transcrições com janelas crescentes comparadas ao controle; 12 casos STOP
e 12 revisões obsoletas com recuperação; teste Swift de mudança de backend
apenas com sessão parada. Resultados do pipeline ficam em
`artifacts/coreml-comparison/pipeline/summary.json`; a aprovação física, sessões
longas da revisão atual e teste com rede desligada continuam separados.

Validação concluída nesta revisão:

- Build Debug e Release de diagnóstico: PASS; assinatura ad-hoc e auditoria
  estática de rede: PASS. Modelos do bundle: 13 grupos / 2.187 arquivos verificados.
- 56 verificações C++ com ASan/UBSan e 16 testes XCTest: PASS.
- 11 verificações específicas de backend e a comparação/recovery do encoder
  descrita acima: PASS.
- Pipeline WAV completo nos quatro backends: PASS, com traduções e reprodução
  sem duplicações. Ruído com Metal: PASS nesta execução. O erro de estabilidade
  registrado na revisão anterior não foi corrigido diretamente por esta mudança;
  um passe isolado não estabelece sua resolução em sessões longas.
- Chamada simulada nos quatro backends, dois caminhos simultâneos e contexto
  Whisper compartilhado: PASS (`artifacts/coreml-comparison/duplex/summary.json`).
- Abertura da interface via LaunchServices: PASS, sem iniciar microfone. Não houve
  nova captura de tela validada; o screenshot antigo não conta como evidência atual.

Aplicativo: `artifacts/LocalVoiceTranslator-Diagnostic.app`. A versão pessoal
anterior em `artifacts/LocalVoiceTranslator.app` foi preservada. Para testar,
abra o diagnóstico, mantenha a sessão parada e escolha **Ajustes → Aceleração do
reconhecimento**. O relatório desta rodada está em
`artifacts/coreml-comparison/validation.json`; ele não libera os gates de release
completo, nem substitui testes físicos, offline e de 30 minutos da revisão atual.

**Limite adicional verificado em 27/09:** os cancelamentos acima eram imediatos
ou após mel. Um novo ensaio de interrupção dentro de `whisper_full` e troca de
caminho encontrou diferenças na transcrição seguinte/retomada. A prioridade com
interrupção não foi ligada ao aplicativo. Veja [ASR_PREEMPTION.md](ASR_PREEMPTION.md)
para controles A/A, resultados reprovados e critérios de recuperação do estado.

## Fontes técnicas

- [Apple: interpretar Core ML e Neural Engine no Instruments](https://developer.apple.com/videos/play/wwdc2022/10027/)
- [Whisper.cpp: encoder Core ML](https://github.com/ggml-org/whisper.cpp#core-ml-support)
- [Core ML: unidades de execução e especialização](https://apple.github.io/coremltools/docs-guides/source/model-prediction.html)
- [ONNX Runtime: Core ML Execution Provider](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html)
- [MLX: CPU/GPU e memória unificada](https://mlx-framework.org/)
- [CTranslate2: hardware suportado](https://opennmt.net/CTranslate2/hardware_support.html)
