# Arquitetura

```text
SwiftUI (eventos a 100 ms; medições a 1 s)
              ↕ eventos estruturados / Objective-C++
AVAudioEngine + VoiceProcessingIO (AEC e supressão do sistema)
  captura → ring 64 → worker de captura/eco/resampling → ring 64 → WebRTC VAD
  render  ← 8 slots PCM ← worker TTS ← scheduler por participante ← worker tradução
     │                                                              ↑
     └→ referência PCM real                 coordenador de estabilidade/revisões
                                                                    ↑
         fila 8 + última hipótese parcial → worker ASR/diarização → fila 32
                 ECAPA512 / Pyannote / Conv-TasNet sob demanda / Whisper Metal
```

Callbacks de áudio só copiam buffers fixos e atualizam atomics. Conversão de frequência,
fonemização, ganho/fades, inferência, locks de filas e liberação de vetores ficam fora
deles. PCMPlayback tem oito slots e uma saída por vez; o worker prepara todo o trecho,
aplica ganho 0,8 e fades de 10 ms. A referência contra eco contém exatamente esse PCM.
Saturação da saída descarta um novo trecho inteiro; não corta a fala em reprodução.
Eventos de início/fim usam o timestamp do dispositivo e o deslocamento da amostra.

O VPIO usa formatos de cliente compatíveis com a entrada e os canais do dispositivo.
Não altera taxa/default device globalmente. Reconfigurações compatíveis têm até três
retentativas; mudanças incompatíveis encerram a captura com erro. A validação física
com desconexão/troca de dispositivos permanece separada.

O detector conservador de auto-playback compara referência e entrada a 8 kHz, janela
máxima de dois segundos e atraso até 500 ms. Só suprime correspondência quase exata;
não fecha o microfone durante TTS. É complementar ao AEC do VPIO, não um substituto
para validação acústica. WAVs exercitam o detector/PCM, sem simular o AEC do sistema.
Uma segunda verificação compara a forma de onda das unidades, inclusive saídas do
separador, com até 30 segundos de PCM realmente reproduzido. Na entrada mista, todas as regiões materiais
devem corresponder ao mesmo alinhamento. Apenas nas saídas do separador, a verificação
tolera buracos/artefatos se pelo menos 70% da energia material corresponder ao playback
alinhado; a outra fonte segue para reconhecimento. É uma heurística de eco residual,
sem garantia para toda reverberação acústica.
A comparação fica nos workers, antes de criar IDs, e não usa texto nem identidade vocal.

WebRTC VAD usa frames de 20 ms, modo 2, preroll de 150 ms, silêncio final de 300 ms e
mínimo de 100 ms vozeados. Em streaming, prefere pausa de 120 ms após seis segundos e
limita a janela a aproximadamente 9,8 s; a configuração legada de testes mantém 8 s.
Taxas 44,1/96/192 kHz são convertidas continuamente para 16 kHz pelo worker, preservando
histórico do filtro e timestamps. Descontinuidades descartam o segmento incompleto e
os contadores expõem perdas de captura/filas.

O worker ASR é dono do Whisper e da diarização. ECAPA512 usa fbank de 80 bandas,
16 kHz, frames 25/10 ms, Hamming, normalização de média e embedding L2. ResNet34 é
referência de avaliação. SpeakerTracker mantém até 64 centroides; combina evidência
por cosseno e margem, atualiza só fala confiável e não faz merges automáticos.
Nenhuma identidade vem de volume, pitch ou gênero. Retornos não expiram IDs.

Pyannote 3.0 analisa janelas de 10 s com avanço de 5 s. Seus rótulos locais nunca viram
IDs persistentes. Cortes de fala limpa exigem evidência de embeddings distintos nas
duas margens. Overlap encaminha até duas fontes ao Conv-TasNet FP32; embeddings
associam as saídas sem atualizar centroides de fala limpa. Falhas/rejeições geram
`overlap-unresolved`. Fontes não são inventadas para preencher o limite de duas.

