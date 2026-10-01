# Continuação com nomes próprios e mudança aparente de idioma

Revisão de código e artefatos em 2026-09-27. Nenhuma inferência nova foi executada e nenhuma política de produção foi alterada nesta revisão.

## Evidência observada

O controle `artifacts/latency-policy/human-comma-boundary-20260927/pt-control.jsonl` contém dois casos que uma correção deve distinguir:

| Gravação | Saída anterior | Nova janela | Resultado atual |
| --- | --- | --- | --- |
| `pt_br-5779886153857809927.wav` | PT, participante 1, `Inicialmente, ele foi hospitalizado no James Paget Hospital.` | EN, participante 0, `and great Yarmouth.` | A continuação da referência `em Great Yarmouth` desaparece da tradução. |
| `pt_br-15365693344391469533.wav` | PT, participante 1, termina em `prata e ouro.` | EN, participante 0, `Thank you.` | O filtro impede uma adição que não consta da referência. A origem acústica da adição não foi anotada manualmente. |

No primeiro caso, o sourceSegment 240 termina por silêncio (`endpointReason=1`), com último frame de voz 4030080 e fim da janela 4034880. A continuação pertence ao sourceSegment 248, também encerrado por silêncio, com fim da janela 4064320. Portanto a perda ocorre entre duas unidades do VAD, e não por confirmação de vírgula dentro da mesma hipótese. As linhas 484–488 registram a continuação removida, o evento `direction-filtered` e o resultado sem o local.

No segundo caso, sourceSegments 352 e 360 também são consecutivos e o participante da continuação é desconhecido. As linhas 731–732 registram `Thank you.` removido. Proximidade temporal, rota portuguesa e histórico imediatamente anterior **não bastam** para decidir que a segunda janela deve herdar português.

Essas gravações são dados de desenvolvimento. A presença de nome correto na referência não autoriza introduzi-lo como prompt de reconhecimento.

## Causa no código

1. `VadSegmenter.hpp` encerra a fala após 300 ms de silêncio; depois de seis segundos, uma pausa de 120 ms também pode encerrar a janela de streaming. A próxima unidade começa uma nova sequência. O preroll conserva até 500 ms da região inativa, mas não uma janela completa da fala anterior.
2. `WhisperASR.cpp` detecta idioma antes de selecionar o prompt e usa o idioma detectado em `params.language`. Não existe, hoje, um prior de idioma da rota nessa seleção.
3. `ASRWorker.cpp` busca texto anterior por **participante confirmado e idioma detectado**. Exige participante diferente de zero e adjacência de até 8000 frames (500 ms), sem sobrepor as janelas. A continuação com participante 0 não recebe prompt. Mesmo com participante conhecido, detectar EN não recuperaria o histórico PT.
4. `LanguageStabilizer.hpp` evita compartilhar histórico entre participantes desconhecidos. Essa proteção é necessária em chamadas com várias pessoas; modificá-la para herdar o último idioma globalmente misturaria interlocutores.
5. `ConversationPipeline.cpp` descarta uma detecção incompatível com `sourceLanguage_`, antes de confirmar/traduzir a unidade. Isso evita traduzir o caminho contrário ou admitir texto espúrio. Simplesmente retirar o filtro não corrige `and` para `em` e pode tentar traduzir o idioma contrário com o modelo inadequado.

O rastreador não deve atribuir participante 1 a uma janela incerta só para viabilizar contexto linguístico. Identidade vocal e idioma são decisões diferentes.

## Próximo experimento recomendado

Testar **recuperação acústica limitada de uma continuação**, mantendo o filtro atual até existir evidência de benefício. Uma primeira etapa só diagnostica; não produz áudio adicional.

- Registrar no harness de desenvolvimento, para cada resultado ASR, os frames inicial/final exatos, utterance/lane, final/revisão, participante, overlap, idioma e probabilidades PT/EN/detectada. As probabilidades existem em `ASRResult`, mas não atravessam `TranslationResult` e não estão nos eventos atuais do corpus. Também registrar contagem de frames positivos do VAD e confiança de não fala, se esta for acrescentada ao ASR; duração total da janela não equivale a duração de voz.
- Reproduzir os dois casos com o mesmo fluxo, preroll, silêncio, histórico e sequência de inferências. Extrair os intervalos realmente enviados ao ASR pelo harness, sem inferir o início do recorte pela duração do WAV ou pelo último frame de voz.
- Comparar o recorte atual com uma segunda análise que inclua uma quantidade pequena e limitada de PCM anterior da **mesma rota**, preservando os limites entre unidades. Medir se a evidência acústica conjunta recupera o idioma e o nome. Não iniciar a análise com a referência textual, não substituir texto já falado e não assumir que o participante anterior é o atual.
- Se o contexto acústico recuperar PT, avaliar decodificação **apenas do novo intervalo** com essa evidência de idioma. O texto da análise conjunta não pode ser enviado diretamente ao TTS: isso duplicaria a frase anterior e poderia revisar conteúdo já falado. Este passo pode exigir novo encode do recorte; medir seu custo, sem supor reaproveitamento entre mels diferentes.
- Limitar a tentativa a finais curtos incompatíveis com a rota, com PCM adjacente disponível, sem descontinuidade de dispositivo, sem overlap e sem outra identidade conhecida. Uma tentativa por unidade; orçamento de memória e prazo explícitos. Falta de evidência, cancelamento ou falha conserva o resultado filtrado. A elegibilidade de participante desconhecido é precisamente a hipótese a validar, não uma condição já comprovada segura.

