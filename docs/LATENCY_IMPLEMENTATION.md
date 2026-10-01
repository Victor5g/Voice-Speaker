# Implementação da redução de latência

Atualização: 12/09/2026. **Em andamento; não é uma nova entrega aprovada nem comprova
as metas de latência.** Objetivos e critérios completos em [LATENCY_STUDY.md](LATENCY_STUDY.md).
O aplicativo/DMG em `artifacts` ainda corresponde à entrega anterior; os fontes e o
build Debug possuem as mudanças abaixo. Os ensaios longos da versão anterior não
aprovam estes fontes.

A etapa seguinte está em [STREAMING_EVALUATION.md](STREAMING_EVALUATION.md): políticas
adaptativas, confirmação por tempo e identidade, e comparação com novas gravações
humanas. A combinação adaptativa foi selecionada como padrão de desenvolvimento;
a bateria automatizada dessa revisão passou, sem substituir o pacote publicado.

A mudança posterior de [agendamento do ASR](ASR_ARBITRATION.md) acrescenta prioridade
a finais e ordem FIFO entre os caminhos. Passou por build, sanitizers e duplex curto;
sua bateria completa está em andamento. O checkpoint adaptativo abaixo corresponde
à revisão anterior a esse agendador.

## Mudanças implementadas

- Timestamps monotônicos atravessam VAD, diarização, ASR, confirmação, tradução, TTS
  e reprodução. ASR separa espera, mel, identificação do idioma e decodificação.
- O playback publica início de buffer, início de PCM com energia sustentada e fim,
  preservando sessão/unidade. A medição de início usa dois blocos de 10 ms acima de
  −50 dBFS; é uma convenção instrumental, não um teste perceptual. O áudio não é cortado.
- O painel Engine separa inferência, carga da voz, preparação inicial e filas. Mostra
  a duração de áudio pendente, além da quantidade de trechos.
- O fim estimado é o último frame positivo do VAD, expresso também em frames a 16 kHz.
  Nunca é rotulado como fim acústico anotado. As amostras sem encerramento por silêncio
  não entram nessa estimativa; o analisador publica as exclusões. P50/P95 exigem 30 amostras.
- TTS prepara apenas o primeiro par de vozes no presencial e uma voz de destino por
  caminho na chamada. Preparação ocorre antes de Ready/captura, sem gerar fala de teste;
  seu tempo aparece separadamente. Há cache LRU de no máximo duas vozes por worker.
- As reservas de memória precedem a criação de cada modelo. Pressão libera o cache
  inativo. Uma carga negada pode liberar a outra voz e tentar admissão uma vez;
  nova negativa permanece explícita. Não há substituição de voz nem backend externo.
- STOP invalida PCM imediatamente; os workers liberam cache/modelos durante encerramento.

O cache foi avaliado isoladamente, mantendo os cortes, modelos, identidade vocal,
cadência de 1.500 ms e estabilidade anteriores. Os próximos experimentos poderão
comparar antecipação/cadência com essa alteração separada.

## Evidências desta etapa

Instrumentação A/A: um baseline anterior e duas execuções instrumentadas mantiveram
as mesmas quatro unidades, textos, traduções, participantes e vozes, sem falhas.
Evidência: `artifacts/latency-implementation/instrumentation-comparison.json`.

Cache A/B: mesmo WAV sintético repetido por 60 s, executáveis isolados em sequência,
três ciclos, 12 unidades reproduzidas por execução; ambos PASS. Texto traduzido,
texto original, participantes e seleção de vozes idênticos por unidade.

| Medida | Sem cache de duas vozes | Com cache/preparação |
|---|---:|---:|
| Cargas de modelo durante a fala | 6 | 1 |
| Reutilizações no candidato | — | 11 |
| Preparação inicial das duas vozes | Não antecipada | 1.324 ms |
| Maior footprint amostrado a cada 10 s | 1,476 GiB | 1,664 GiB |
| Pico RSS do processo | 1,569 GiB | 1,717 GiB |

Primeiro buffer desde início do segmento capturado, em ordem das seis falas:

| Fala | Baseline ms | Candidato ms |
|---|---:|---:|
| PT 1 | 7.163,8 | 6.661,1 |
| EN 1 | 7.319,8 | 7.297,6 |
| PT 2 | 7.100,5 | 6.623,1 |
| EN 2 | 7.220,3 | 6.702,9 |
| PT 3 | 8.641,0 | 8.161,6 |
| EN 3 | 7.222,6 | 6.757,6 |

