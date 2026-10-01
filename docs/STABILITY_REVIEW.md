# Revisões após confirmação: diagnóstico e aceitação

27/09/2026. Continuação da implementação; **não é correção da causa do ASR nem
aceitação de tradutor completo**.

Experimento posterior: [evidência de encerramento por token](BOUNDARY_EVIDENCE.md).
Evitou a divergência nas repetições com ruído, mas aumentou a espera em fala limpa;
permanece desativado na política padrão.

O cenário sintético `speech-noise.wav` reproduziu uma confirmação de
“Vamos começar a reunir.” seguida da revisão final “Vamos começar a reunião.”.
O mecanismo existente conseguiu manter a continuação sem repetir o prefixo,
mas isso não corrige a palavra que já havia sido confirmada para tradução.
O teste antigo contava `revision-preserved` e ainda podia terminar com PASS.

## Mudanças

- O motor conta `revisionConflicts` (todas as divergências depois da confirmação)
  e `recoveredContinuations` (subconjunto com continuação recuperada). Os
  contadores são atômicos, reiniciam com a sessão e independem da fila de eventos.
- A bridge, o painel Engine e a gravação/exportação TXT incluem ambos os campos.
  Nenhum conteúdo de conversa foi adicionado ao relatório do aplicativo.
- Os goldens presencial e duplex reprovam divergências, incluindo continuação
  recuperada. Recuperação continua disponível no aplicativo para não perder
  mais fala; não é tratada como prova de qualidade.
- O gate de release lê os eventos dos JSONLs, além do resultado final, nos
  goldens, cenários e ensaios longos. Um PASS antigo com `revision-preserved`
  não pode aprovar a entrega. Quatro testes cobrem esse critério, contadores,
  registros incompletos e o caso estável.

## Evidência

`artifacts/stability-review-noise.jsonl` terminou com `revisionConflicts=1`,
`recoveredContinuations=1`, `preservedRevisionEvents=1`. O executável retornou
1 e explicou a falha em `artifacts/stability-review-noise.err`. Não houve
descarte de captura, VAD, reprodução ou eventos nesse ensaio. Houve compilação
em paralelo: este resultado demonstra a divergência, não mede ganho de latência.

O novo leitor também rejeitou o PASS histórico de
`artifacts/coreml-comparison/pipeline/noise-metal.jsonl`. Os artefatos antigos
foram preservados. Não reutilizar sua classificação como aceitação atual.

Validação desta alteração: builds Debug/Release diagnóstico, 56 verificações
C++, 20 testes Swift e quatro testes Python passaram. Os goldens sem ruído e
duplex passaram com zero divergências. A assinatura e a auditoria estática de
rede passaram; a janela Engine abriu no diagnóstico de UI. Isso não testa a
interação manual com o painel Salvar nem uma chamada física.
Evidência consolidada: `artifacts/stability-review-validation.json`.
O aplicativo atualizado é `artifacts/LocalVoiceTranslator-Diagnostic.app`;
o relatório TXT agora cobre 115 campos numéricos.

O próximo trabalho na confirmação deve comparar hipóteses e contexto acústico
com as mesmas entradas, medir estabilidade e atraso conjuntamente e preservar
negações/números/nomes. Aumentar cegamente a espera ou aceitar a revisão como
sucesso não comprova a conversa natural desejada. Testes físicos, chamada ao
vivo, rede desligada e novos ensaios de 30 minutos permanecem pendentes.
