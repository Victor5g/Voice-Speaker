# Entrega pessoal do LocalVoiceTranslator

Atualizado em 2026-09-12T01:38:10.481380+00:00.
Implementado o fluxo nativo local microfone → Whisper → tradução PT-BR ⇄ EN → Piper →
PCM, com escuta contínua, participantes, overlap/separação, revisões e interface.
**Aceitação como tradutor completo: PENDING**, pois faltam os testes físicos e com
rede desligada pelo usuário. Build ou WAV aprovado não substitui esses testes.

## Build

- Debug/XCTest e Release usam xcodebuild ARM64, macOS 14+, SwiftUI/C++20/Objective-C++.
- App pessoal: gerado em `artifacts/LocalVoiceTranslator.app` (1.29 GiB de arquivos).
- DMG: gerado em `artifacts/LocalVoiceTranslator.dmg`.
- Assinatura ad-hoc, App Sandbox; sem notarização/distribuição comercial.
- Gates reais verificam hashes/auxiliares, testes atuais e sessão contínua. O relatório
  `artifacts/automated-acceptance.json` vincula resultados ao fingerprint dos fontes.
- O bloqueio antigo de temporários Xcode foi resolvido pela autorização já existente.
- Empacotamento revisado: PASS.
  O DMG usa makehybrid/convert dentro do sandbox, sem montagem temporária; integridade
  da imagem e assinaturas verificadas. A imagem não foi montada para teste de instalação.
  O diagnóstico histórico da embalagem está em `artifacts/packaging-only-revalidation.json`;
  os gates atuais vinculam os testes ao código da entrega atual.
- Janela principal do Release entregue: PASS, por inspeção da captura
  `artifacts/release-final-window.png`. Abertura normal do app após o empacotamento final.
  Sessão de microfone não iniciada nessa conferência. A medição de memória ociosa usa
  esse Release. A preparação dos dispositivos foi conferida separadamente na UI Debug
  atual, conforme `docs/AUDIO_REVIEW.md`.

## Audio

A versão 0.2.1 corrige a preparação de chamada e mantém botão/erros visíveis.
O teste de primeiro uso pressiona os controles reais e confirma os dispositivos publicados
e sua remoção. Veja `docs/CALL_SETUP_REVIEW.md` e `artifacts/call-setup-review/ui.json`.

PCM sintetizado preparado fora do callback, ganho/fades incluídos na referência de
eco; filtragem adicional de fontes separadas por correspondência com o PCM reproduzido.
São oito slots e uma fala reproduzida por vez. Saturação descarta um novo trecho
inteiro com aviso. Resampling contínuo 44,1/96/192→16 kHz; callback usa buffers fixos.
STOP invalida sessão, silencia e limpa UI; workers encerram em segundo plano.

Diagnóstico real do app: Conversation/Engine visíveis em capturas de tela, VoiceProcessingIO ativado,
callbacks de captura avançando durante tom de 350 ms, ASR/Metal pronto e STOP limpo
no ensaio preservado em `artifacts/audio-hardware-verified.json`. O último reteste
aguardou consentimento de microfone do macOS e expirou; não foi registrado como PASS.
Inventário de dispositivos: entrada padrão GENERAL WEBCAM e saída External Headphones,
48 kHz; o inventário não prova qual rota estava ativa em todos os ensaios anteriores.
Nenhuma configuração global foi alterada. Conversa acústica com TTS/alto-falantes,
double-talk, troca/desconexão e prevenção de recursão continuam **PENDING**.

## AI

- Whisper small/Metal no Balanced. Eco/base permanece em avaliação por erro de sentido
  na reavaliação completa; não está habilitado.
- CT2 4.8.0 + SentencePiece CPU ARM64, INT8; duas direções Marian, `>>pob<<` no destino
  brasileiro. FP32 foi comparado; Quality permanece indisponível pela admissão de memória.
- Sherpa-ONNX 1.13.6/Piper e pronúncia local, seis vozes em três pares fixos por ID.
- ECAPA512 comparado com ResNet34; centroides normalizados, até 64 identidades por
  sessão, sem expiração ou merges automáticos. Sem inferência por volume/pitch/gênero.
- Pyannote 3.0 separado do VAD/tracker; Conv-TasNet FP32 sob demanda, até duas fontes.
  Separação não confiável é sinalizada, sem fontes inventadas.
- Parciais com revisões, três acordos consecutivos e margem de palavras. Áudio já
  falado é imutável. Revisão de uma palavra só admite continuação ancorada e deixa aviso;
  divergência ampla não produz continuação adivinhada.

## Chamada

