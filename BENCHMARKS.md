# Medições no Apple M2 Pro

Resultados de desenvolvimento em macOS/Xcode locais; não são promessa de desempenho
em outros ambientes. Outros builds/testes podem ter competido por CPU/GPU. Modelos,
hashes e configuração estão em `models/manifest.json`, `selection.json` e
`translation-selection.json`. P50/P95 são publicados somente com pelo menos 30 amostras.

## Configurações completas e ASR humano

Cada candidato foi executado com tradução INT8 CPU, ECAPA512, Pyannote e Piper pelo
pipeline de produção, com uma conversa sintética PT/EN. Separação é carregada quando
necessária; esse WAV não tem overlap. As seis vozes estão disponíveis, uma por vez.
Acurácia: 12 gravações humanas FLEURS CC-BY-4.0, seis por idioma. WER é a média das
taxas por gravação, literal: números por extenso, grafias e nomes contam como erro.
A amostra pequena não representa todos os sotaques/ambientes.

| ASR | Conversa completa | Pico RSS MiB | WER humano médio | Amostras primeiro áudio |
|---|---|---:|---:|---:|
| ggml-tiny | PASS | 737.3 | 22.45% | 2 |
| ggml-base | FAIL | não medido neste ensaio | 14.91% | 0 |
| ggml-small | PASS | 1537.6 | 8.90% | 2 |

Balanced usa small/INT8 pela precisão observada. Base/INT8 usou menos memória, mas
falhou na reavaliação completa: “reunião” virou “reunir” e a tradução “gathering”.
Eco foi retido após essa regressão, apesar do ensaio anterior aprovado. Tiny excedeu
o limiar exploratório WER 20%. Quality com
tradução FP32 foi medido, mas a admissão conservadora de memória impediu uma síntese;
permanece indisponível. Nenhum perfil foi escolhido apenas pelo nome/tamanho do modelo.
`artifacts/profile-comparison.json` contém as transcrições e configurações completas.

## Sessão contínua pelo pipeline real

Estado: **PASS**, fixture sintética repetida em tempo real, sem dispositivo físico.
Duração: 1810.0 s. Falas reproduzidas: 327.
Primeiro áudio: 181 amostras; P50 7252.2 ms,
P95 9034.9 ms. E2E: P50 13000.4 ms,
P95 15200.8 ms. Os percentis usam no máximo as últimas 256 amostras.

Footprint físico médio entre minutos 5–10: 1532.8 MiB;
nos últimos cinco minutos: 1662.7 MiB.
Maior footprint amostrado a cada 10 s: 1727.1 MiB.
Pico RSS do processo: 1670.2 MiB.
Limite de engenharia: 8192 MiB; não é um teto imposto pelo kernel.
Revisões finais recuperadas com aviso, preservando áudio já emitido:
1. Conflitos sem alinhamento, omissões por ciclo,
duplicação de playback e perdas de filas reprovam esse teste. A qualidade é conferida
por ciclo capturado, reunindo as divisões legítimas de segmentos do VAD.
Duplex de 30 minutos: **PASS**, duas entradas e duas
saídas de produção, fixture sintética. Pico de footprint: 1.86 GiB.
PT→EN primeiro áudio P50/P95: 6981.6 / 8680.6 ms.
EN→PT-BR primeiro áudio P50/P95: 8120.8 / 8640.4 ms.
As estatísticas só são válidas com 30 amostras por caminho. Evidência:
`artifacts/call-soak-30min.jsonl` e `call-soak-provenance.json`. Os ensaios longos
presencial/duplex foram executados simultaneamente, com outros testes de desenvolvimento;
isso deve ser considerado ao comparar latência com um Mac ocioso.

Evidências: `artifacts/soak-30min.jsonl` e `soak-provenance.json`.
O ensaio anterior interrompido pelo repouso do macOS está preservado separadamente.
Outro ensaio de 30 minutos, sem perdas nas filas, falhou na precisão em um ciclo;
ficou em `soak-before-streaming-preroll.*`. O streaming agora preserva 500 ms de
contexto anterior ao início da fala. O beam permanece 5; o candidato beam 8 foi
rejeitado porque não resolveu o caso. O ensaio breve anterior ao ajuste de STOP
duplex foi interrompido como obsoleto e preservado em `soak-superseded-duplex-stop.*`.
Os testes longos mantêm uma asserção temporária `caffeinate -i`, liberada ao terminar;
nenhuma configuração permanente de energia é alterada. Evidência de outra revisão
não aprova os fontes atuais.

