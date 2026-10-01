# Confirmação quando uma vírgula se torna ponto

27/09/2026. Experimento posterior a [BOUNDARY_EVIDENCE.md](BOUNDARY_EVIDENCE.md).
**Desativado no aplicativo. O candidato reprovou a estabilidade em uma das
30 gravações PT. Os quatro estágios terminaram (120 execuções); o candidato EN
tem atribuição inválida no runner e exige nova medição após a correção.**

## Causa da espera extra no áudio limpo

No trace limpo, a revisão 3 já continha “Vamos começar a reunião, eu preciso.”.
A revisão 4 preservava as palavras e trazia “Vamos começar a reunião. Eu preciso
de 5.”. A condição experimental anterior exigia repetir o mesmo delimitador,
além dos acordos lexicais e da confiança do ponto atual. Por isso aguardava a
revisão 5, uma janela depois, mesmo na variante de evidência atual.

O novo parâmetro interno `allowCommaToPeriodBoundary` permite essa transição
quando a evidência é atual, com limiar não nulo. Não dispensa repetição das
palavras, duas palavras seguintes estáveis, tempo mínimo de áudio novo,
identificação do participante nem confiança atual da pontuação. Não permite
usar pergunta, reticências ou ausência de pontuação como vírgula anterior.
Também corrigimos o reconhecimento de reticências na condição experimental:
o último caractere `.` sozinho não prova que o delimitador anterior era ponto.

O comportamento padrão continua com limiar zero e o novo parâmetro falso.
Esta mudança não corrige erros semânticos do resultado final e não calibra as
probabilidades do Whisper como probabilidades de acerto da frase.

## Comparação concluída

Mesmo modelo, WAVs e política de produção; candidato com limiar 0,3,
`consecutiveBoundaryEvidence=false` e `allowCommaToPeriodBoundary=true`.
Duas repetições de cada cenário, invertendo controle/candidato na segunda.
Sem compilações nem outras inferências concorrentes durante as medições.

| Cenário | Controle atual | Candidato |
|---|---|---|
| Ruído, duas execuções | Duas divergências após confirmar “reunir” | Zero divergências; manteve “reunião” |
| Limpo, duas execuções | Passou | Passou, primeira confirmação na mesma revisão 4 |
| Primeiro áudio PT limpo, execução 1 | 4.282 ms desde a captura | 4.252 ms desde a captura |
| Primeiro áudio PT limpo, execução 2 | 4.265 ms desde a captura | 4.278 ms desde a captura |

As diferenças limpas foram **−30 e +13 ms**, sem a janela adicional de cerca de
750 ms observada no experimento anterior. Esses poucos resultados não comprovam
melhora geral de velocidade nem autorizam percentis. A resposta EN limpa variou
−35 e −5 ms em relação aos controles correspondentes.

No ruído, o candidato aguardou o resultado final da primeira fala: primeiro
áudio em 6.825 e 6.791 ms desde a captura, ou 1.040 e 1.007 ms depois do fim
estimado pelo VAD. Essa espera preservou a palavra correta naquele caso.
O tempo desde a captura inclui a duração da fala; não é tempo puro de cálculo.
Não se trata de medição acústica nem de conversa humana ao vivo.

Evidências: `artifacts/boundary-evaluation/comma-promotion/summary.json`, com
oito execuções, hashes de fontes/binário/WAV, eventos e resultados. Os quatro
candidatos passaram; os dois controles com ruído falharam como esperado.

## Validação e próximos critérios

Passaram 33 testes de evidência de encerramento com ASan/UBSan, além dos testes
existentes de temporização (21), cache de voz (10), política de streaming (24)
e árbitro (18). A nova proteção contra tratar reticências como ponto foi
identificada pelo teste e corrigida antes de reconstruir o executável nativo.
Os quatro testes Python dos gates de aceitação também passaram.

O runner de corpus humano agora oferece a comparação específica entre padrão
atual e este candidato, preserva os configs nos artefatos, recusa sobrescrever
evidências existentes e reprova execução incompleta. O corpus reservado não é
usado para ajustar a condição.

```sh
scripts/build-production-test.sh tests/cpp/ConversationGolden.cpp build/boundary-conversation-golden
scripts/project-local.sh /usr/bin/python3 scripts/compare-boundary-evidence.py \
  --output artifacts/boundary-evaluation/comma-retest --threshold 0.3 \
  --repeats 2 --current-boundary --allow-comma-to-period

scripts/build-production-test.sh tests/cpp/HumanCorpusEvaluation.cpp build/comma-human-corpus
scripts/project-local.sh /usr/bin/python3 tools/diagnostics/run-human-corpus.py \
  --comparison comma-boundary --binary build/comma-human-corpus \
  --limit 30 --tag comma-boundary-20260927
```