Quatro dispositivos selecionáveis, dois fluxos PT-BR → EN e EN → PT-BR,
filas/PCM/eventos separados, filtro de direção e modelo Whisper compartilhado com
acesso serializado. O teste duplex mediu 1.76 GiB
como pico de footprint; não é uma sessão longa nem prova de integração com reuniões.
Usar fones. Cabos virtuais não são instalados pelo app; o aplicativo da chamada
recebe e transmite apenas a saída selecionada pelo usuário.

O modo nativo agora prepara dois agregados/taps Core Audio dentro do app, sem instalar
driver externo (macOS 14.2+, saída estéreo separada do microfone). Um teste com App
Sandbox e dois processos verificou transporte nas duas direções e remoção com cliente
ativo. A interface real mostrou os dois dispositivos preparados e os removeu ao sair
normalmente, mesmo após fechar a janela. A revisão de TTS e da duplicação de uma voz
está em [AUDIO_REVIEW.md](AUDIO_REVIEW.md). Chamadas reais por aplicativo permanecem pendentes.

Um ensaio adicional entregou PT e EN simultaneamente a entradas opostas:
**PASS**, 227 blocos de 20 ms com
as duas saídas em reprodução. Pico de footprint: 1.68 GiB.
Fonte derivada de teste registrada em `call-opposing-provenance.json`; resultado em
`call-opposing-directions.jsonl`. Não envolve drivers ou aplicativos de chamada.

## Performance

[Benchmarks completos](../BENCHMARKS.md) distinguem ASR isolado, pipeline, RSS,
footprint, qualidade humana/sintética e amostragem. Orçamento de engenharia 8 GiB,
reservas para inferências e liberação de caches/voz/separação sob pressão.
Sessão duplex de 30 minutos: **PASS**, com modelo compartilhado.
Sessão presencial contínua de 30 minutos: **PASS**.
Primeiro áudio P50/P95: 7252.2 / 9034.9 ms.
E2E P50/P95: 13000.4 / 15200.8 ms.
Filas limitadas, estado térmico/CPU/memória e gráficos reais; sem temperatura inventada.

## Privacy

Sem conta, serviço externo, download durante uso ou clonagem. Conversa só em memória;
STOP limpa sessão/filas/UI. Backends sem telemetria e logs de conteúdo no Release.
Dependências e modelos nativos estão no bundle; Python é exclusivo da preparação local.

Auditoria estática: App Sandbox + entrada de áudio, sem entitlement de rede ou APIs
/símbolos diretos de rede identificados. Ensaio anterior do pipeline humano com rede
negada ao processo pelo sandbox (não repetido para esta revisão): **PASS**. A primeira tentativa de sandbox
aninhado foi recusada; o teste usa uma política única com as mesmas restrições de escrita.
Isso não equivale ao aplicativo GUI com a rede desligada pelo usuário: **PENDING**.
Não se promete apagamento forense de swap ou relatórios gerenciados pelo macOS.

## Tests

| Grupo | Evidência / estado |
|---|---|
| C++ original, ASan/UBSan no código próprio | 56 verificações; `cpp-tests.log` |
| XCTest real | 15 testes, incluindo preparação sem dispositivos virtuais prévios, descoberta nativa e roteamento; `swift-tests.log` |
| ASR/VAD/Metal + transcrições segmentadas | 5 + 3; `asr-pipeline-tests.log` |
| Contenção de cleanup | 5; `cleanup-tests.log` |
| Estabilidade/scheduler/PCM/tracker/memória | 48; `conversation-unit-tests.log` |
| Formas de onda sintéticas de regressão de eco | 9; `separated-echo-regression.log` |
| Uma voz, revisões e volume variável | 1 regressão nativa humana; `speaker-revision-tests.log` |
| Modelos inválidos, cancelamento, Start/Stop | 49; `reliability-tests.log` |
| Golden sintético PT/EN, mesmo pipeline | PASS, 4 unidades faladas |
| Chamada duplex, duas direções locais por WAV | PASS; `call-routing-golden.jsonl` |
| Golden humano FLEURS licenciado | PASS, 3 unidades faladas |
| Eco digital com PCM reproduzido | PASS, 195 blocos de eco suprimidos |
| Sete cenários sintéticos | `scenario-results.json`; inclui 3 vozes/retorno, overlap, ruído/silêncio |
| Chamada duplex contínua de 30 min por WAV | PASS; `call-soak-30min.jsonl` |
| Sessão contínua real de 30 min por WAV | PASS; `soak-30min.jsonl` |