Primeiro áudio mede captura→início; E2E mede captura→fim da unidade falada. No WAV a
latência de dispositivo é zero; o app soma a latência de apresentação informada pelo
Core Audio. Portanto esses números não medem acústica microfone→alto-falante.

## Tradução CPU

Oito referências autorais por direção/precisão; execução nativa CTranslate2 4.8.0 e
SentencePiece. FP32/Accelerate e INT8/Ruy em ARM64, sem Metal no CTranslate2.

| Direção | Precisão | Casos | Latência média ms | Pico RSS MiB |
|---|---|---:|---:|---:|
| pt-en | float32 | 8 | 143.2 | 340.8 |
| pt-en | int8 | 8 | 66.1 | 179.4 |
| en-pt | float32 | 8 | 378.6 | 936.2 |
| en-pt | int8 | 8 | 132.5 | 392.2 |

Negação, quantidades e vocabulário brasileiro foram conferidos contra referências.
Há falha real em nomes: “João” foi traduzido como “John” em PT→EN, e “encontrar” como
“find”, que pode perder o sentido de encontro. As referências não constituem revisão
humana independente completa. EN→PT usa obrigatoriamente `>>pob<<`.

## Vozes e participantes

As seis vozes geraram PCM finito; a síntese de frases de aproximadamente 3,4–4,9 s
levou 174–224 ms no ensaio registrado. Faber/Lessac, Jeff/Ryan e Edresson/Amy formam
pares fixos. O roundtrip pelo ASR testa integração, não qualidade auditiva; houve
erros de reconhecimento/pronúncia na voz Edresson e mudança de tempo verbal em Jeff.

ECAPA512: duas extrações de 3 s em 39,7–43,8 ms; ResNet34: 162,0–163,4 ms.
Cosseno mesma voz ECAPA 0,855–0,923, vozes distintas 0,013–0,301; ResNet34
0,827–0,900 e 0,072–0,387. Ensaio de três vozes sintéticas, sem garantia biométrica
ou validação de 64 pessoas. `speaker-benchmark.jsonl`, `tts-benchmark.jsonl`.

## Separação com fontes conhecidas

| Mistura | Melhoria SI-SDR fonte A/B | Resultado |
|---|---|---|
| EN/EN sintética | +13,59 / +17,42 dB | Ambas as saídas transcritas |
| PT/EN sintética | +2,89 / +3,47 dB | Transcrições com erros; qualidade inferior |
| PT/EN humana FLEURS, RMS igualado na fixture | +16,56 / +16,57 dB | Ambas as frases transcritas |
| PT/EN humana sem equalizar (grande diferença de volume) | −2,15 / +21,94 dB | Separação rejeitada; overlap não identificado |

Normalizar as fontes conhecidas foi preparação da fixture humana, não uma informação
que o aplicativo teria nem um critério de identidade. Conv-TasNet foi treinado em
inglês; o resultado português varia. A normalização linear da saída restaura a escala
arbitrária do ONNX, sem fabricar uma fonte. Evidências em `separation-benchmark.jsonl`,
`human-separation.jsonl`, `human-separation-unbalanced.jsonl` e transcrições adjacentes.

## ASR isolado: 90 medições anteriores

Whisper F16, Metal, quatro threads, flash attention, beam 5, temperatura 0. Três
WAVs sintéticos PT-BR/EN-US/EN-GB, um warmup excluído e 10 medições por fixture:
30 amostras por modelo. Inclui mel/LID/transcrição, exclui carregamento.

| Modelo | P50 ms | P95 ms | RTF P50 | Pico RSS MiB | Metal MiB | WER sintético |
|---|---:|---:|---:|---:|---:|---:|
| tiny | 109,60 | 124,29 | 0,0178 | 221,5 | 231,5 | 0,012 |
| base | 162,31 | 180,86 | 0,0264 | 332,7 | 341,0 | 0,030 |
| small | 386,67 | 405,36 | 0,0629 | 738,4 | 810,0 | 0,011 |

Estado térmico nominal no ensaio. Não somar RSS/Metal como se fossem medições
independentes sem considerar memória unificada. `artifacts/benchmark/summary.json`.

App parado, janela real visível: 51.7 MiB de footprint
em dez amostras curtas. O diagnóstico físico curto confirmou callbacks simultâneos e
ativação de AEC; não mediu atenuação acústica nem conversa humana completa.
