# LocalVoiceTranslator

Aplicativo macOS nativo para conversar em **português brasileiro ⇄ inglês**, com
reconhecimento, tradução e voz processados neste Mac. SwiftUI, Objective-C++ e C++20;
App Sandbox ativo; sem conta, serviço externo, clonagem de voz ou downloads durante uso.

O fluxo de produção já é exercitado com WAVs: captura → VAD → participantes/overlap →
Whisper/Metal (encoder Core ML opcional) → CTranslate2/CPU → Piper/VITS → reprodução PCM. A entrega pessoal ainda
precisa da validação física com microfone/alto-falantes e da conversa com a rede desligada
pelo usuário. **Ainda não foi validado como tradutor completo.** Consulte o
[relatório de entrega](docs/FINAL_REPORT.md) e o [procedimento físico](docs/MANUAL_VALIDATION.md).


Estudo de desempenho: [objetivos de latência, diagnóstico e critérios de aprovação](docs/LATENCY_STUDY.md). A [implementação e suas medições](docs/LATENCY_IMPLEMENTATION.md) estão em andamento; as metas de 1–2 s após a pausa ainda não foram comprovadas.
O [teste de estados privados do Whisper](docs/ASR_PRIVATE_STATES.md) preservou
as transcrições, mas executar os dois caminhos simultaneamente piorou a mediana
portuguesa. Essa concorrência continua desativada no aplicativo.
O [teste humano da confirmação de falas](docs/COMMA_BOUNDARY_EVALUATION.md)
reprovou a regra experimental. A [correção de conclusão do corpus](docs/CORPUS_DRAIN_FIX.md)
evita atribuir uma tradução ainda em processamento à gravação seguinte.
O [Whisper Turbo](docs/ASR_TURBO_CANDIDATE.md) está separado para avaliação;
o modelo padrão permanece small/Metal.

As [melhorias de processamento e diagnóstico do teste](docs/REALTIME_IMPROVEMENTS.md)
incluem controles independentes por direção, cache de voz sob pressão de memória,
identidades conservadoras e [glossário editável](docs/GLOSSARY.md).

## Usar

Para chamadas, abra **Áudio → Chamada**, escolha seu microfone físico e seus fones,
confirme o uso de fones e clique em **Preparar áudio da chamada**, no rodapé.
Na versão **0.2.1**, envio/recepção aparecem depois dessa preparação; qualquer erro
fica junto ao botão. [Correção e teste do primeiro uso](docs/CALL_SETUP_REVIEW.md). O app cria dois
dispositivos nativos de sessão, sem instalar driver. No aplicativo da chamada,
selecione **LocalVoiceTranslator Microphone** como microfone e
**LocalVoiceTranslator Speakers** como saída. Inicie a escuta no tradutor.
Esse modo exige macOS 14.2+ e saída estéreo separada do microfone. Bluetooth com
entrada/saída no mesmo UID ainda não é suportado. A permissão de captura de áudio
é controlada pelo macOS. [Guia e limites](docs/CALL_AUDIO_SETUP.md).

Após o empacotamento aprovado pelos testes, abra `artifacts/LocalVoiceTranslator.app`, permita o microfone e clique em **Iniciar
escuta**. Fale em português ou inglês; a direção é automática. A tela Conversation
mostra participante, original, tradução e reprodução. Engine mostra medições do motor.
Em **Engine → Gravar desempenho**, inicie a coleta antes ou durante a conversa.
Use **Parar gravação → Exportar TXT…** e escolha onde salvar. O relatório inclui
as métricas dos dois caminhos da chamada, coletadas a cada segundo, sem áudio ou
texto das conversas. Limite de 1 hora ou 32 MiB; STOP encerra a coleta e mantém o
relatório em memória para exportação. **Limpar** permite começar outra gravação.
Dados não exportados são descartados ao fechar o aplicativo.
STOP interrompe captura/reprodução, invalida resultados atrasados e limpa a conversa;
a liberação dos modelos continua em segundo plano.

Na aba **Idiomas e vozes** (também em **Ajustes, ⌘,**), configure os quatro campos:
**Eu falo em** e **Eles ouvem em** para enviar sua voz traduzida à chamada;
**Eles falam em** e **Eu ouço em** para traduzir todos os participantes nos seus fones.
Exemplo: PT-BR → inglês para você; inglês → PT-BR para os participantes.
Ao escolher um idioma manual igual ao outro campo, o par é invertido junto,
sem rejeitar a seleção. **Automático** continua disponível.
O seletor **Estatísticas de** do Engine apenas muda as métricas exibidas;
use **Configurar idiomas e vozes** para editar esses idiomas.
Em **Vozes e processamento**, configure a identificação de falantes. Com a identificação
desligada, escolha uma voz fixa; a transcrição continua funcionando. VOCÊ também
controla o modo Presencial. PARTICIPANTES controla apenas a recepção da chamada.
As escolhas persistem entre aberturas e só mudam com a sessão parada.
**Restaurar padrões…**, no topo de Idiomas e vozes, restaura ambos os lados,
vozes/automação, pausas/filas, perfil Balanced, aceleração padrão Metal, caminhos
de áudio e o glossário original. A confirmação informa que termos e categorias
personalizados serão substituídos e que os dispositivos nativos da chamada serão
desativados. Depois, prepare novamente a chamada na aba Áudio, se necessário.
Modelos e arquivos exportados são mantidos. O botão fica bloqueado enquanto
a sessão ou a preparação do áudio estiver ativa.
Em **Avançado**, desligue a saída em voz para usar somente texto, ajuste a pausa
de encerramento entre 200 e 1.000 ms e o limite de áudio pendente entre 3 e 15 s.
Áudios novos que excederiam esse limite são omitidos com indicação na conversa;
o texto é mantido e a fala em reprodução não é cortada. Uma unidade longa pode
tocar inteira se a saída estiver livre.
**Editar glossário…** oferece pesquisa, termos, categorias, três modos de
preservação, regras por direção e importação/exportação JSON. O catálogo inicial
contém 1.477 termos únicos; apenas as regras aplicáveis ao trecho entram na restauração.

