# Estado da amostragem e retomada do reconhecimento

27/09/2026. Continuação de [ASR_PREEMPTION.md](ASR_PREEMPTION.md).
**Causa isolada nos casos medidos; nenhuma configuração nova promovida ao app.**

Continuação: [estados privados e concorrência](ASR_PRIVATE_STATES.md) avalia
isolamento entre caminhos sem modificar a amostragem. O teste passou em
isolamento, mas a concorrência não foi ativada por piorar a mediana portuguesa.

## Causa e implementação experimental

Na versão fixada do Whisper.cpp, `whisper_sample_token_topk` ordena os logits,
mas seleciona os tokens com `std::discrete_distribution` e `decoder.rng`.
Isso ocorre também na busca com temperatura zero usada pelo aplicativo.
Antes de uma nova transcrição, os geradores dos decodificadores 1 em diante
são reinicializados; o do decodificador 0 conserva o avanço da execução anterior.
Interromper uma parcial muda esse avanço e pode mudar a fala seguinte.

O [fonte oficial do commit fixado](https://github.com/ggml-org/whisper.cpp/blob/2eeeba56e9edd762b4b38467bab96c2517163158/src/whisper.cpp)
foi conferido contra o arquivo local; suas diferenças correspondem ao patch
Core ML/reutilização já registrado. O comportamento da amostragem não foi
introduzido por esse patch.

Experimentos isolados, sem substituir fontes ou bibliotecas usadas pelo app:

1. **Estado novo por inferência**, mantendo os pesos: passou nos seis ensaios da
   primeira gravação. Serviu para investigar a origem; não foi adotado como solução.
2. **Reinicializar somente o gerador 0**: passou em 72 execuções nas 12 gravações,
   incluindo 24 controles A/A e 24 interrupções. Nenhuma transcrição final ou
   retomada divergiu do controle correspondente. Isso isola a amostragem como
   causa suficiente para os casos reproduzidos, sem recriar buffers ou pesos.
3. **Usar os candidatos ordenados com temperatura zero**, com desempate pelo ID
   do token; manter sorteio nas temperaturas positivas: passou nos 18 ensaios das
   três primeiras gravações, incluindo seis interrupções. Ainda falta ampliar
   este ensaio específico aos demais áudios/backends.
4. A mesma busca determinística com **oito candidatos**, em vez de cinco, foi
   avaliada em precisão/tempo; não trouxe benefício de precisão neste conjunto.

A busca da [implementação de referência do Whisper](https://github.com/openai/whisper/blob/main/whisper/decoding.py)
expande candidatos por `topk`. O experimento 3 aplica esse princípio ao ponto de
seleção local; não afirma equivalência completa entre os dois decodificadores.
Não escolhemos outra semente aleatória para fazer uma frase específica passar.

## Precisão e custo medidos

Mesmo modelo small, Metal, idioma automático, seis gravações humanas FLEURS por
idioma e cinco repetições: 30 amostras por idioma/configuração. Aquecimento
excluído. Os blocos de configurações foram executados sequencialmente, sem builds
concorrentes; todos os registros indicaram estado térmico nominal. Repetições
não representam 30 falantes ou 30 frases independentes.

| Configuração | WER médio PT / EN | ASR P50 PT / EN (ms) | ASR P95 PT / EN (ms) |
|---|---|---|---|
| Controle atual | 9,44% / 8,13% | 253 / 239 | 340 / 316 |
| Reinicializar gerador 0 | 8,89% / 9,57% | 254 / 243 | 303 / 325 |
| Candidatos ordenados, cinco | 7,04% / 9,57% | 251 / 238 | 299 / 302 |
| Candidatos ordenados, oito | 7,04% / 9,57% | 272 / 258 | 324 / 336 |

Esses tempos são de ASR isolado e aquecido. Não incluem espera pela fala,
diarização, tradução, síntese, fila ou dispositivo. O desenho não autoriza
atribuir causalidade a pequenas diferenças de tempo. O footprint máximo do
processo ficou entre 832 e 887 MB, não sendo o pico do tradutor completo.

O controle manteve **“Bishkek”** nas cinco repetições desta comparação; todos os
candidatos produziram **“Beech Keck”** nas cinco. Portanto, apesar de a média
portuguesa melhorar, o critério de ausência de novos erros críticos de nomes
não passou. Erros anteriores, como “Aerosmith” → “Aaron Smith”, também continuam.
O WER usa palavras normalizadas por caixa/pontuação; não distingue sozinho
equivalência de números ou gravidade semântica. A revisão humana completa está
pendente.

**Decisão:** não ativar interrupção prioritária nem mudar a amostragem padrão
nesta rodada. A correção da dependência entre execuções e a qualidade da
transcrição são critérios separados. Aumentar a busca para oito candidatos
também não foi adotado: manteve as transcrições e aumentou tempo/memória.

## Ferramentas e reprodução

Os patches ficam em `tools/experiments/asr-state/`. O construtor abaixo aplica-os
a uma cópia do fonte, cria biblioteca e executáveis em uma pasta nova de `build/`
e registra hashes em `provenance.json`. Não modifica o runtime do aplicativo.

```sh
scripts/project-local.sh /usr/bin/python3 scripts/build-asr-state-experiment.py \
  reset-rng --output build/asr-reset-comparison
scripts/project-local.sh build/asr-reset-comparison/preemption-test metal reuse

scripts/project-local.sh /usr/bin/python3 scripts/build-asr-state-experiment.py \
  deterministic-beam --output build/asr-beam-comparison
scripts/project-local.sh build/asr-beam-comparison/preemption-test metal reuse 3
# --beam-size 8 constrói a variante de busca ampliada.
```

`benchmark` recebe os mesmos argumentos de `build/asr-benchmark`. Os resultados
desta rodada estão em `artifacts/asr-state-recovery/`: `rng-all.jsonl`,
`topk-three.jsonl`, `quality-summary.json` e os logs de cada configuração.
Textos nesses arquivos são exclusivamente fixtures de desenvolvimento.

Próximos critérios: validar uma configuração que preserve nomes/números/negações;
repetir a comparação no corpus reservado e nos backends Core ML; depois medir
prioridade no pipeline duplex e o atraso até a voz. A meta de 45–75 ms, as
sessões longas e a aceitação física/offline continuam não demonstradas.
