# Avaliação das políticas de streaming

12/09/2026 — implementação em avaliação. As metas finais L0–L8 continuam em
[LATENCY_STUDY.md](LATENCY_STUDY.md). **A combinação adaptativa foi selecionada como
padrão de desenvolvimento** e passou pela bateria automatizada completa. O aplicativo e
DMG publicados ainda não foram substituídos por essa revisão.

## O que mudou no pipeline

`StreamingPolicy` permite comparar a cadência, o número de acordos e o tempo de
estabilidade sem criar um pipeline alternativo. A configuração candidata usa:

- Intervalo mínimo de 750 ms, arredondado à grade de áudio de 20 ms. A admissão
  aumenta o intervalo conforme o custo observado de diarização/ASR e a quantidade de
  caminhos compartilhando Whisper, deixando uma margem estimada de 30%. Isso não
  substitui a medição real de fila nem comprova a meta de espera do ASR.
- Dois acordos, pelo menos 600 ms de áudio novo, término de oração e duas palavras
  seguintes repetidas. Reprocessar o mesmo áudio não aumenta a evidência; o fim da
  janela é específico de cada fonte, mesmo quando outra fonte continua crescendo.
- Texto provisório pode acumular estabilidade enquanto o participante é desconhecido,
  mas não é falado até a identidade ser confirmada. Um final ainda pode sair como
  participante não identificado, preservando a capacidade e os erros explícitos.
- Embeddings podem excluir somente as bordas sem fala apontadas pela segmentação.
  O áudio do ASR mantém seu contexto completo. Não foram reduzidos os limiares de
  similaridade, criação ou atualização de pessoas. A admissão exige duas partes
  distintas com fala suficiente; não usa pitch, volume ou gênero.

Os novos contadores mostram parciais publicadas/substituídas, trabalho obsoleto e
unidades sem transcrição. O rastreamento integral de todo cancelamento ainda não foi
concluído. Arbitragem entre os caminhos e antecipação de ASR em pausas curtas permanecem
etapas separadas. O corte do VAD continua com a política anterior.

## Decisões sustentadas pelos ensaios

Uma cadência menor, isoladamente, não venceu o controle: no primeiro WAV o PT passou
de aproximadamente 6,72 s para 7,70 s até a primeira voz, enquanto o EN melhorou.
Mais hipóteses também mudaram pontuação e números, atrasando a confirmação.

Os primeiros candidatos de confirmação rápida falavam com ID desconhecido e podiam
trocar a voz quando a pessoa era identificada. **Esses resultados foram rejeitados**
para adoção, apesar dos testes funcionais existentes aprovarem a reprodução. Foi
acrescentada uma verificação que rejeita áudio provisório sem participante identificado.

O diagnóstico de embeddings mostrou que retirar bordas sem fala antecipava a
corroboração nos dois exemplos sintéticos, mantendo o limiar de 0,68. A configuração
com esse ajuste foi então testada no pipeline completo. A regressão humana de uma
voz com volume variável passou com e sem o ajuste.

## Medições exploratórias da combinação

Valores em segundos, da origem do segmento capturado até o início de PCM com energia
sustentada. Incluem fala e preroll; **não são espera após a última palavra**.
Execuções em sequência, mesmos arquivos e pipeline, sem microfone nem aplicativo de reunião.

| Cenário | Controle | Candidato |
|---|---|---|
| Conversa sintética PT / EN | 6,94 / 7,74 | 4,75 / 5,20 |
| Fala com ruído, três trechos | 6,93 / 4,68 / 8,68 | 4,62 / 4,04 / 4,34 |
| Três participantes e retorno | 6,74 / 7,45 / 8,25 / 8,76 | 5,69 / 5,10 / 5,36 / 6,51 |
| Golden humano anterior, três trechos | 7,70 / 4,34 / 3,36 | 7,70 / 4,38 / 3,45 |

No último caso não houve ganho demonstrado. Todos esses ensaios funcionais passaram;
o teste de três participantes manteve três identidades. As amostras são insuficientes
para publicar percentis ou aprovar L1–L3. Fonte:
`artifacts/latency-policy/trim/experiment-results.json`, com configurações e hashes.

O duplex local também passou nas duas configurações, mantendo captura durante
reprodução e filtragem de direção. Isso não comprova uso físico em Meet/Zoom/Discord.
Artefatos: `artifacts/latency-policy/call-control.jsonl` e `call-fast-trim.jsonl`.

