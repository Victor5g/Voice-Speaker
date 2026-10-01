# Candidato ASR large-v3-turbo Q5_0

Adquirido e verificado para avaliação de desenvolvimento em 2026-09-27. O teste posterior de compatibilidade passou em dois WAVs completos. A comparação de desempenho continua pendente. Não selecionado nos perfis nem incluído no aplicativo; o nome "turbo" não estabelece vantagem sobre o modelo atual neste pipeline.

O inventário separado `models/asr-candidates.lock.json` fixa o GGML publicado por `ggerganov/whisper.cpp` no commit `5359861c739e955e79d9a303bcbc70fb988958b1`: 574.041.195 bytes, SHA-256 `394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2`, licença MIT. O vocabulário está incorporado no arquivo. Referência do modelo: [cartão oficial OpenAI](https://huggingface.co/openai/whisper-large-v3-turbo). Identidade do arquivo: [ponteiro LFS fixado](https://huggingface.co/ggerganov/whisper.cpp/raw/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-large-v3-turbo-q5_0.bin).

Aquisição explícita, somente durante desenvolvimento e depois de encerrar medições que possam sofrer interferência:

```sh
scripts/project-local.sh /usr/bin/python3 scripts/fetch-asr-candidate.py large-v3-turbo-q5_0
```

O script baixa exclusivamente esse candidato em uma pasta temporária sob `models/optional`, verifica tamanho e SHA-256, substitui o arquivo por rename e só então registra o modelo em `models/manifest.json`. Nenhum perfil, seleção, dependência ou biblioteca é alterado. A preparação de recursos existente inclui apenas ASR selecionado, portanto registrar o candidato não o coloca no aplicativo.

Links simbólicos e caminhos que escapem do projeto são rejeitados. Falhas de transporte ou integridade preservam arquivo e manifesto anteriores. Falha ao substituir o manifesto reverte a substituição do modelo. Dois processos deste downloader são serializados por lock local; uma edição do manifesto detectada durante o download interrompe a operação. Os dois renames não são uma transação de sistema de arquivos: interrupção abrupta entre eles pode deixar um modelo verificado ainda não registrado; repetir o comando registra o cache verificado.

Testes sem rede, downloads reais ou inferência:

```sh
scripts/project-local.sh /usr/bin/python3 tests/asr_candidate_download_tests.py
```

## Evidência de aquisição

Os 22 testes de integridade e isolamento passaram. A aquisição real terminou com sucesso e uma verificação independente releu o arquivo completo: tamanho 574.041.195 bytes e SHA-256 iguais ao lock. O manifesto passou de 18 para 19 entradas; as 18 entradas anteriores e todos os outros campos permaneceram iguais. `models/selection.json` permaneceu idêntico byte a byte, mantendo Balanced `ggml-small.bin` com backend Metal. Nenhuma compilação, inferência ou atualização de `.app` foi executada nesta aquisição.

Evidência local: `artifacts/asr-turbo-acquisition/validation.json`, `download.log` e snapshots `manifest-before.json` / `selection-before.json`. Os termos do candidato foram acrescentados ao `MODEL_LICENSES.md`. O download ficou integralmente no projeto.

Antes de qualquer adoção: comparar com o controle nas mesmas gravações humanas PT/EN, incluindo nomes, negação e números; avaliar estabilidade de prefixos e revisões após confirmação; medir separadamente ASR, espera e primeiro áudio; medir memória do pipeline completo, cancelamento, carga prolongada e ambos os caminhos da chamada. Nenhuma aceleração ANE é atribuída a este GGML/Metal e nenhum encoder Core ML é adquirido por este script.

## Triagem ASR preparada

Estado atual: **PENDING**. A primeira tentativa foi recusada no preflight por outra compilação externa ativa (`xcodebuild` do CicloLivre, PID 95468). Nenhum dos dois modelos foi executado nessa tentativa. Evidência: `artifacts/asr-turbo-acquisition/screen-run.log` e `system-preflight.json`. É necessário repetir a captura do preflight e a execução depois de as compilações/inferências concorrentes terminarem; o processo externo não será interrompido.

`scripts/compare-asr-candidate.py` compara small/Metal e turbo/Metal em sequência. Seleção explícita do split development: português índices 0 (ferver), 19 (Great Yarmouth), 29 (cálcio); inglês índices 8 (11:35), 19 (Great Yarmouth), 24 (Haiti). Três repetições medidas por gravação, 18 resultados por modelo; o warmup de cada arquivo já é excluído pelo benchmark nativo. Oito testes puros do comparador passaram, sem carregar modelos.

É necessário encerrar outras compilações/inferências antes da medição. Como o sandbox aninhado restringe `ps`, o operador captura externamente um inventário recente no projeto com formato `{"capturedAt":"ISO-8601 com fuso","processes":[{"pid":1,"command":"/sbin/launchd"},...]}`. `command` deve ser o caminho de executável obtido por `ps -axo pid=,comm=`. O comparador rejeita um inventário com mais de 60 segundos ou processos conhecidos de compilação/inferência ativos. Nenhum processo é encerrado pelo script.

```sh
scripts/project-local.sh /usr/bin/python3 scripts/compare-asr-candidate.py \
  --run --system-preflight artifacts/asr-turbo-preflight.json
```

A saída padrão é uma pasta nova `artifacts/asr-turbo-screen`; se já existir, o comando recusa sobrescrever evidência. `--output artifacts/NOVA-PASTA` permite outra execução. Sem `--run`, imprime apenas o escopo e não executa inferência. O inventário inicial é uma observação pontual; o operador também deve inspecionar processos ao terminar.

São preservados comandos, stdout/stderr, timeout e códigos de saída, além de hashes do binário, fontes, bibliotecas, modelos, fixtures e seleção. O relatório registra WER por gravação/iteração, média/mínimo/máximo de tempos, memória e estados térmicos reportados pelo benchmark. Não publica P50/P95 com este conjunto. `PASS_SCREEN_COMPLETE_NOT_MODEL_APPROVAL` significa execução e coleta completas, inclusive se houver erros de reconhecimento; revisão de sentido permanece pendente. A ordem small→turbo é fixa e não controla efeitos de ordem/temperatura. Esta triagem usa arquivos inteiros e não comprova qualidade de streaming, latência E2E ou vantagem em chamada real.

## Compatibilidade nativa verificada

Em um teste separado, o backend Metal carregou o arquivo adquirido e transcreveu
dois WAVs completos: PT `pt_br-15365693344391469533.wav` (cálcio/potássio) e
EN `en_us-4844570914389909899.wav` (Haiti). Houve um warmup e uma inferência
registrada por arquivo. Os dois textos registrados coincidiram exatamente com
as referências e os idiomas foram identificados corretamente. Isso testa a
compatibilidade do modelo com o wrapper nativo; não reproduz os cortes parciais
nem o histórico do ASR no corpus de produção.

O footprint físico amostrado foi 819.316.056 e 820.692.312 bytes somente no
processo ASR. Essa observação não valida o orçamento do aplicativo completo.
Os tempos brutos foram preservados, mas uma compilação externa estava ativa:
`latencyComparisonEligible=false`. Não há comparação de velocidade com small,
percentis, medição E2E ou evidência de execução no Neural Engine neste teste.

Evidências: `artifacts/asr-turbo-compatibility/validation.json`, `native.jsonl`,
`native.err` e `processes.txt`. A seleção permanece Balanced small/Metal.