A etapa humana compara 30 gravações de desenvolvimento por idioma em cada
configuração: 120 execuções de gravações. O relatório distingue falhas técnicas,
revisões, WER e textos por gravação. Variações no silêncio entre gravações e
histórico do ASR impedem interpretar pequenas diferenças como efeito causal
isolado da política. A revisão de sentido e anotação acústica permanecem
pendentes, mesmo se os testes funcionais passarem.

A promoção exige também corpus reservado com política congelada, streaming útil
em fala contínua, chamadas duplex, memória e cancelamento, além da aceitação
física/offline. O aplicativo de diagnóstico entregue anteriormente não foi
substituído nesta etapa. A meta de 45–75 ms continua não demonstrada.

## Revisão por gravação

`tools/diagnostics/review-human-comparison.py` compara os arquivos do lote sem
executar inferência. Confere hashes e referências antes de formar pares, mantém
as falhas e revisões por gravação e destaca diferenças de texto, tradução,
negações e números escritos. As marcações lexicais são pedidos de revisão,
não julgamentos de equivalência semântica. Números por extenso, por exemplo,
podem diferir dos dígitos sem alterar o sentido.

```sh
# Durante a execução: captura somente linhas completas do JSONL.
scripts/project-local.sh /usr/bin/python3 tools/diagnostics/review-human-comparison.py \
  artifacts/latency-policy/human-comma-boundary-20260927 --allow-incomplete \
  > artifacts/latency-policy/human-comma-boundary-20260927/review-live.json

# Depois de finalizar: sem a opção, retorna código 2 se faltarem resultados.
scripts/project-local.sh /usr/bin/python3 tools/diagnostics/review-human-comparison.py \
  artifacts/latency-policy/human-comma-boundary-20260927 \
  > artifacts/latency-policy/human-comma-boundary-20260927/review-final.json
```

`OPEN_TRACE` descreve o arquivo sem resumo terminal; não prova que um processo
esteja ativo. Essa verificação deve consultar o processo responsável. Mesmo
um relatório completo mantém `meaningAcceptance=PENDING`. Dez testes cobrem
UTF-8 parcial, JSON inválido, duplicações, registros faltantes, incompatibilidade
de entradas, regressões, negação por idioma, trechos filtrados e separação entre
sucesso funcional e revisão de sentido, relógio ausente, um primeiro áudio por
gravação e limiar de 30 amostras para publicar percentis.
Evidência: `artifacts/human-comparison-tests.log`.

O controle português concluiu as 30 gravações com zero falhas técnicas e WER
agregado de 8,24%. Isso **não aprova seu sentido**: há “ferver” → “viver”,
“cálcio” → “calço” e perda de “em Great Yarmouth”. Neste último caso, o trace
mostra `direction-filtered` para “and great Yarmouth.”, identificado como EN no
caminho configurado PT. A ausência é, portanto, também um problema de idioma
entre segmentos, independente do teste de pontuação.

O mesmo controle filtrou um “Thank you.” extra que não consta na referência de
outra gravação. Um filtro pode evitar uma hipótese espúria; não deve ser
desativado com base em um único exemplo. O comparador preserva todos os eventos
filtrados/incertos/não suportados para revisão, sem condená-los automaticamente.
A avaliação inglesa completa é descrita ao final; seu candidato tem atribuição
inválida e não pode ser comparado em WER/latência.

O relatório também distingue confirmação parcial (`endpointReason=0`) de
confirmação após silêncio ou limite de janela. No controle PT completo, apenas
3 de 30 gravações tiveram alguma confirmação parcial, embora 20 tenham produzido
som antes de terminar o arquivo WAV. As duas medidas não são equivalentes: um
arquivo pode conter silêncio ou várias frases. O fim do WAV não é anotação
acústica do fim da fala.

O controle PT teve primeiro áudio, medido desde o início do arquivo, em P50
7.312 ms e P95 9.295 ms (30 gravações). Esses valores incluem a fala. Nas 27
gravações cuja primeira unidade veio de corte por silêncio, a média após o fim
estimado pelo VAD foi 1.236 ms; não há P50/P95 para esse subconjunto inferior a
30. São callbacks de software sem medição de saída física.

Na comparação parcial, a gravação `pt_br-12857830854717968324.wav` teve aumento
de aproximadamente 1.239 ms até o primeiro áudio com o candidato. O controle
confirmou um prefixo com ponto e depois a continuação “antes de…”; o candidato
esperou a frase completa com vírgula, correspondente à estrutura da referência.
Isso demonstra por que o resultado sintético limpo não basta para generalizar
ausência de espera extra. Não há decisão de promoção enquanto o lote completo
e a revisão conjunta de sentido/tempo estiverem pendentes.