São 191 verificações unitárias/integração básica, preservando as 72 originais, mais
quatro goldens, sete cenários e o soak; medições repetidas de benchmark não inflam essa
contagem. As dependências estáticas não foram todas instrumentadas com sanitizers.
Último ensaio de confiabilidade: 12 ciclos,
maior pedido de STOP 0.2 ms, maior encerramento de
fundo 474.6 ms. Isto não mede latência acústica.

## Remaining issues

1. Executar e registrar [MANUAL_VALIDATION.md](MANUAL_VALIDATION.md): microfone→voz nas
   duas direções, double-talk/AEC acústico, IDs humanos/retorno/bilinguismo, overlap,
   Start/Stop físico, dispositivos, 30 minutos físicos e rede desligada pelo usuário.
   A aba Áudio e a janela Settings foram abertas e inspecionadas visualmente.
   Captura/reprodução com dispositivos físicos explícitos, modelo pronto, fechamento de
   janela auxiliar e STOP: PASS; `custom-audio-verification.json`.
   Isso não valida conversa humana, eco acústico, desconexão ou uma chamada remota.
   Instalação dos cabos virtuais e testes de Meet/Zoom/Teams/Discord/Slack: PENDING;
   veja [CALL_AUDIO_SETUP.md](CALL_AUDIO_SETUP.md).
2. Qualidade: nomes podem mudar (“João”→“John”), frases curtas/sotaques podem falhar;
   WER humano médio small 8,90%, base 14,91% em só 12 gravações. Sem garantia geral.
3. Separação portuguesa varia; mistura humana com grande desnível foi rejeitada.
   64 IDs são capacidade testada, não validação com 64 pessoas; máximo duas fontes.
4. Música/TV inteligível pode virar fala do ambiente. Tom instrumental/ruído sintéticos
   não substituem gravações reais de música/TV. Nenhum filtro de intenção foi prometido.
5. Ouvir/revisar as seis vozes com pessoas. Roundtrip pelo próprio ASR não prova qualidade.
6. Eco/base não passou consistentemente na precisão; Quality/FP32 não foi aprovado no orçamento. Reservas são conservadoras, não limite
   do kernel. Uma revisão posterior pode sinalizar conflito e reter a continuação.
7. Janelas contínuas têm limite de 9,8 s; a preferência por pausas/contexto não garante
   cortes perfeitos em toda fala natural. Testes físicos amplos ainda são necessários.

## Machine impact

Sem Homebrew, sudo, pacotes globais, serviços, drivers, alteração de PATH/perfil de
shell/default device/rede ou instalação em Applications. Ferramentas, modelos,
dependências e caches controláveis ficam no projeto. O venv local referencia, em três
launchers read-only, o Python já existente no Xcode; não instala outro Python global.
Os temporários/caches internos do Xcode/macOS mantêm a exceção já autorizada.

O modo nativo registra agregados/taps públicos Core Audio durante o uso. A remoção
normal foi testada, inclusive com cliente ativo e após fechar a janela do app. Um
encerramento forçado pode deixar agregados no estado gerenciado pelo macOS; a UI
orienta remover apenas os dois agregados LocalVoiceTranslator na Configuração de
Áudio MIDI. Não se promete zero estado no sistema. Nenhum HAL plugin foi instalado.

TCC, container do app, LaunchServices e relatórios são estado gerenciado pelo sistema.
A tentativa histórica de sandbox aninhado gerou um crash report do macOS, preservado.
Uma tentativa de copiar o Python do SDK falhou por vínculo de biblioteca e foi revertida;
uma chamada ao helper Bash por zsh tentou diretórios na raiz e foi recusada pelo volume
somente leitura. Não foi feita comparação de todo o filesystem; não se afirma impacto
externo zero. Auditoria: `artifacts/machine-impact-audit.log`.

## Artifacts

- [LocalVoiceTranslator.app](../artifacts/LocalVoiceTranslator.app)
- [LocalVoiceTranslator.dmg](../artifacts/LocalVoiceTranslator.dmg)
- SHA-256 DMG: `bb780102339b8bcb3f260a0afbb4fd28d11e0f8058ae067f4d9f13c14b51b58e`
- [Aceitação automatizada](../artifacts/automated-acceptance.json)
- [Medições](../BENCHMARKS.md), [licenças](../MODEL_LICENSES.md),
  [manifesto de modelos](../models/manifest.json), [cobertura das fases/cenários](IMPLEMENTATION_STATUS.md).
- Logs de build, assinatura/auditorias, WAVs de desenvolvimento e JSONLs em `artifacts/`.
  A aplicação não grava esses WAVs/JSONLs durante uso normal.