A assinatura é ad-hoc para uso local. O empacotamento gera
`artifacts/LocalVoiceTranslator.dmg` com a mesma aplicação. Enquanto algum gate
estiver pendente, apenas `LocalVoiceTranslator-Diagnostic.app` pode estar disponível. Não há notarização nem instalação automática em Applications.
Recompilar pode exigir novo consentimento de microfone pelo macOS.

## Construir e verificar

Ambiente de desenvolvimento medido: Apple M2 Pro, 16 GiB, macOS 26.6.2, Xcode 26.5;
alvo ARM64/macOS 14+. Os modelos e as ferramentas ficam no projeto.

```sh
scripts/bootstrap-local.sh
# Restauração explícita após remover ferramentas/modelos opcionais:
scripts/bootstrap-local.sh --download
scripts/build-debug.sh
scripts/validate-personal.sh
scripts/test-soak.sh                 # 30 minutos reais, WAV em tempo real
scripts/test-call-soak.sh            # 30 minutos, dois caminhos simultâneos
python3 scripts/release-gates.py --finalize-soak
scripts/package-personal.sh
open artifacts/LocalVoiceTranslator.app
```

`validate-personal.sh` executa os testes nativos, XCTest, ASR, vozes/cancelamento,
conversa e cenários. O gate de empacotamento exige evidência de 30 minutos para o motor
atual e hashes de todos os arquivos dos modelos. Mudanças nos fontes invalidam a
aceitação automatizada. A aceitação física/offline permanece explicitamente separada.
`build-release.sh --diagnostic` permite compilar uma cópia de diagnóstico identificada
quando um teste ainda está em investigação.

Fixtures sintéticos: `scripts/generate-speech-fixtures.sh` usa somente vozes macOS já
instaladas, durante desenvolvimento. `scripts/generate-conversation-fixtures.py` monta
os cenários. Gravações humanas FLEURS (CC-BY-4.0) são restauradas separadamente com
`/usr/bin/python3 scripts/fetch-human-fixtures.py`; nunca entram no aplicativo.

## Limites conhecidos

- Até 64 identidades por sessão; separação limitada a duas vozes simultâneas. IDs são
  estimativas por embeddings. Sotaques, falas curtas, ruído e sobreposição podem dificultá-los.
- Há três pares fixos de vozes PT/EN; participantes adicionais reutilizam os pares.
- Tradução e ASR podem errar nomes, números e sentido. O modelo PT→EN pode transformar
  João em John. As referências de desenvolvimento não representam toda conversa real.
- Separação mostrou resultados diferentes conforme idioma e relação entre volumes;
  trechos sem separação confirmada são sinalizados. TV com fala não é distinguida de
  uma pessoa relevante por uma regra de intenção inexistente.
- Balanced é o padrão aprovado. Eco/Whisper base ainda apresentou perda de sentido no
  teste completo, e Quality/FP32 não passou pelo orçamento de inferência. Consulte [benchmarks](BENCHMARKS.md).

## Privacidade e limpeza

```sh
scripts/verify-offline.sh artifacts/LocalVoiceTranslator.app
scripts/audit-machine-impact.sh
scripts/clean-all.sh --dry-run
scripts/clean-all.sh
```

A limpeza limita-se à allowlist dentro do projeto e preserva fontes/manifestos/fixtures.
Não altera Homebrew, serviços, perfis de shell, drivers, rede, TCC ou estado do macOS.
Os temporários internos do Xcode já foram autorizados em
[BUILD_AUTHORIZATION.md](docs/BUILD_AUTHORIZATION.md).

Detalhes: [arquitetura](ARCHITECTURE.md), [privacidade](PRIVACY.md),
[dependências](THIRD_PARTY.md), [licenças de modelos](MODEL_LICENSES.md),
[estado das fases](docs/IMPLEMENTATION_STATUS.md).

### Uso em chamadas

A aba **Áudio → Chamada** configura dois sentidos: seu microfone PT-BR → inglês
para a chamada; áudio inglês da chamada → português nos seus fones. Precisa de
**dois dispositivos virtuais independentes**, além do microfone e dos fones.
Leia [como conectar Meet, Zoom, Teams, Discord e Slack](docs/CALL_AUDIO_SETUP.md).
O processamento duplex é testado com WAV; a preparação nativa pela interface e o
transporte PCM entre processos passaram. Chamadas reais continuam como aceitação manual pendente. O aplicativo não altera as
configurações da chamada nem instala drivers automaticamente.

### Aceleração Apple

Ajustes permite comparar Metal e Core ML + Metal com a sessão parada. Metal continua
o padrão por precisão; Core ML é experimental. O reaproveitamento do encoder reduziu
a mediana do ASR isolado em ~35% (PT) e ~31% (EN) nas gravações medidas.
Veja [implementação, resultados e limites](docs/APPLE_ACCELERATION.md).