## Resultado português completo: candidato não aprovado

O controle completou 30 gravações com zero falhas técnicas; o candidato completou
30 com uma falha de estabilidade (`revision-preserved` e `confirmed-text-revised`).
O WER agregado passou de 8,24% para 7,38%, mas o menor erro médio não remove essa
falha nem aprova o sentido. O footprint máximo amostrado foi 1,78 GB no controle
e 1,65 GB no candidato; ambos são do processo de teste, não uma chamada física.

Na gravação `pt_br-15365693344391469533.wav`, o candidato confirmou
“Considera-se que elementos como calça e potássio são metais.”. A hipótese final
começou com “Considere-se…”, divergindo do prefixo já confirmado. A continuação
foi preservada sem repetir a primeira cláusula, mas o gate corretamente não
transformou a recuperação em aprovação. O controle desse lote não teve revisão
posterior nesse trecho; ainda assim transcreveu “calço” no lugar de “cálcio”.
Ambas as traduções contêm erro de sentido em relação à referência humana.

O caso evidencia que confiança na pontuação não garante estabilidade das palavras
dentro da cláusula. O experimento de vírgula/ponto não corrige essa limitação do
critério de confirmação. A diferença observada não isola causalidade da opção:
os cortes e históricos de inferência também variam no replay de produção.
Não se deve afrouxar o gate nem ajustar o limiar a esta frase para aprovar o lote.

Decisão: **não promover o candidato**. A coleta EN terminou; o defeito de
atribuição descrito abaixo impede uma comparação válida desse idioma.
Não altera a política ou o executável do app.
Relatórios PT: `artifacts/latency-policy/human-comma-boundary-20260927/`, arquivos
`pt-control-result.json`, `pt-comma-boundary-result.json` e respectivos JSONLs.

## Revisão EN e falha de drenagem do runner

A revisão paralela encontrou um problema de medição independente da política:
o runner considera as filas vazias como indício de término, mas não verifica
uma tradução já retirada da fila e ainda em execução. A igualdade entre unidades
traduzidas e reproduzidas também é verdadeira quando os dois conjuntos estão
vazios. Os 500 ms sem eventos não tornam essa condição suficiente.

No candidato EN, as 30 gravações terminaram e o relatório bruto registrou uma
falha `no-translated-speech` para `en_us-4844570914389909899.wav` (Haiti).
**A fala não desapareceu do pipeline**: ela foi reconhecida corretamente,
traduzida e reproduzida, mas os eventos posteriores foram contabilizados na
gravação seguinte, sobre rumores. A cronologia prova essa atribuição incorreta:

| Evidência no JSONL do candidato EN | Valor |
|---|---|
| Intervalo do WAV Haiti, em frames de 16 kHz | [4.624.320, 4.742.400) |
| `sourceSegment` / unidade traduzida | 264 / 30 |
| Último frame VAD / fim da janela da unidade | 4.720.000 / 4.724.160 |
| Primeiro frame do WAV seguinte | 4.766.720 |
| Tempo da tradução da unidade Haiti | 2.270,818 ms |
| Primeiro áudio após fim estimado pelo VAD | 3.411,905 ms |

As linhas 557–558 registram reconhecimento e início da tradução ainda como
Haiti; a linha 559 encerra seu resultado sem tradução. As linhas 560–564 e 577
registram tradução, síntese e reprodução da mesma unidade 30 / fonte 264 sob
o nome do WAV seguinte. A tradução terminou cerca de 116 ms depois do início
da captura desse próximo WAV. Todos os frames de origem da unidade pertencem
ao Haiti e antecedem a captura seguinte. Isso não representa o ASR copiando o
texto de uma conversa para outra: o nome do arquivo no evento é atribuído pelo
runner no instante de consumo.

Consequências para esta avaliação:

- Preservar os JSONLs, o `FAIL` e o WER bruto de 15,18%; esse WER por gravação
  está contaminado pela omissão em uma referência e inserção na seguinte e
  **não deve ser usado para comparar a precisão do candidato**.
- Corrigir a detecção de trabalho em andamento e testar o runner antes de uma
  nova medição. Reatribuir textos retrospectivamente não constitui um novo
  resultado funcional aprovado.
- Desconsiderar o primeiro áudio atribuído a rumores: o primeiro evento ali é
  áudio do Haiti. O atraso real dessa tradução e a espera de reprodução da
  fala seguinte continuam sendo dados úteis para investigar o pipeline.
- Manter a reprovação PT por revisão de texto já confirmado; a falha do runner
  EN não apaga esse resultado independente.

O levantamento por `sourceSegment` encontrou seis eventos entre gravações,
todos dessa unidade EN; nenhum nos dois lotes PT completos ou no controle EN
completo, com 30 gravações. O artefato
`artifacts/boundary-evaluation/comma-promotion/source-chronology-review.json`
preserva linhas, clocks, frames e hashes dos snapshots sem modificar os brutos.