Usando o detector de início de PCM com energia sustentada, os ganhos pareados foram
504,4 / 30,1 / 467,7 / 518,4 / 467,1 / 474,5 ms. Assim, cinco das seis primeiras
vozes ganharam cerca de 0,47–0,52 s neste ensaio; a primeira EN→PT, que precisou
carregar Jeff, não apresentou ganho atribuível ao cache. O ensaio não tem potência
estatística para generalizar esses números.

Essas medidas incluem duração/contexto da fala. **Não são espera após a última palavra**,
nem comprovam L1–L3. Há apenas três falas por idioma; não são publicados percentis.
A primeira voz inglesa já estava preparada; a primeira tradução EN→PT usa Jeff pelo
participante identificado, exigindo a única carga durante fala. A preparação Faber
não elimina essa carga, e essa limitação permanece visível.

Artefatos: `repeat-baseline.jsonl`, `repeat-cache.jsonl`, `cache-stage-summary.json` e
`cache-provenance.json`, todos sob `artifacts/latency-implementation/`.
Os WAVs são fixtures sintéticos, não evidência de qualidade humana.

Testes adicionados: 21 para clocks, corte, onset, duração de fila e STOP; dez para
reutilização, LRU, admissão, pressão, corrupção e liberação de voz. ASan/UBSan PASS.
Build Debug e 48 testes de conversa PASS. Duplex por WAV e 12 ciclos de Start/Stop
(49 verificações de confiabilidade) PASS; maior pedido de STOP 0,012 ms, com teardown
em background. A janela SwiftUI Engine abriu no teste de UI; isso não valida interação
com todos os novos campos nem áudio físico.

Regressão completa da etapa de cache: **222 verificações, quatro goldens e sete cenários PASS**.
`artifacts/latency-implementation/checkpoint-acceptance.json` preserva seu fingerprint.
Na etapa adaptativa posterior, **246 verificações, quatro goldens e sete cenários PASS**;
o checkpoint está em `artifacts/latency-policy/checkpoint-acceptance.json`. Os dois testes de
30 minutos constam **STALE** por pertencerem ao engine anterior; deverão ser refeitos
na combinação final. Os testes físicos e offline continuam pendentes.

## Objetivos ainda a cumprir

| Objetivo | Trabalho restante |
|---|---|
| L0 — Medição completa | Referências acústicas anotadas, rastreamento de todas as hipóteses canceladas/descartadas, contabilidade integral de admissões e exclusões |
| L1 — Resposta após pausa P50 ≤1,2 s / P95 ≤2 s | Antecipação de reconhecimento em pausa; medir pelo fim acústico, por idioma, com ≥30 amostras elegíveis |
| L2 — Corte P50 ≤350 ms / P95 ≤500 ms | Comparar 180/220/260 ms especulativos contra 300 ms; provar retomada sem corte falso nem mudança de sentido |
| L3 — Fala contínua P50 ≤3,5 s / P95 ≤5 s | Cadência adaptativa e confirmação temporal implementadas; faltam contexto/alinhamento nos cortes e aprovação no corpus reservado |
| L4/L5 — Qualidade e participantes | Regressões por idioma, referências críticas e revisão de sentido; identidade e nenhuma duplicação/recursão |
| L6 — Fila ASR P95 ≤250 ms | Arbitragem justa com prioridade a finais, parciais substituíveis, contabilização de trabalho cancelado e teste duplex |
| L7 — <8 GiB e STOP | Revalidar configurações completas, pressão e cancelamento com cada nova combinação |
| L8 — Estabilidade/uso real | Novas sessões de 30 min presencial/duplex na revisão final; testes físicos, chamadas, troca de dispositivo e rede desligada |

Além das 12 gravações humanas anteriores, foram preparadas 120 novas gravações
licenciadas, divididas em desenvolvimento e avaliação reservada por idioma. Ainda
faltam anotações acústicas, revisão de sentido e comprovação dos falantes/sotaques
exigidos pelo estudo; a disponibilidade dos arquivos não equivale à aprovação.
Essa falta não é preenchida com reconhecimento do próprio TTS. A combinação final e
uma nova entrega dependem das regressões e dos critérios acima.
