# Interrupção de parciais para priorizar transcrições finais

27/09/2026. **Experimento reprovado para ativação no aplicativo.** A prioridade
existente continua organizando finais antes de parciais na fila, sem interromper
uma inferência ativa pela chegada de uma final de outro caminho.

**Investigação posterior:** a dependência do gerador aleatório do primeiro
decodificador foi isolada, e sua reinicialização passou em 72 execuções com
24 interrupções. A comparação de precisão encontrou uma regressão de nome,
então a mudança continua sem promoção. Veja [ASR_STATE_RECOVERY.md](ASR_STATE_RECOVERY.md).

## Implementação avaliada

O `InferenceArbiter` permite que um cliente experimental marque uma parcial como
interrompível. Uma final válida pede a interrupção; a parcial consulta esse pedido
pelo callback cooperativo do Whisper. O contexto permanece exclusivo até a
liberação da concessão: o pedido nunca permite inferências concorrentes no mesmo
estado mutável. Finais nunca são interrompíveis.

Pedidos já cancelados ou obsoletos não interrompem ninguém. Uma interrupção pedida
permanece registrada até a liberação, mesmo que a final seja cancelada depois;
isso evita retirar um cancelamento já observado pelo backend. A próxima inferência
começa sem o pedido anterior.

Os 18 testes de arbitragem passaram com ASan/UBSan e ThreadSanitizer, incluindo
400 transações concorrentes sem acesso simultâneo, cancelamento de espera,
movimentação de concessões e exceções. Isso verifica o agendador, não a qualidade
do estado interno do modelo após interrupção.

**A ligação experimental com `ASRWorker` foi retirada após a reprovação nativa.**
Nenhum caminho do app solicita uma parcial interrompível. O mecanismo fica
acessível aos testes nativos, sem novo ajuste de interface. O cancelamento
existente por STOP/revisão obsoleta não foi modificado.

## Teste com o modelo real

O teste usa `WhisperASR` e `InferenceArbiter` de produção, Whisper small/Metal e
gravações humanas FLEURS com hashes conferidos. Três gravações PT servem como
parciais e três EN como finais. Foram avaliados dois pontos de cancelamento:
depois do mel (checkpoint 2) e dentro de `whisper_full` (checkpoint 6). São
contagens do callback desta versão, não milissegundos nem uma API estável.

Para cada par/ponto, há três contextos novos com o mesmo aquecimento e histórico
de entrada: controle A, controle A repetido e candidato interrompível. Comparam-se
texto e idioma da final, além da transcrição retomada da parcial. A final é
enfileirada deliberadamente em um checkpoint, não em um endpoint acústico.

| Verificação | Resultado |
|---|---|
| Execuções nativas | 18: 12 controles e 6 candidatos |
| Controle A/A com o mesmo histórico | 6/6 iguais |
| Parciais interrompidas publicando texto | 0/6 |
| Candidatos com diferença na final ou retomada | **5/6** |
| Aceitação do candidato | **FAIL** |

Exemplos: “Bishkek” passou a “Beech Keck” em uma final; a grafia de “Chandra
Shikar” também mudou. Em uma retomada PT, faltou o trecho final que aparecia no
controle. As referências humanas e a revisão de sentido continuam necessárias;
igualdade com o controle não provaria, sozinha, que ele está correto.

A espera da final pelo modelo caiu, nos três pares de cada checkpoint, de
226–286 ms para 1,17–1,26 ms após o mel e de 105–165 ms para 0,61–1,11 ms dentro
de `whisper_full`. São intervalos do controle repetido e candidato nesta contenção
induzida, **não ganho de latência de conversa aprovado**. Não há percentis:
são três pares por condição e a equivalência de texto falhou.

## Investigação e limites

O primeiro ensaio comparava finais com transcrições isoladas produzidas em outra
sequência. Já havia diferença no controle sem interrupção, com e sem reutilização
do encoder. Ele não permitia atribuir a diferença à interrupção; foi substituído
pelo ensaio A/A/candidato com históricos iguais. Seus logs foram preservados
como diagnóstico, sem contar como medição válida de ganho.

Duas sondagens foram compiladas somente em `build/`, sem modificar as dependências
usadas pelo app. Limpar o cache de atenção própria antes da detecção de idioma
não eliminou a divergência na primeira gravação. Desativar Flash Attention passou
no checkpoint após mel dessa gravação, mas falhou dentro de `whisper_full`,
inclusive na retomada PT. Nenhuma sondagem foi integrada ao runtime.

A causa interna ainda não foi isolada. Os testes mostram dependência do histórico
de execução; não demonstram corrupção de memória ou uma falha específica de
Metal, ANE ou reaproveitamento do encoder.

Os testes anteriores de STOP e revisão obsoleta cobriam STOP já solicitado e
cancelamento após mel. Não cobriam interrupção dentro de `whisper_full` com esta
sequência e troca de caminho. Seu PASS não resolve este caso.

## Critérios para prosseguir

1. Isolar e recuperar o estado após cancelamento, sem carregar outros pesos ou
   exceder o orçamento de memória.
2. Repetir A/A/candidato nos dois idiomas e backends suportados, exigindo ausência
   de regressões nas referências e retomadas, além de ausência de texto interrompido.
3. Só então conectar ao worker e medir o pipeline duplex, contabilizando trabalho
   interrompido, parciais omitidas, finais preservadas e espera por caminho.
4. Confirmar ganho após fim da fala, qualidade e estabilidade em sessões longas
   antes de alterar o padrão. Este experimento não alcança a meta de 45–75 ms.

## Evidências e reprodução

`artifacts/asr-preemption/native-provenance.json` registra hashes do executável,
fontes e bibliotecas. `matched-metal.jsonl` e `.err` contêm resultados e
divergências de fixtures de desenvolvimento. `arbiter-asan-ubsan.log` e
`arbiter-tsan.log` cobrem o agendador. Sondagens descartadas têm logs separados.

```sh
bash scripts/build-asr-preemption-test.sh
# Sai com código 1 enquanto houver divergências; não mascarar o FAIL.
scripts/project-local.sh build/asr-preemption-tests metal reuse 3
# Sem limite final: percorre todas as 12 gravações do manifesto.
```

A revisão atual passou nos 56 testes C++ e 20 testes Swift do aplicativo.
O aplicativo de diagnóstico entregue anteriormente mantém seu comportamento;
não foi reempacotado nesta rodada experimental. Testes físicos, offline e de
sessão longa do tradutor completo permanecem pendentes.