Conv-TasNet é carregado sob demanda. O modelo exportado usa uma função de perda
invariante à escala; sua saída bruta pode ter escala/polaridade arbitrária. Uma projeção
linear das duas saídas contra a mistura restaura suas escalas antes da validação PCM.
Consistência da mistura, correlação entre fontes e energia relativa são condições de
admissão, não uma garantia de inteligibilidade. O modelo é liberado após cinco segundos
inativo ou sob pressão de memória.

Whisper usa Metal, quatro threads, beam 5 e detecção real de idioma. Contexto textual
limitado a 512 bytes por participante/idioma só condiciona janelas adjacentes, até 500 ms,
de fala contínua; isso evita repetir texto de turnos antigos. A direção PT/EN é validada
por LanguageStabilizer e LanguageRouter; idioma incerto/externo é explícito.

Hipóteses parciais chegam a cada 1,5 s e carregam sessão, fala, revisão e lane. O
SegmentStabilityManager confirma prefixos em três hipóteses consecutivas, respeita palavras/pontuação e
exige duas palavras após a oração em cada uma das três hipóteses usadas para confirmar. Silêncio/final confirma a cauda.
Áudio já confirmado não é substituído: divergência posterior é sinalizada. Uma única
palavra alterada pode preservar a continuação se o limite da oração e duas palavras
da cauda permanecerem alinhados; reescritas ambíguas retêm a continuação. Não há
promessa de que uma hipótese repetida seja linguisticamente correta.

Tradução e TTS têm workers independentes. CTranslate2 4.8.0 roda CPU ARM64:
INT8/Ruy ou FP32/Accelerate, sem Metal. SentencePiece codifica as unidades; EN→PT
recebe obrigatoriamente `>>pob<<`. Orações são traduzidas separadamente para evitar
omissão de uma frase em entradas concatenadas. As unidades têm no máximo 512 tokens.
Piper/VITS roda via Sherpa-ONNX 1.13.6 e fonemização espeak local. Seis vozes formam
três pares; `(speakerID - 1) % 3` fixa o perfil mesmo quando o idioma muda. Uma voz
fica carregada por vez. Texto de síntese é limitado antes da inferência e PCM a 30 s.

As filas têm capacidade fixa: captura 64 blocos de 2048 floats; VAD 64; segmentos 8;
ASR 32; tradução 8; TTS 8 (até 4 por participante); playback 8; eventos UI 256. O
scheduler escolhe o menor timestamp entre as cabeças das filas dos participantes.
Separação/diarização são etapas sequenciais do worker ASR e compartilham a fila de
segmentos; não se mostram filas fictícias para elas. Métricas de idade existem onde
há timestamps de enfileiramento, sem inventar a idade dos rings.

MemoryBudget usa footprint físico do processo, reservas conservadoras de modelos e
admissão de trabalho, incluindo sessões antigas em encerramento. Limite de engenharia:
8 GiB. Tradução, TTS e ASR também pedem reserva para inferências; separação reserva
2 GiB temporários. Pressão crítica nega novos trabalhos. Pressão libera separação,
voz ociosa e caches. As estimativas não são um limite de memória imposto pelo kernel;
os testes verificam o comportamento e o pico observado.

STOP invalida a sessão, silencia o render, para o tap/hardware e entrega a destruição
para uma fila de fundo. Um novo core recebe a próxima sessão; resultados antigos não
entram na UI. Whisper/ORT observam cancelamento nos pontos suportados. CTranslate2
termina sua unidade limitada em segundo plano; nenhum resultado cancelado vira áudio.
O logger global Whisper não guarda ponteiro para uma sessão destruída.

Métricas incluem filas reais, latências por etapa, modelos/voz carregados, participantes,
CPU, footprint, pico, pressão e estado térmico público do macOS. Não há leitura inventada
de temperatura em graus. Sparklines têm 60 pontos; transcrições, 80 linhas. Primeiro
áudio conta uma amostra por fonte reconhecida; E2E mede cada unidade confirmada até o
fim da reprodução. Soma-se a latência de apresentação da saída. P50/P95 só aparecem
com 30 amostras e usam no máximo as últimas 256.