## Corpus humano e critérios pendentes

Foram extraídas dos arquivos locais já verificados 120 gravações humanas FLEURS
CC-BY-4.0: 60 por idioma, com 30 frases de desenvolvimento e 30 reservadas por idioma.
Os pares PT/EN ficam sempre no mesmo conjunto; as frases do corpus anterior foram
excluídas. Não houve download nem geração por TTS. Manifesto e hashes:
`tests/data/latency-human/manifest.json`.

O corpus fornece textos e referências paralelas de tradução, mas não fornece IDs
confiáveis de participantes nem limites acústicos anotados. Portanto não comprova a
cobertura de seis participantes exigida pelo estudo, nem transforma fim VAD em verdade
acústica. O conjunto reservado não será usado para escolher a configuração.

`HumanCorpusEvaluation.cpp` executa o mesmo `AudioCore`, captura/reproduz PCM em tempo
real e espera a saída anterior terminar antes de iniciar a próxima gravação. Registra
transcrição, tradução, identidades, perdas e timestamps. O avaliador distingue erros
funcionais de WER e mantém revisão de sentido/latência acústica como pendentes.

A triagem inicial usa seis gravações novas por idioma. Antes de adotar o candidato,
comparar WER, cobertura, números, nomes e negação; depois avaliar a configuração
congelada no conjunto reservado. Ainda faltam os ensaios finais de 30 minutos,
anotações acústicas, revisão de sentido e validação física/offline.

## Achados da primeira triagem humana

Na primeira execução com seis gravações por idioma, o WER em PT foi 6,19% no
controle e 7,22% no candidato; em EN, 5,81% em ambos. O candidato produziu “Nada
reservada” onde a referência diz “Na área reservada”. Essa diferença de sentido
impede aprovar o candidato com base apenas na velocidade ou no WER agregado.

Essa comparação precisava de um controle adicional: esperar o fim da reprodução
entre arquivos gerou durações diferentes de silêncio nas duas configurações. Um
probe com o mesmo WebRTC/VadSegmenter reproduziu cortes distintos usando apenas
esses históricos de silêncio, sem ASR/TTS. Na última gravação, um histórico fechou
a janela em 7,98 s; o outro dividiu em 5,20 s e 8,14 s. Isso não permite atribuir
automaticamente a diferença de texto apenas à nova política de confirmação.

Foram preservados os resultados iniciais. Um novo replay fornece **o mesmo PCM
completo, incluindo todos os silêncios**, às duas configurações. Evidências:
`artifacts/latency-policy/fixed-vad-*.jsonl` e
`artifacts/latency-policy/fixed-pt-fast-trim/`. Nesse replay, ambos passaram pelos
critérios funcionais e tiveram WER de 7,22%, reconhecendo as mesmas palavras, com
diferenças de pontuação. Ambos produziram “Nada reservada”. Portanto, o primeiro
ensaio não isolava uma regressão causada pelo candidato; o erro de sentido continua
existindo e precisa ser tratado. Esse resultado não aprova a qualidade semântica.

Também foram corrigidos dois problemas do avaliador humano: confundir a drenagem
do PCM com a chegada assíncrona do evento de fim, e exigir filas de entrada sempre
vazias enquanto a captura continuava ativa. O teste curto após as correções passou
nos dois idiomas e políticas. As primeiras mensagens de `incomplete-playback` em
EN eram desse avaliador; não comprovavam perda de áudio no aplicativo.

`StreamingPolicy::production()` agora seleciona intervalo mínimo de 750 ms, dois
acordos, 600 ms de evidência nova, cadência adaptativa e análise de embeddings sem
bordas silenciosas. Os executáveis de validação usam esse mesmo padrão quando não
recebem configuração explícita; o controle permanece disponível para comparações.

A combinação passou pela bateria automatizada completa em 12/09/2026 às 16:49 UTC:
246 verificações, quatro goldens e sete cenários, incluindo os 24 novos testes de
política com ASan/UBSan. Evidência com hashes:
`artifacts/latency-policy/checkpoint-acceptance.json`.
Os dois ensaios de 30 minutos continuam STALE, pois pertencem à revisão anterior.
Esse resultado não substitui os critérios acústicos/semânticos, a validação física
nem o teste com rede desligada. O pacote publicado permanece na revisão anterior.