Na revisão dos primeiros 13 pares EN disponíveis, controle e candidato não
tinham falhas técnicas nas mesmas gravações, mas ambos reconheceram 11:45 onde
a referência registra 11:35, reproduzindo 23h45. No caso da aurora, o controle
produziu “Aurora Borealis as the sky”, enquanto o candidato produziu “Aurora
Borreles as the The sky”. Esses casos exigem revisão de sentido; não isolam um
efeito causal da opção de pontuação. Não há promoção do candidato nem aceitação
semântica, acústica ou física neste relatório.

## Consolidação dos quatro estágios

O lote original encerrou com código 1. Todos os quatro estágios têm 30 resultados
e resumo terminal; `nativeRunsComplete=true`. A revisão com proteção de
atribuição mantém `comparisonComplete=false`: terminar os arquivos não torna a
comparação válida.

| Estágio | Resultado nativo | Elegibilidade da atribuição | WER |
|---|---|---|---|
| PT controle | PASS, 30 gravações | Sem cruzamento detectado | 8,24% |
| PT candidato | FAIL, uma revisão após confirmar | Sem cruzamento detectado | 7,38% |
| EN controle | PASS, 30 gravações | Sem cruzamento detectado | 6,47% |
| EN candidato | FAIL, uma gravação sem tradução atribuída | INVALID_ATTRIBUTION | Não comparável; bruto 15,18% preservado |

O controle EN não confirmou nenhuma unidade durante parcial neste lote. Primeiro
áudio desde o início do WAV: P50 6.753 ms e P95 8.358 ms, incluindo a duração da
fala. As 30 primeiras unidades vieram de corte por silêncio; após o fim estimado
pelo VAD, P50 1.161 ms e P95 1.797 ms. São timestamps de software, não saída
acústica. Esses valores descrevem o controle; os tempos agregados do candidato
EN foram retirados da comparação devido à atribuição inválida.

`review-human-comparison.py` agora sinaliza uma unidade quando seu
`windowEndFrame` conhecido/não nulo é menor ou igual ao `captureFirstFrame`
conhecido/não nulo da gravação atribuída. Um `firstFrame` anterior, sozinho,
pode ser preroll legítimo e não gera essa reprovação. Preserva os eventos e
resultados originais em `nativeStatus`, `rawSourceWER` e `rawTiming`; não
redistribui textos nem converte a falha em sucesso. Suprime WER e distribuição
de tempo do lote contaminado e comparações por par desse lote, pois a gravação
anterior também pode ter uma omissão artificial. Passaram 14 testes Python,
incluindo unidade tardia, preroll e proteção do resultado nativo.

Relatórios consolidados: `review-live.json` no diretório do lote original e
`artifacts/boundary-evaluation/comma-promotion/attribution-safe-review.json`.
O nome histórico `review-live.json` não indica processo ativo; estes arquivos
foram atualizados após o encerramento. Nenhum JSONL ou resultado nativo foi
reescrito.

A correção do runner registra unidades aceitas em
`CorpusDrainTracker` desde `recognized`/início das etapas até `played` ou erro
terminal. Isso impede drenar enquanto uma tradução ou síntese observada está
em execução com sua fila vazia. Também registra `firstFrame` e denuncia eventos
de janela anterior. A lacuna entre retirar um resultado ASR e publicar sua
aceitação também foi tratada: o coordenador confirma cada marcador final
`utterance-end` depois dos eventos correspondentes, e o runner exige
`coordinatedUtterances == asr.processed == vad.segments`, fila de eventos vazia
e ledger sem unidades pendentes. A janela de silêncio permanece como proteção
adicional, sem substituir essas condições explícitas.

Passaram 13 verificações do ledger e 15 do coordenador com ASan/UBSan, incluindo
uma pausa controlada de 550 ms depois de retirar um resultado sem publicá-lo.
O smoke corrigido Haiti + rumores passou 2/2, sem eventos entre arquivos; ele
usou estado ASR novo e não substitui o lote antigo. A transcrição do Haiti nesse
smoke omitiu “since the 1800s”, portanto o PASS funcional não aprova o sentido.
Havia compilação externa do CicloLivre no início da execução, registrada na
proveniência: seus tempos não são uma comparação isolada de latência.
O smoke PT de controle também terminou 2/2, conservando o erro semântico
conhecido “ferver” → “viver”.

Detalhes em [CORPUS_DRAIN_FIX.md](CORPUS_DRAIN_FIX.md). O candidato de pontuação
continua desativado, o lote original mantém EN inválido e PT reprovado, e a
coleta antiga não é retroativamente validada pela correção.
