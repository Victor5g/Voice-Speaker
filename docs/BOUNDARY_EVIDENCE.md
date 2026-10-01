# Evidência de encerramento antes de confirmar uma parcial

27/09/2026. Implementação experimental, **desativada no padrão**. O defeito
documentado em [STABILITY_REVIEW.md](STABILITY_REVIEW.md) continua bloqueando
a aceitação da política padrão; os candidatos abaixo não foram promovidos.

Continuação: [transição de vírgula para ponto](COMMA_BOUNDARY_EVALUATION.md)
removeu a janela extra nas duas comparações limpas e manteve a proteção nas
duas com ruído. A avaliação humana foi ampliada; o novo candidato ainda não
foi promovido.

## Implementação

O Whisper pode repetir uma palavra errada com probabilidade alta. No caso
“reunir” → “reunião”, a sondagem encontrou probabilidades baixas para o token
que adiciona o ponto. Isso motivou uma condição adicional à estabilidade textual,
sem adicionar uma duração fixa de espera nem outra inferência.

- `BoundaryEvidence` reconstrói palavras a partir dos tokens e associa a cada
  encerramento a probabilidade do token que forneceu sua pontuação. Valores
  inválidos, desalinhamento e limites excedidos deixam a evidência indisponível.
  São no máximo 512 palavras, com limite adicional por palavra.
- A coleta ocorre somente quando uma política experimental pede essa evidência
  e somente nas parciais. O padrão não coleta nem armazena os vetores.
- `minimumBoundaryProbability` adiciona uma condição para liberar cláusulas já
  repetidas. Zero preserva a política existente. Pontuação precisa coincidir nas
  hipóteses, e uma cláusula posterior não pode ultrapassar uma anterior incerta.
- `consecutiveBoundaryEvidence` compara duas estratégias: reiniciar a evidência
  do encerramento quando sua probabilidade é insuficiente, ou preservar os
  acordos lexicais e exigir o limite apenas na hipótese atual.
- O resultado final continua podendo confirmar a fala; texto já confirmado
  nunca é reescrito ou reproduzido novamente. STOP e cancelamento permanecem.

**Probabilidade de token não é probabilidade calibrada de acerto semântico.**
Esses limites foram explorados no conjunto de desenvolvimento, não validados em
conversa espontânea. São controles internos dos testes, não novos ajustes do app.

## Comparação medida

Mesmo modelo, PCM, relógio de captura, cadência mínima de 750 ms e dois acordos
textuais; execuções seriais, invertendo a ordem na segunda repetição. Cada
estratégia com limite 0,3 teve duas execuções com ruído e duas sem ruído, mais
seus controles. Os testes exigiram também os termos de referência do áudio.

| Estratégia / limite | Ruído: divergências no candidato | Controle com ruído | Atraso adicional da primeira resposta PT no áudio limpo |
|---|---|---|---|
| Evidência consecutiva / 0,3 | 0 em 2 execuções | Reprovou nas duas | 772 e 775 ms |
| Evidência atual / 0,3 | 0 em 2 execuções | Reprovou nas duas | 733 e 768 ms |

O candidato manteve “reunião” nas quatro execuções com ruído. Nesse início,
aguardou o resultado final; o som saiu aproximadamente **970–1.040 ms depois
do fim estimado pelo VAD**. Os 6,75–6,82 s desde o início da captura incluem a
duração da fala. Não são 6,8 s de processamento nem uma medição acústica.

No áudio limpo, a primeira resposta EN variou de −23 a +66 ms frente ao
controle; essa amostra pequena não permite atribuir ganho. Um primeiro ensaio
com limite 0,5 adicionou cerca de 1,6 s em PT e motivou o teste de 0,3. Não há
P50/P95, significância estatística ou aprovação de qualidade humana nesta comparação.

**Decisão:** manter o padrão. A proteção funcionou no caso conhecido, mas o custo
observado em fala limpa ainda contraria o objetivo de reduzir a espera. A variante
com evidência atual não eliminou esse custo. É necessário ampliar a comparação
e estudar evidência acústica/decodificação antes de promover uma configuração.

A promoção exige comparar qualidade e atraso conjuntamente no corpus humano
reservado, manter streaming útil em fala contínua e cumprir os critérios de
[LATENCY_STUDY.md](LATENCY_STUDY.md), incluindo pelo menos 30 amostras elegíveis
para percentis. Não basta fazer o cenário com ruído passar esperando sempre
pelo fim da fala, nem reduzir milissegundos aceitando palavras divergentes.

