# Estados privados do Whisper e concorrência

27/09/2026. Continuação de [ASR_STATE_RECOVERY.md](ASR_STATE_RECOVERY.md).

**Decisão: manter o reconhecimento serializado no aplicativo.** A concorrência
melhorou o tempo para concluir duas solicitações simultâneas, mas piorou a mediana
da resposta portuguesa. Não demonstrou menor atraso até a voz traduzida.

## Implementação

`WhisperASR` agora mantém pesos com propriedade compartilhada e um estado nativo
de inferência próprio. `forkState()` cria outro estado sobre os mesmos pesos.
Cada estado tem seus buffers, resultados e histórico de amostragem. O estado
derivado mantém os pesos e o caminho do encoder Core ML válidos mesmo após a
destruição do objeto original. Inferência e leitura dos resultados usam as APIs
explícitas de estado do Whisper.

Os workers do aplicativo continuam compartilhando **um único estado com acesso
exclusivo**. Nenhum worker chama `forkState()`. A nova API permite experimentar
concorrência sem mudar a amostragem ou duplicar os pesos. Cada instância ainda
exige um único proprietário da inferência; não permite duas chamadas simultâneas
no mesmo estado. Uma futura integração deve reservar memória antes de criar o
segundo estado.

## Resultados

Modelo small atual, 12 gravações humanas FLEURS verificadas por hash, seis por
idioma. Mesma sequência e aquecimento em cada caminho nas condições serial e
concorrente. Metal: cinco repetições, 30 solicitações por caminho/condição.
Core ML: uma repetição, seis por caminho/condição. Execuções dos backends em
sequência, sem compilações concorrentes.

- **60/60 transcrições** do wrapper padrão coincidiram exatamente em texto e
  idioma com os registros anteriores à mudança de propriedade do estado.
- **96/96 comparações** entre estados privados seriais e concorrentes coincidiram:
  60 Metal, 12 Core ML CPU/ANE, 12 CPU/GPU e 12 automático.
- Nos quatro modos, cancelar um estado não alterou a transcrição seguinte do
  outro. O estado derivado continuou funcionando após destruir o original.
- O problema de retomada **dentro do próprio estado cancelado** documentado no
  estudo anterior não foi corrigido por esta separação.

Tempos Metal, da solicitação ao resultado, incluindo a espera pelo árbitro:

| Medida | Estados privados seriais | Estados privados concorrentes |
|---|---:|---:|
| PT P50 / P95 | 361 / 563 ms | 407 / 452 ms |
| EN P50 / P95 | 393 / 563 ms | 388 / 470 ms |
| Conclusão do par P50 / P95 | 525 / 565 ms | 434 / 470 ms |
| ASR sem fila, média PT / EN | 255 / 261 ms | 403 / 410 ms |
| Maior footprint observado | 1,05 GB | 1,10 GB |

Os percentis usam interpolação linear inclusiva. O footprint foi amostrado ao
carregar os estados e entre rodadas; não representa pico instrumentado de todas
as alocações. São recursos do processo de teste, não do aplicativo completo.

A espera explícita praticamente desapareceu na condição concorrente, mas cada
inferência levou mais tempo. Isso é compatível com disputa pelos recursos de
execução: eliminar a fila do árbitro não elimina o custo das tarefas. Há ganho
para concluir o par e reduzir as caudas deste ensaio, sem ganho uniforme na
resposta individual. O desenho sempre executa a condição serial primeiro;
não permite atribuir pequenas diferenças exclusivamente à concorrência.

Média da solicitação ao resultado nos ensaios curtos de Core ML:

| Encoder configurado | Serial PT / EN | Concorrente PT / EN |
|---|---:|---:|
| CPU/ANE | 404 / 414 ms | 425 / 425 ms |
| CPU/GPU | 352 / 358 ms | 369 / 377 ms |
| Automático | 403 / 412 ms | 434 / 402 ms |

Não publicamos P50/P95 desses ensaios com seis amostras por caminho. Todos
mantêm o decoder Metal. A configuração CPU/ANE não constitui nova medição de
uso físico do ANE; a instrumentação anterior está em
[APPLE_ACCELERATION.md](APPLE_ACCELERATION.md). Não foram testados backends
diferentes entre os dois caminhos nesta rodada.

## Reprodução e critérios seguintes

```sh
bash scripts/build-asr-private-state-test.sh
scripts/project-local.sh build/asr-private-state-tests metal 5
scripts/project-local.sh build/asr-private-state-tests coreml-ane 1
scripts/project-local.sh build/asr-private-state-tests coreml-gpu 1
scripts/project-local.sh build/asr-private-state-tests coreml-all 1
scripts/project-local.sh /usr/bin/python3 scripts/summarize-asr-private-state.py
```

O último comando consolida os registros já salvos em
`artifacts/asr-private-state/`, confere a equivalência padrão e registra hashes
de fontes, binários, bibliotecas e entradas em `validation.json`. Os quatro
comandos de inferência acima escrevem JSONL na saída padrão; para uma nova
consolidação, salve cada saída no arquivo do backend correspondente. O controle
histórico fica em `artifacts/asr-state-recovery/control-quality.jsonl`.

Antes de promover concorrência:

1. Medir chegadas desencontradas de parciais e finais no pipeline duplex completo;
   exigir melhora de atraso por caminho, sem esconder regressão na média do par.
2. Validar admissão de memória com ASR, diarização, tradução, TTS e separação
   presentes, mantendo o limite de 8 GiB.
3. Comparar sentido, nomes, números e negações em corpus reservado. Igualdade
   entre modos não prova que a transcrição esteja correta; os históricos de dois
   estados também diferem do histórico de um estado compartilhado na chamada.
4. Repetir cancelamento, pressão, sessão longa e validação física/offline.

Os bloqueios de revisão já falada e aceitação física continuam. Este ensaio
não mede captura, confirmação, tradução, síntese nem saída acústica. A meta de
45–75 ms até a tradução falada permanece não demonstrada.
