# Agendamento do reconhecimento compartilhado

12/09/2026. Etapa de implementação de L0/L6; metas de latência ainda não aprovadas.

## Comportamento implementado

Os dois sentidos de chamada compartilham os pesos e o contexto mutável do Whisper.
O mutex anterior impedia acesso simultâneo, mas não especificava a ordem dos pedidos.
`engine/asr/InferenceArbiter.hpp` agora concede uma única autorização de inferência:

- Finais prontos para ASR precedem hipóteses parciais em espera.
- Dentro de cada classe, a ordem é FIFO. Um caminho não ultrapassa um pedido da mesma
  classe que o outro já tenha colocado na fila.
- Cada worker faz um pedido por vez. A parcial pendente por caminho continua sendo
  substituída na fila do VAD pela revisão mais recente.
- STOP acorda o pedido em espera; mudanças de revisão são verificadas a cada 20 ms,
  como no mecanismo anterior. Pedidos cancelados são removidos sem aguardar a liberação
  do modelo. Essa periodicidade não é promessa de prazo do escalonador do macOS.
- A autorização é liberada por RAII, inclusive se a inferência lançar uma exceção.
  Callbacks externos de obsolescência executam fora do mutex do agendador.

Uma transação Whisper já iniciada não é interrompida apenas porque apareceu um final
no outro caminho. Cancelamento por STOP/revisão continua disponível pelo backend.
Prioridade vale para pedidos prontos: não antecipa a diarização ainda em execução.
Parciais podem esperar enquanto houver finais; sob carga excessiva isso deve aparecer
nas métricas, sem sacrificar finais para simular baixa latência.

## Correção de medição

O tempo entre publicação VAD e retirada pelo ASR passa a ser registrado uma vez por
segmento, inclusive se ficar obsoleto antes da diarização. Antes, era registrado por
unidade com texto publicado, duplicando o peso de segmentos com múltiplas fontes e
omitindo descartes.

A espera pelo modelo inclui os pedidos cancelados durante a admissão. As etapas de
inferência com timestamps válidos também são medidas antes de descartar texto vazio
ou uma revisão ultrapassada. Isso amplia a cobertura agregada; não conclui a exigência
L0 de rastrear individualmente todos os trabalhos aceitos, recusados e cancelados.

## Evidência disponível

- Build Debug nativo: PASS.
- Dez regressões com ASan/UBSan: PASS, incluindo prioridade, FIFO, remoção de pedidos,
  callbacks que falham, liberação após exceção e 400 transações concorrentes.
- As mesmas regressões com ThreadSanitizer: PASS. Não é uma auditoria de concorrência
  de todas as bibliotecas nativas.
- Duplex com o mesmo WAV, binários antes/depois executados em sequência: ambos PASS,
  duas unidades PT→EN e uma EN→PT reproduzidas, captura durante playback e nenhuma
  perda de fila detectada. Footprint máximo aproximadamente 1,947/1,948 GB decimais.
- Nos eventos publicados, havia dez inferências por direção em cada execução.
  O maior tempo de espera foi 338 ms antes e 347 ms depois. Há contenção residual;
  esse ensaio não demonstra melhoria de cauda nem aprova P95 ≤250 ms. Pedidos
  cancelados não aparecem nesse traço de eventos, embora agora contem na métrica
  agregada do engine.

Evidências em `artifacts/asr-arbitration/`: logs de build e sanitizers, binários
identificados por SHA-256 em `comparison.json` e timelines de controle/candidato.
A bateria completa dessa revisão está em andamento. Os testes de 30 minutos e
a aceitação física/offline permanecem pendentes para a revisão final.

## Próxima decisão técnica

Para reduzir a espera residual, medir a interrupção cooperativa de uma parcial ativa
quando chegar um final no outro caminho, preservando a transação exclusiva e sem
publicar texto interrompido. Comparar o trabalho desperdiçado e o impacto sobre a
estabilidade: conceder prioridade não torna a inferência atual instantânea.

A antecipação durante pausas de 180/220/260 ms ainda não está implementada. Ela precisa
de um contrato explícito entre VAD, revisão, ASR e confirmação: resultado antecipado
não pode ser falado só porque uma pausa provisória foi detectada. Retomada de fala
deve invalidá-lo antes da reprodução; reutilizar inferência exige verificar a mesma
unidade e a compatibilidade do contexto de áudio. A comparação final deve fornecer
o mesmo PCM e os mesmos silêncios aos dois lados, conforme o achado da avaliação humana.