## Evidências e reprodução

- `artifacts/boundary-evaluation/threshold-030/summary.json`
- `artifacts/boundary-evaluation/threshold-030-current/summary.json`
- `artifacts/boundary-evaluation/native-probe.jsonl` — sondagem com a classe
  WhisperASR de produção; os cortes não substituem o pipeline VAD/diarização.
- `artifacts/boundary-evidence-tests.log` — 22 verificações com ASan/UBSan.

Os JSONs de comparação preservam hashes do executável, dos fontes e do WAV,
configuração, eventos, textos e resultados. A revisão posterior passou a coletar
probabilidades apenas quando solicitado; os testes com o executável dessa revisão
ficam em `artifacts/boundary-evaluation/final-smoke/`.

```sh
bash scripts/test-latency.sh
scripts/test-coreml.sh --inference
scripts/build-production-test.sh tests/cpp/ConversationGolden.cpp build/boundary-conversation-golden
scripts/project-local.sh /usr/bin/python3 scripts/compare-boundary-evidence.py \
  --output artifacts/boundary-retest --threshold 0.3 --repeats 3
# Acrescente --current-boundary para comparar a segunda estratégia.
```

O teste nativo com 12 gravações humanas comparou 36 janelas: coleta ativada
e encoder reutilizado versus coleta desativada e encoder sem reutilização.
Textos/idiomas permaneceram iguais, o alinhamento das probabilidades passou,
assim como 12 STOP e 12 cancelamentos de revisão com recuperação. Isso testa a
integração e o isolamento dos dados; não estabelece calibração semântica do limite.
O avaliador do corpus humano também passou a reprovar `revision-preserved`,
consistente com os goldens presencial/duplex.

Na revisão final, o golden humano PT/EN passou com a política padrão e com o
candidato consecutivo 0,3, sem divergências e com os mesmos textos reconhecidos.
O cenário com ruído também passou com o candidato. Esse WAV humano combina duas
gravações FLEURS; não representa uma chamada física ou a avaliação do corpus
reservado. O build Debug passou. A decisão de manter o padrão foi preservada.

## Erro de sentido independente da confirmação

Uma gravação adicional do conjunto humano de desenvolvimento
(`pt_br-18401650931916642390.wav`) revelou **“ferver a água” → “viver a água”**
no reconhecimento final. A tradução resultante contém “living water” em vez de
“boiling water”. O erro ocorre igualmente no controle padrão e no candidato
consecutivo 0,3; portanto, esta proteção não o corrige.

Os arquivos `artifacts/boundary-evaluation/human-corpus-control-smoke.jsonl`
e `human-corpus-smoke.jsonl` registram um PASS **funcional**, com uma unidade
traduzida e reproduzida, sem falhas técnicas. Seus campos `meaningReview` e
`acousticAnnotation` permanecem PENDING: esse PASS não aprova o sentido da
tradução. A comparação textual identificou a divergência; a revisão humana do
áudio continua pendente. O participante permaneceu não identificado nos dois
testes. Não se infere aprovação de diarização desta gravação isolada.

Os próximos critérios continuam sendo reduzir o atraso **sem confirmar palavras
erradas**, avaliar erros de sentido nas referências humanas e medir a conversa
completa. Nem ausência de revisões, nem o uso de GPU/ANE, substituem essas
verificações.

## Aplicativo e validação consolidada

A versão atual foi recompilada em Debug e Release de diagnóstico. Passaram
56 verificações C++ e 20 testes Swift, incluindo coleta limitada, exclusão de
conteúdo, exportação UTF-8, falha de escrita e preservação do relatório após
STOP. A assinatura, os 13 grupos de modelos / 2.187 arquivos e a auditoria
estática de rede também passaram. Essa auditoria não substitui o teste físico
com rede desligada.

O aplicativo atualizado está em `artifacts/LocalVoiceTranslator-Diagnostic.app`.
Em **Engine → Gravar desempenho**, use **Parar gravação → Exportar TXT…** para
salvar as estatísticas. O padrão de confirmação permanece inalterado. O painel
de salvar ainda requer verificação manual; os testes automatizados cobrem o
conteúdo, a escrita do TXT e o ciclo da gravação.

`artifacts/boundary-evaluation/validation.json` consolida escopos, pendências e
hashes dos resultados e do executável. Não libera a aceitação do tradutor completo.
