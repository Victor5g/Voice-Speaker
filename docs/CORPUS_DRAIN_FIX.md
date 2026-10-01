# Drenagem e atribuição das gravações do corpus

27/09/2026. Correção decorrente da [avaliação de vírgula/ponto](COMMA_BOUNDARY_EVALUATION.md).
O lote original permanece reprovado/inválido. Esta etapa valida a conclusão do
trabalho de cada gravação; não aprova o candidato de streaming nem a qualidade
da tradução.

## Defeito e correção

Filas vazias não significam que seus workers estejam ociosos. No lote original,
uma tradução do Haiti continuou em execução por 2.270 ms depois de sair da fila.
O runner encerrou a gravação, iniciou a seguinte e atribuiu os eventos restantes
ao arquivo errado. Outro intervalo vulnerável existia quando o coordenador
retirava um resultado ASR, mas ainda não tinha publicado um evento de aceitação.

A correção combina duas condições:

- `CorpusDrainTracker` mantém cada unidade aceita pendente até `played` ou erro
  terminal, cobrindo tradução, síntese e reprodução em andamento mesmo com
  filas vazias.
- `ConversationPipeline` publica `coordinatedUtterances` depois de processar
  cada marcador final `utterance-end`. O ASR produz esse marcador depois de
  todos os resultados da fala, inclusive resultados filtrados ou vazios. A
  leitura do snapshot observa essa confirmação antes de consultar a fila de
  eventos; a ordem inversa poderia combinar uma fila vazia antiga com uma
  confirmação nova e perder um evento recém-publicado.

O runner exige que a contagem confirmada pelo coordenador seja igual a
`asr.processed` e `vad.segments`, `pendingEvents == 0` e ledger vazio, além das
condições existentes de captura, VAD, filas e reprodução. O intervalo de
silêncio adicional já não serve como substituto da confirmação do coordenador.
Essa confirmação não declara tradução/TTS/playback concluídos; esse papel cabe
ao ledger e às condições de reprodução.

O trace agora emite `firstFrame`, permite escolher `startIndex` para reproduzir
casos específicos e interrompe o lote se houver timeout de drenagem ou evento
cujo fim de janela pertença à gravação anterior. Assim, uma execução inválida
não continua contaminando os arquivos seguintes.

## Evidência

| Verificação | Resultado |
|---|---|
| Ledger de unidades aceitas, ASan/UBSan | 13 passaram |
| Coordenador real, fila de eventos e snapshot, ASan/UBSan | 15 passaram |
| Comparador Python, incluindo atribuição tardia e preroll | 14 passaram |
| Runner nativo corrigido: Haiti e rumores, EN | 2/2 passaram funcionalmente |
| Runner nativo corrigido: primeiras gravações PT, controle | 2/2 passaram funcionalmente |

O teste do coordenador pausa por 550 ms o callback depois de retirar um
resultado. A contagem não avança por silêncio ou pela retirada da fila; avança
apenas após o marcador final. Também cobre consumir a saída antes/depois do
marcador e a limpeza no STOP. Logs:
`artifacts/corpus-drain-tracker-tests.log`,
`artifacts/conversation-drain-tests.log` e
`artifacts/boundary-evaluation/comma-promotion/attribution-review-tests.log`.

O smoke EN em `artifacts/corpus-drain-regression/en.jsonl` terminou com zero
unidades/eventos pendentes em ambos os arquivos, sem atribuição cruzada. Sua
proveniência fixa hashes do binário, configuração e fontes. A seleção começa
no índice 24 com estado ASR novo: não reproduz todo o histórico de inferência
das 30 gravações antigas. Nesse smoke, o Haiti perdeu “since the 1800s” na
transcrição. O resultado funcional não elimina essa falha de conteúdo nem
permite atribuí-la causalmente à correção de drenagem.

O smoke PT de controle também terminou 2/2 em
`artifacts/corpus-drain-regression/pt.jsonl`, com zero unidades/eventos pendentes.
A primeira gravação conservou o erro conhecido “ferver” → “viver”; sua conclusão
funcional tampouco constitui aceitação de sentido.

`artifacts/corpus-drain-regression/provenance.json` e
`processes-at-start.txt` registram uma compilação externa do CicloLivre ativa
no início. Por isso os números desse smoke são evidência funcional e não uma
comparação isolada de latência. Não se publicam percentis a partir de dois
arquivos.

## Execução paralela dentro do projeto

A investigação também revelou uma disputa na geração de `build/project-local.sb`
quando vários agentes iniciavam ferramentas simultaneamente: a publicação do
perfil agora usa arquivo temporário e substituição atômica. Os dois testes em
`scripts/test-project-local.py` passaram: 20 lançamentos reais simultâneos do
sandbox e falha forçada de publicação, preservando o perfil anterior e removendo
o temporário. Evidência: `artifacts/project-local-tests.log`.

Essa correção mantém os comandos e temporários no projeto; não instala drivers
ou serviços. A política candidata continua desativada. O lote de 120 execuções
antigo permanece com PT candidato reprovado e EN candidato
`INVALID_ATTRIBUTION`; corrigir o runner não transforma aqueles resultados em
PASS nem dispensa novas medições com política congelada.
