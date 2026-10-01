# Estado da implementação

**Atualização de 27/09/2026:** a aceitação completa permanece pendente. Foi
reproduzida uma revisão divergente depois da confirmação no cenário com ruído.
O antigo teste aceitava continuação recuperada como PASS; esse critério foi
corrigido. Engine e exportação TXT agora mostram os contadores de divergências.
Detalhes e escopo dos novos testes em [STABILITY_REVIEW.md](STABILITY_REVIEW.md).

Implementação nativa das fases 6–20, mantendo as fases 1–5 e seus 72 checks.
O resultado executado da versão entregue está em [FINAL_REPORT.md](FINAL_REPORT.md);
`artifacts/automated-acceptance.json` vincula os gates aos fontes atuais.

| Fase | Implementação | Aceitação / limite |
|---|---|---|
| 1–5 | SwiftUI, VPIO, VAD, Whisper/Metal, idiomas | Builds/XCTest/WAVs; hardware atual precisa do procedimento físico |
| 6 | CT2 4.8.0 + SentencePiece, duas direções | Referências de sentido; nomes próprios ainda podem ser alterados |
| 7 | Sherpa 1.13.6/Piper, seis vozes e pronúncia local | PCM das seis vozes testado; qualidade auditiva humana não substituída por ASR |
| 8 | Workers separados, eventos, PCM/scheduler/eco | Golden sintético e humano; reprodução acústica pendente |
| 9–11 | ECAPA512, OnlineDiarizer, tracker 64 identidades | Comparação ResNet34; três vozes sintéticas e retorno; identidade bilíngue humana pendente |
| 12–13 | Pyannote e Conv-TasNet, até duas fontes | SI-SDR/transcrição com fontes conhecidas; qualidade varia por mistura |
| 14 | Parciais, revisões, estabilidade e contexto limitado | Regressão parcial é retida; conflito final preserva áudio já falado |
| 15–16 | Filas/latências/modelos/participantes e gráficos | Valores reais; percentis somente com 30 amostras; sem temperatura inventada |
| 17 | Orçamento 8 GiB e pressão/admissão | Modelos e inferências pedem reserva; falhas explícitas, caches/voz/separação liberáveis |
| 18 | Comparação de configurações completas | Balanced small/INT8 aprovado; Eco/base teve regressão de sentido; Quality/FP32 indisponível pelo orçamento |
| 19 | Sem persistência de conversa/rede/telemetria de backend | Auditoria estática; teste com rede desligada ainda PENDING |
| 20 | Gates reais, assinatura ad-hoc, app/DMG pessoal | Verificações automatizadas e soak exigidos para empacotar; sem notarização comercial |

## Cobertura dos 15 cenários

| Cenário | Evidência automatizada | O que falta |
|---|---|---|
| PT-BR → EN | Golden sintético/humano e referências Marian | Microfone/alto-falantes, revisão humana ampla |
| EN-US → PT-BR | Golden humano e marcador brasileiro | Teste físico |
| EN-GB → PT-BR | Voz sintética Daniel e pipeline real | Gravações humanas licenciadas de EN-GB |
| Sotaques ingleses distintos | Fixtures sintéticos distintos | Cobertura humana representativa |
| Mesma pessoa alternando idioma | Tracker/roteador e pares fixos | Ground truth de uma pessoa bilíngue |
| Duas pessoas alternando | Duas fontes sintéticas; WAV humano com duas gravações | Identidades humanas verificadas no ambiente |
| Três participantes | Cenário com três vozes sintéticas distintas | Três pessoas reais |
| Retorno após inatividade | Mesmo ID após pausa no cenário; tracker sem expiração | Retorno humano após vários minutos |
| Duas falas simultâneas | Golden de overlap, SI-SDR e ASR das saídas humanas/sintéticas | Outros pares, ambientes e diferenças de volume |
| Ruído ambiente | Ruído sintético + fala, filas limitadas | Ruídos domésticos reais |
| Silêncio | Sem segmento/voz na fixture de silêncio | Ambiente físico |
| Música/TV sem fala relevante | Tom instrumental não gera tradução; VAD pode dar falso positivo | Música/TV reais; não existe filtro semântico de relevância |
| TTS enquanto captura continua | Captura/render em tempo real no pipeline WAV | Double-talk no VPIO com alto-falantes |
| Auto-playback sem recursão | Referência PCM com eco digital atrasado | Eco/reverberação acústicos reais |
| Mudanças rápidas de idioma | LanguageStabilizer testado por participante | Conversa bilíngue física espontânea |

Nenhum teste com o próprio TTS é tratado como prova suficiente de qualidade.
Gravações FLEURS possuem licença/proveniência própria; cenários sintéticos ficam em
outro diretório. Falhas de candidatos e ensaios anteriores permanecem nos artefatos;
não são confundidas com os gates atuais aprovados.

## Extensão: modo Chamada

A aba Áudio oferece seleção por UID de quatro dispositivos, dois pipelines isolados,
filtro PT→EN na saída e EN→PT-BR na recepção, métricas e gráficos por caminho. O Whisper
é compartilhado com exclusão mútua; pesos/modelo não são duplicados por direção.
O limite global permanece 8 GiB. Até 64 identidades são expostas na conversa conjunta;
identidades entre caminhos são distintas. Nenhuma inferência associa automaticamente
alguém ouvido pelo cabo à pessoa do microfone físico.

O teste de produção duplex com WAV passou. Dispositivos explícitos de webcam/fones,
modelo pronto, fechamento de janela auxiliar e STOP passaram em diagnóstico físico.
A aba Áudio e Settings foram inspecionadas na UI real. Ainda faltam cabos virtuais,
chamadas por aplicativo e validação prolongada duplex. Veja CALL_AUDIO_SETUP.md.