Esse experimento permite avaliar o caso real sem aumentar a espera de todos os turnos nem alterar imediatamente o fechamento do VAD. Sua aprovação depende de distinguir a continuação legítima da adição `Thank you.`. Acrescentar contexto pode reforçar alucinações; forçar português pode simplesmente transformar a adição em `Obrigado.`. Nenhum dos dois resultados seria correção.

## Alternativas e limites

| Alternativa | Limite observado ou esperado |
| --- | --- |
| Forçar sempre o idioma configurado | Pode reduzir uma etapa de LID, mas esconde idioma errado, troca real de idioma e alucinações. Não é uma correção conservadora. |
| Aceitar EN numa rota PT | Mantém `and` incorreto e admite o contraexemplo; os modelos carregados por rota podem nem atender essa direção. |
| Reusar idioma do último participante | Ambos os casos são participante 0. Assumir identidade ou herdar globalmente prejudica várias pessoas e alternância de idioma. |
| Aumentar a pausa do VAD para todos | Pode juntar `em Great Yarmouth`, mas aumenta latência em toda conversa e pode unir pessoas diferentes. Precisa comparação acústica, não apenas WER agregado. |
| Prompt textual da frase anterior | O prompt é escolhido depois da detecção atual e pode repetir conteúdo. Alterar apenas o lookup não resolve LID nem garante fidelidade. |
| Rejeitar uma lista de frases como `Thank you` | A frase pode ser fala legítima. Uma lista específica para o corpus não é um critério acústico. |

## Critérios de regressão antes de promover

1. O pipeline de produção deve preservar `em Great Yarmouth` e seu sentido uma única vez, sem repetir `James Paget Hospital` e sem injetar palavras da referência no reconhecimento.
2. O contraexemplo não pode ganhar uma unidade falada espúria, nem em inglês nem como `Obrigado`.
3. Testes devem incluir fala real em idioma contrário à rota, nomes estrangeiros dentro de PT/EN, retorno de outro participante, participante desconhecido, silêncio/ruído, cortes de janela, overlap e alternância real de idioma. O corpus atual não prova todos esses comportamentos.
4. STOP, revisão obsoleta, mudança de dispositivo e sessão nova invalidam retry e PCM contextual. Não há persistência de áudio/texto no aplicativo.
5. Medir custo adicional por tentativa, frequência de tentativa, filas e memória; publicar P50/P95 somente com pelo menos 30 amostras relevantes. Não misturar tempo de captura com tempo posterior ao fim da fala.
6. A/B deve preservar modelo, backend, ordem e histórico de inferência. O estado de amostragem do Whisper depende de chamadas anteriores; uma chamada diagnóstica extra não pode contaminar o controle. Usar estados independentes com histórico equivalente e registrar a sequência.

Estado: **diagnóstico confirmado; correção de produção pendente**. Este documento não demonstra qualidade física, áudio de chamada, latência de 45–75 ms, nem aprovação geral de streaming.

## Probe isolado de contexto acústico

`tests/cpp/ASRContinuationProbe.cpp` implementa uma comparação de desenvolvimento, compilada com `bash scripts/build-asr-continuation-probe.sh`. Executar somente quando não houver outro benchmark nativo:

```sh
scripts/project-local.sh build/asr-continuation-probe MODEL SHA256 WAV FIRST_FRAME END_FRAME CONTEXT_FRAMES
```

Os índices são relativos ao WAV mono PCM16 de 16 kHz; o fim é exclusivo. O probe reconhece o recorte original e, se o contexto for maior que zero, o recorte estendido à esquerda. Cada condição cria modelo e estado novos, sem prompt ou warmup. A saída JSONL contém texto completo, idioma bruto, probabilidades, limites, hash do WAV e tempos de mel/LID/decode. O texto da condição estendida inclui a fala anterior e **não é uma continuação pronta para reprodução**. A comparação tem histórico inicial equivalente entre condições, mas não reproduz o histórico de chamadas do pipeline do corpus.

`tests/data/asr-continuation-probe-inputs.json` registra somente coordenadas comprovadas no artefato antigo. Os inícios exatos não constam daquele trace e permanecem `null`. No contraexemplo `Thank you.`, o fim relativo observado é 144000 frames, enquanto o WAV tem 138240 frames: a captura inclui cauda adicionada pelo harness. O probe rejeita esse limite em vez de preencher ou cortar silenciosamente. O replay instrumentado deve fornecer limites e PCM efetivos antes de uma comparação fiel; escolher outro recorte é permitido apenas como experimento explicitamente diferente.
