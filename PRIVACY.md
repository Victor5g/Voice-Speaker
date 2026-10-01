# Privacidade e impacto na máquina

O aplicativo não tem contas, clientes HTTP, WebSockets, telemetria, serviço remoto de
inferência, analytics ou download de modelos. O App Sandbox mantém somente o acesso
a áudio de entrada e aos arquivos escolhidos pelo usuário no painel de exportação;
não há entitlement de rede. Bibliotecas nativas são estáticas;
frameworks dinâmicos são do sistema. Modelos e arquivos auxiliares são verificados por
SHA-256 antes de carregar. Python/CMake/curl pertencem ao desenvolvimento e não são
executados nem incluídos como runtime do aplicativo.

Áudio capturado, texto, traduções, embeddings e métricas da conversa ficam em memória.
Não há banco de conversas, autosave, transcript logger, histórico em UserDefaults ou
writer de WAV na aplicação. As APIs de escrita de áudio usadas pelos benchmarks são
exclusivas dos executáveis de teste. Artefatos com texto/áudio em `artifacts/` foram
produzidos explicitamente por testes de desenvolvimento, a partir das fixtures de
`tests/data/`; não são gravações de sessões do usuário.

Preferências de processamento das duas direções e o glossário editado são
persistidos em UserDefaults no container do aplicativo. São configurações
explicitamente escolhidas, sem histórico de áudio ou transcrições. Termos pessoais
adicionados ao glossário permanecem nele até remoção; exportar/importar JSON usa
somente o arquivo escolhido pelo usuário e não envia dados pela rede.

Exceção explícita: **Engine → Gravar desempenho** coleta as métricas do painel,
uma vez por segundo, em memória, até 1 hora ou 32 MiB de amostras. Inclui ambos
os caminhos da chamada, configurações/modelos, contadores, latências e estado do
processo. Uma lista explícita de campos exclui áudio, texto, traduções, embeddings,
IDs de dispositivos e detalhes textuais de erros (somente presença de erro).
**Exportar TXT** escreve apenas no destino escolhido via NSSavePanel, com o
entitlement `files.user-selected.read-write`. Não há exportação automática,
envio remoto, autosave ou recuperação após fechar o aplicativo. STOP encerra a
gravação e preserva esse relatório solicitado para exportação; Limpar o descarta.

STOP interrompe o hardware e a reprodução, invalida a sessão e limpa a interface
imediatamente, exceto o relatório de desempenho solicitado acima. Filas, contexto e embeddings são destruídos pelo encerramento de workers
em segundo plano. Resultados atrasados são descartados. Liberação/zeragem de buffers
não equivale a apagamento criptográfico de páginas do alocador, swap ou dumps do macOS.

Whisper usa um logger silencioso sem ponteiro pendente para sessão; CTranslate2 mantém
logging desligado. O patch local do Sherpa remove logs que poderiam conter texto e
substitui saídas fatais por exceções tratadas. Todos os ambientes ORT usados pelo Sherpa
e pelos wrappers próprios usam logger silencioso e `DisableTelemetryEvents()`.
Diagnósticos DEBUG imprimem apenas contadores/estado técnico; não existem no Release.

A auditoria `scripts/verify-offline.sh` inspeciona assinatura, entitlements, frameworks,
símbolos de rede, código Swift e strings de URL. URLs de documentação presentes em
bibliotecas não significam chamadas de rede. A auditoria estática passou na aplicação
nativa; ela não prova o comportamento de todo framework do macOS nem substitui o teste
real de conversa com a rede desligada pelo usuário. Não foram alteradas configurações
globais de Wi-Fi/Ethernet. Procedimento: [MANUAL_VALIDATION.md](docs/MANUAL_VALIDATION.md).

Fontes, dependências, modelos, Python de conversão, CMake, DerivedData, caches
controlados, WAVs de teste e artefatos estão no projeto. Nenhum Homebrew, sudo, pacote
global, serviço, driver ou alteração de perfil/PATH foi instalado por este trabalho.
O Python já existente no sistema é pré-requisito; pacotes adicionais estão apenas em
`tools/downloads/conversion-venv`. Os três launchers do venv referenciam o interpretador já instalado do Xcode; a auditoria os identifica separadamente como pré-requisito de leitura. Uma tentativa de copiar esse launcher não funcionou e foi revertida. O aplicativo final executa exclusivamente código nativo.

A autorização de 11/09/2026 permite os temporários/cache internos do Xcode/macOS em
`/private/var/folders/.../{T,C}`. O sandbox de ferramentas também permite o projeto e
sinks de terminal: [BUILD_AUTHORIZATION.md](docs/BUILD_AUTHORIZATION.md). Isso é distinto
do App Sandbox. Lançamentos da aplicação usam LaunchServices normalmente; sobrepor o
sandbox de build a `secinit` causou uma falha histórica e não é repetido.

O macOS pode manter registros TCC, container do app, LaunchServices, unified logs,
caches Xcode/Metal e diagnósticos fora do projeto. Há um relatório histórico de crash
`~/Library/Logs/DiagnosticReports/LocalVoiceTranslator-2026-09-11-111140.ips`, gerado pelo
sistema na tentativa com sandbox aninhado. Não foi removido. Sem baseline completo do
filesystem, não se afirma que nenhum serviço do sistema criou arquivos externos.

`clean-all.sh` valida a allowlist e a contenção de todos os destinos antes de apagar.
Remove build, caches, artefatos, modelos opcionais e ferramentas baixadas; preserva
fontes, pins, licenças e fixtures. Não limpa TCC, perfis, serviços ou estado do macOS.

## Optional call audio routes

The Audio tab can send synthesized speech to a user-selected virtual Core Audio
output. The meeting app selected by the user then receives and transmits that PCM.
Local inference remains offline; the remote meeting is not an offline service.
The translator neither joins calls nor accesses meeting accounts. Routing selections
and device UIDs are held in session memory, not written as conversation history.
Read-only enumeration lists connected Core Audio devices; no default device is changed.
STOP clears both paths. Meeting mute does not stop this app's local capture.
Optional external virtual drivers are not embedded or installed by the app.

A development-only whisper-cli beam-size experiment exceeded the vendored backend's
maximum of eight decoders and aborted. Its command output is retained in
`artifacts/soak-repro-beam10.err`; macOS may maintain its own crash diagnostic.
The production beam remains five. No global crash logs were removed.

## Native audio for calls

The optional native call mode uses public Core Audio aggregates/taps created by the
app, not installed HAL plugins. The receive tap excludes this app and is limited to
output sent to LocalVoiceTranslator Speakers. The send tap includes only this app's
output sent to LocalVoiceTranslator Microphone. macOS may request system audio
capture permission through NSAudioCaptureUsageDescription. No conversation recording
was added. STOP ends capture/playback/inference but keeps the endpoint objects until
explicit deactivation or normal app termination. Before removing endpoints, restore
meeting audio choices: a meeting app may fall back to the physical microphone when
an input disappears. Public aggregates may survive a forced termination/crash and
require removal in Audio MIDI Setup. macOS permission/state records are OS managed;
zero external state is not promised. No third-party driver was installed.

Development self-tests generated only synthetic PCM across these native endpoints;
only metrics were recorded under artifacts/latency-speaker-review. Normal usage does
not write these diagnostics. The external GPL driver installer prepared earlier stays
inside the repository as an unused alternative; it is not required for native mode.
