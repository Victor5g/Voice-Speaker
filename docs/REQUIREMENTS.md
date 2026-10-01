# OBJETIVO

Crie um aplicativo macOS nativo, completo e funcional, provisoriamente chamado **LocalVoiceTranslator**.

O aplicativo é um **tradutor conversacional de áudio em tempo real**, totalmente local.

O primeiro hardware-alvo é:

* macOS
* Apple Silicon
* Apple M2 Pro
* 16 GB de memória unificada

O primeiro release deve suportar EXCLUSIVAMENTE:

**Português brasileiro ⇄ Inglês**

A direção da tradução deve ser detectada automaticamente para cada fala:

* fala detectada como português brasileiro → traduzir para inglês;
* fala detectada como inglês → traduzir para português brasileiro.

Não deve existir necessidade de selecionar manualmente a direção da tradução.

O aplicativo deve traduzir tanto:

* a fala do próprio usuário capturada pelo microfone;
* quanto a fala de outras pessoas presentes no ambiente.

O resultado traduzido deve ser reproduzido pelo alto-falante.

Fluxo fundamental:

MICROFONE
→ detectar quem está falando
→ detectar PT-BR ou inglês
→ reconhecer fala
→ traduzir para o idioma oposto
→ gerar voz no idioma traduzido
→ ALTO-FALANTE

Exemplo:

Usuário fala:

"Vamos começar a reunião."

Aplicativo:

PT-BR detectado
→ "Let's start the meeting."
→ reproduzir a frase em inglês pelo alto-falante.

Outra pessoa fala:

"I think we should wait."

Aplicativo:

EN detectado
→ "Acho que devemos esperar."
→ reproduzir a frase em português brasileiro.

O processo deve ocorrer automaticamente.

---

# 1. PRINCÍPIOS DO PRODUTO

O produto deve ser extremamente simples.

NÃO existe:

* login;
* cadastro;
* conta;
* servidor;
* cloud;
* sincronização;
* analytics;
* tracking;
* publicidade;
* telemetria remota;
* crash reporting remoto;
* assinatura;
* backend.

O processamento deve ser:

**100% local.**

O aplicativo deve continuar funcionando normalmente quando:

* Wi-Fi estiver desligado;
* Ethernet estiver desligada;
* o computador não tiver acesso à internet.

Nenhum:

* áudio;
* texto;
* tradução;
* embedding;
* identificação de speaker;
* métrica;
* informação do usuário

pode sair da máquina.

---

# 2. NÃO POLUIR A MÁQUINA

Esta é uma exigência crítica.

NÃO instale dependências globalmente.

É proibido executar automaticamente:

brew install
brew upgrade
brew tap
pip install global
sudo pip
npm install -g
gem install
sudo
modificações em /usr/local
modificações em /opt/homebrew
modificações em /Library
modificações em ~/.zshrc
modificações em ~/.bashrc
modificações permanentes de PATH
LaunchAgents
LaunchDaemons
serviços persistentes
kernel extensions

Não altere configurações globais do macOS.

Assuma como únicos pré-requisitos externos:

* macOS;
* Xcode;
* Xcode Command Line Tools.

Todo restante deve ficar dentro do diretório do projeto.

Utilizar diretórios como:

./third_party/
./tools/
./models/
./build/
./.build/
./cache/
./artifacts/
./tests/data/

Configure explicitamente o Xcode para colocar DerivedData dentro do projeto:

-derivedDataPath ./build/DerivedData

Todos os caches controláveis também devem permanecer dentro do projeto.

Se qualquer comando precisar escrever fora do repositório:

NÃO execute.

Documente o motivo.

---

# 3. TECNOLOGIA

Não use:

* React Native;
* Electron;
* Qt;
* Flutter;
* Tauri;
* browser embutido.

Utilize:

## UI

Swift / SwiftUI

## Engine

C++20

## Bridge

Objective-C++ apenas onde necessário.

## Áudio

Preferencialmente:

* CoreAudio;
* AudioUnit;
* AVAudioEngine;
* VoiceProcessingIO.

## Hardware acceleration

Utilizar quando apropriado:

* Metal;
* Accelerate;
* Apple Silicon NEON.

## Build

* Xcode project;
* xcodebuild.

Evite dependência global de CMake.

Se uma biblioteca exigir CMake internamente:

* mantenha ferramenta local dentro de `./tools/`; ou
* integre a dependência diretamente ao Xcode quando tecnicamente melhor.

---

# 4. ESCOPO DE IDIOMAS

O primeiro release suporta somente:

## Português

Português brasileiro.

Código lógico interno:

pt-BR

O ASR pode internamente retornar `pt`, mas o produto deve tratar esse idioma como português brasileiro dentro deste release.

## Inglês

Suportar reconhecimento das principais variantes e sotaques do inglês, incluindo:

* inglês americano;
* inglês britânico;
* inglês canadense;
* inglês australiano;
* demais sotaques comuns suportados pelo modelo.

Não é necessário classificar cada sotaque como um idioma diferente.

Internamente:

en

Para síntese de voz, disponibilizar pelo menos:

* English US;
* opcionalmente English UK.

Default:

English US.

---

# 5. REGRA DE TRADUÇÃO

A direção é automática.

Implementar explicitamente:

if detected_language == Portuguese:
translate PT-BR → EN
synthesize English voice

else if detected_language == English:
translate EN → PT-BR
synthesize Brazilian Portuguese voice

else:
mark segment as unsupported
DO NOT guess
DO NOT translate

O usuário não deve precisar escolher:

"Português para inglês"

ou

"Inglês para português".

A direção é determinada individualmente em cada segmento.

---

# 6. SUA PRÓPRIA VOZ TAMBÉM É TRADUZIDA

Este requisito é obrigatório.

Não construa o sistema supondo:

"microfone = outra pessoa".

Qualquer pessoa capturada pelo microfone é participante da conversa.

Isso inclui o usuário do computador.

Exemplo:

Speaker 1 = usuário do Mac.

Speaker 2 = interlocutor.

O fluxo precisa funcionar assim:

Speaker 1 fala PT-BR
→ PT-BR detectado
→ tradução para EN
→ voz inglesa no alto-falante.

Speaker 2 fala EN
→ EN detectado
→ tradução para PT-BR
→ voz brasileira no alto-falante.

---

# 7. FULL DUPLEX

O programa deve:

* ouvir continuamente;
* traduzir;
* falar a tradução pelo alto-falante;
* continuar ouvindo enquanto o áudio traduzido é reproduzido.

Portanto:

FULL DUPLEX REAL.

O áudio produzido pelo próprio aplicativo NÃO deve entrar novamente no pipeline como se fosse um novo participante.

Implementar proteção em múltiplas camadas:

1. Acoustic Echo Cancellation;
2. referência do playback;
3. VoiceProcessingIO quando adequado;
4. Noise Suppression;
5. detecção de self-playback quando necessária.

Criar teste específico:

TTS output
→ speakers
→ microphone leakage

Resultado esperado:

não gerar novo segmento de tradução.

---

# 8. PIPELINE

Arquitetura geral:

Microphone
↓
Audio Capture
↓
AEC / Noise Suppression
↓
VAD
↓
Overlap Detection
↓
┌───────────────────────────────────┐
│ sem fala simultânea               │
│          ↓                        │
│ diarização                        │
│                                   │
│ fala simultânea                   │
│          ↓                        │
│ speech separation                 │
│          ↓                        │
│ streams separados                 │
│          ↓                        │
│ diarização por stream             │
└───────────────────────────────────┘
↓
Speaker Tracking
↓
ASR
↓
Language Detection
↓
Language Stabilization
↓
PT-BR ↔ EN Router
↓
Translation
↓
Segment Stability
↓
Voice Manager
↓
TTS
↓
Audio Mixer
↓
Playback

---

# 9. ARQUITETURA CONCORRENTE

Não execute o pipeline inteiro de maneira serial.

Use workers independentes.

Exemplo conceitual:

Realtime Audio Thread
↓
Audio Queue
↓
VAD Worker
↓
Diarization Worker
↓
ASR Worker
↓
Translation Worker
↓
TTS Worker
↓
Playback Queue

Overlap/separation deve ter worker específico.

Utilizar:

* std::jthread;
* std::stop_token;
* std::atomic;
* bounded queues;
* SPSC queues quando apropriado.

Não utilizar um mutex global.

---

# 10. REALTIME AUDIO THREAD

A thread realtime de áudio nunca pode:

* executar ASR;
* executar tradução;
* executar TTS;
* carregar modelo;
* esperar mutex pesado;
* alocar grandes blocos;
* chamar UI;
* fazer I/O de disco;
* realizar logging pesado.

Ela deve basicamente:

capturar
→ copiar para buffer previamente alocado
→ enfileirar
→ retornar.

---

# 11. BACKPRESSURE

Todas as filas devem possuir limite.

Nunca permita crescimento ilimitado.

Cada fila deve possuir métricas:

currentDepth
maxDepth
droppedItems
oldestItemAgeMs

Implementar política explícita para congestionamento.

A realtime audio thread nunca deve bloquear.

---

# 12. VAD

Use VAD local e de baixa latência.

Avalie:

* WebRTC VAD;
* Silero VAD;
* outra implementação tecnicamente superior.

Não use Python em runtime.

O VAD deve ser adequado a streaming.

---

# 13. DETECÇÃO AUTOMÁTICA DE MÚLTIPLOS FALANTES

O usuário não deve informar:

* quantidade de pessoas;
* identidade das pessoas;
* gênero;
* voz.

O aplicativo deve descobrir automaticamente.

Cada pessoa deve receber:

Speaker 1
Speaker 2
Speaker 3
...

Implementar:

SpeakerEmbeddingEngine
OnlineDiarizer
SpeakerTracker

Avaliar inicialmente:

ECAPA-TDNN

ou solução tecnicamente superior e compatível com processamento totalmente local.

O tracker deve:

* criar speakers dinamicamente;
* manter IDs estáveis;
* evitar duplicação excessiva;
* reconhecer quando speaker anterior volta a falar;
* atualizar centroides;
* realizar merge quando necessário;
* lidar com participantes inativos por vários minutos.

Não usar apenas pitch, volume ou energia como identidade.

---

# 14. FALA SIMULTÂNEA

Diarização NÃO é speech separation.

Implemente separadamente:

OverlapDetector

Se não houver overlap:

audio
→ diarization
→ ASR.

Se houver duas ou mais vozes simultâneas:

audio
→ SpeechSeparator
→ stream A
→ stream B
→ ...
→ embeddings
→ speaker assignment
→ ASR de cada stream.

A primeira versão pode possuir limite tecnicamente justificado para quantidade de vozes simultâneas, por exemplo 2.

Mas deve suportar número maior de participantes na conversa total.

Não execute speech separation pesada permanentemente.

Preferir:

lazy/on-demand.

---

# 15. ASR

Use inicialmente:

whisper.cpp

com suporte adequado a Apple Silicon e Metal.

O runtime não pode depender de Python.

Objetivo inicial no M2 Pro 16 GB:

avaliar por benchmark:

* Whisper tiny;
* Whisper base;
* Whisper small.

Escolher automaticamente o modelo default com base no benchmark real.

Não inventar performance.

---

# 16. DETECÇÃO DE IDIOMA

Apenas dois idiomas são aceitos:

PT-BR
EN

O ASR pode fornecer probabilidade para múltiplos idiomas, mas o `LanguageRouter` deve considerar somente PT e EN.

Não determine idioma a partir de uma janela extremamente pequena isoladamente.

Criar:

LanguageStabilizer

Manter estado por speaker.

Exemplo:

Speaker 1 historicamente PT
segmento muito curto parece ES com baixa confiança
→ NÃO mudar idioma.

Mas permitir mudança real:

Speaker 1 fala português.
Depois começa conscientemente a falar inglês.
→ após evidência suficiente, trocar para EN.

A UI deve exibir:

PT-BR
ou
EN

por segmento/speaker.

---

# 17. TRADUÇÃO LOCAL

A tradução precisa ser completamente offline.

Para este primeiro release NÃO precisamos de tradução multilíngue genérica.

Precisamos somente:

PT-BR → EN
EN → PT-BR

Isso deve ser utilizado para:

* reduzir RAM;
* reduzir tamanho dos modelos;
* aumentar velocidade;
* aumentar qualidade específica do par.

Avaliar tecnicamente:

* CTranslate2;
* Marian;
* modelos especializados PT↔EN;
* alternativas locais mais modernas.

Escolher utilizando:

qualidade
latência
RAM
licença
tamanho
Apple Silicon compatibility.

Não escolher automaticamente um modelo enorme por possuir mais idiomas.

---

# 18. TTS

A tradução deve sair em voz.

Precisamos de pelo menos:

## PT-BR

pelo menos 2 vozes locais se tecnicamente possível.

## EN

pelo menos 2 vozes locais.

Avaliar:

* Piper;
* sherpa-onnx;
* alternativa local superior.

Não utilizar API cloud.

Não utilizar TTS que dependa de internet.

Não precisamos de voice cloning.

Não precisamos imitar a voz real da pessoa.

---

# 19. VOZ DIFERENTE POR PARTICIPANTE

Criar:

VoiceManager

Quando um novo speaker aparece:

Speaker 1 → voice A
Speaker 2 → voice B
Speaker 3 → voice C

Manter essa atribuição durante toda a sessão.

A voz escolhida depende do idioma DE SAÍDA.

Exemplo:

Speaker 1 fala PT-BR.

Output = EN.

VoiceManager usa uma voz inglesa associada a Speaker 1.

Quando Speaker 1 posteriormente falar inglês:

Output = PT-BR.

Usar a voz PT-BR correspondente ao mesmo perfil lógico.

Manter consistência razoável.

Exemplo:

Speaker 1:
English voice A
Portuguese voice A

Speaker 2:
English voice B
Portuguese voice B

Não é necessário corresponder identidade vocal real.

---

# 20. EVITAR VOZES SE ATROPELANDO

Crie:

OutputScheduler

Se duas traduções estiverem prontas simultaneamente:

não simplesmente reproduzir tudo por cima.

Implementar política configurável, inicialmente:

preservar ordem temporal
+
pequeno crossfade quando apropriado
+
fila por speaker
+
priorização coerente.

Mostrar congestionamento na tela Engine.

---

# 21. STREAMING E BAIXA LATÊNCIA

Não espere necessariamente uma frase inteira terminar.

Utilizar:

* VAD;
* partial ASR;
* estabilidade de tokens;
* pontuação;
* silêncio;
* contexto.

Criar:

SegmentStabilityManager

Somente comprometer tradução para TTS quando o prefixo estiver suficientemente estável.

Não reproduzir tradução que precise ser posteriormente "desdita".

Objetivo:

reduzir tempo entre fala e primeiro áudio traduzido sem destruir qualidade linguística.

---

# 22. MÉTRICAS PRINCIPAIS

Não use uma meta fictícia fixa de latência.

Medir no hardware real:

TTFT:
Time To First Translated Audio.

E2E:
Speech start → first translated playback.

Também medir:

Audio capture latency
VAD
Overlap Detection
Speech Separation
Diarization
ASR
Translation
TTS
Playback

Mostrar:

P50
P95

quando houver amostra suficiente.

Medir também:

ASR Real-Time Factor.

---

# 23. MEMORY BUDGET

O hardware inicial possui:

16 GB unified memory.

Objetivo de engenharia:

uso normal <= 8 GB.

Preferencialmente significativamente menor.

Criar:

MemoryBudgetManager

Monitorar:

* RAM do processo;
* peak RSS;
* tamanho estimado de cada modelo;
* caches;
* buffers;
* memory pressure do sistema.

Speech separation deve preferencialmente ser:

carregada sob demanda
ou
descarregada se o orçamento exigir.

Evitar swap excessivo.

---

# 24. PERFIS

Criar:

Eco
Balanced
Quality

Default:

Balanced.

Não associar modelos arbitrariamente sem benchmark.

No primeiro start, ou por ferramenta de benchmark de desenvolvimento:

medir hardware.

Para M2 Pro 16 GB, testar configurações apropriadas e armazenar somente uma preferência local técnica, sem qualquer informação pessoal.

---

# 25. INTERFACE PRINCIPAL

A UI deve ser minimalista.

Criar tela:

## Conversation

Exemplo:

┌──────────────────────────────────────────────┐
│ LocalVoiceTranslator                         │
│                                              │
│         PT-BR ⇄ EN   AUTO                    │
│                                              │
│               ● LISTENING                    │
│                                              │
│ Speaker 1                             PT-BR  │
│ Vamos começar agora.                        │
│ Let's start now.                       🔊    │
│                                              │
│ Speaker 2                                EN  │
│ Give me five minutes.                       │
│ Me dê cinco minutos.                   🔊    │
│                                              │
│ Speaker 3                                EN  │
│ I'm ready.                                  │
│ Estou pronto.                          🔊    │
│                                              │
│                   STOP                      │
└──────────────────────────────────────────────┘

Não colocar configurações avançadas nessa tela.

---

# 26. ENGINE — GERENCIADOR DE TAREFAS

Criar uma segunda tela denominada:

Engine

Inspirada conceitualmente no Activity Monitor do macOS.

Mostrar dados REAIS.

## System

CPU %
RAM
Peak RAM
Memory pressure
Thermal state
Threads
Metal active/inactive

## Pipeline

Audio
VAD
Overlap
Separation
Diarization
ASR
Translation
TTS
Playback

Mostrar latências.

## Overall

TTFT P50
TTFT P95
E2E P50
E2E P95
ASR RTF

## Queues

Para cada:

Audio
VAD
Separation
Diarization
ASR
Translation
TTS
Playback

mostrar:

current
capacity
dropped
oldest age.

## Speakers

Exemplo:

Speaker 1 | PT-BR | speaking
Speaker 2 | EN    | idle
Speaker 3 | EN    | idle

## Models

ASR:
name
quantization
backend
estimated memory

Translation:
model
backend
memory

Diarization:
model

TTS:
model
voices loaded

Separation:
loaded yes/no

## Audio

Input level
Output level
AEC status
Noise suppression
Underruns
Overruns

## Overlap

Overlap %
Events
Separation calls
Average separation latency

---

# 27. GRÁFICOS

Usar SwiftUI nativo.

Evitar dependência externa se não necessária.

Sparklines para:

CPU
RAM
TTFT
E2E
ASR latency
ASR queue

Manter histórico curto em memória.

Não persistir.

---

# 28. PRIVACIDADE E DADOS

Por padrão:

áudio = memória
texto = memória
tradução = memória
speaker embeddings = memória
métricas = memória

Ao pressionar STOP:

* encerrar captura;
* parar workers;
* limpar filas;
* destruir textos;
* destruir embeddings da sessão;
* limpar áudio;
* remover histórico da UI.

Ao fechar o aplicativo:

nenhum histórico de conversa deve permanecer.

---

# 29. APP SANDBOX

Tentar manter:

App Sandbox = enabled.

Entitlements mínimos.

Necessário:

audio input.

Não adicionar network entitlement.

Se uma API essencial não funcionar dentro do sandbox:

primeiro reproduza o problema;
documente evidência;
só então proponha alteração.

Não desligar sandbox apenas por conveniência.

---

# 30. MODELOS

O app Release deve ter todos os componentes necessários para funcionar offline.

Não faça download automático durante runtime.

Durante desenvolvimento:

models/
manifest.json

Cada modelo deve possuir:

name
type
version
filename
sha256
license
source
expectedSize
backend

Validar SHA-256 antes do uso.

Se os modelos não puderem ser redistribuídos legalmente:

não os inclua ilegalmente.

Documente:

* origem oficial;
* licença;
* hash esperado;
* caminho onde devem ser colocados.

Não substitua por mock.

---

# 31. DEPENDÊNCIAS

Armazenar em:

third_party/

Pode utilizar:

* código vendorizado;
* git submodules fixados;
* Swift Package local quando adequado.

Sempre fixar versão/commit.

Nunca usar:

main
master
latest

como dependência flutuante.

Criar:

THIRD_PARTY.md

com:

library
version
commit
license
upstream
purpose
local modifications.

---

# 32. ESTRUTURA DO REPOSITÓRIO

Criar aproximadamente:

LocalVoiceTranslator/
├── LocalVoiceTranslator.xcodeproj
├── README.md
├── ARCHITECTURE.md
├── PRIVACY.md
├── THIRD_PARTY.md
├── MODEL_LICENSES.md
├── BENCHMARKS.md
│
├── app/
│   ├── LocalVoiceTranslatorApp.swift
│   ├── AppState.swift
│   ├── Views/
│   │   ├── ConversationView.swift
│   │   ├── EngineView.swift
│   │   ├── SettingsView.swift
│   │   └── AboutView.swift
│   └── Components/
│
├── bridge/
│   ├── TranslationEngineBridge.h
│   └── TranslationEngineBridge.mm
│
├── engine/
│   ├── audio/
│   ├── vad/
│   ├── overlap/
│   ├── separation/
│   ├── diarization/
│   ├── asr/
│   ├── language/
│   ├── translation/
│   ├── tts/
│   ├── pipeline/
│   ├── scheduler/
│   ├── monitoring/
│   ├── memory/
│   └── common/
│
├── models/
├── third_party/
├── tools/
├── scripts/
├── tests/
│   ├── cpp/
│   ├── swift/
│   └── data/
├── build/
└── artifacts/

---

# 33. TESTES UNITÁRIOS

Criar testes para:

RingBuffer
BoundedQueue
VAD segmentation
OnlineDiarizer
SpeakerTracker
LanguageStabilizer
LanguageRouter
SegmentStabilityManager
TranslationRouter
VoiceManager
OutputScheduler
AudioMixer
PerformanceMonitor
MemoryBudgetManager

---

# 34. TESTES DE INTEGRAÇÃO

Testar localmente com WAVs.

Cenários obrigatórios:

1. PT-BR → EN.

2. EN-US → PT-BR.

3. EN-GB → PT-BR.

4. Inglês com sotaques distintos.

5. Um speaker alternando PT e EN.

6. Dois speakers alternando.

7. Três speakers alternando.

8. Speaker sai e retorna depois.

9. Duas pessoas falando simultaneamente.

10. Ruído ambiente.

11. Silêncio.

12. Música/TV sem fala relevante.

13. TTS tocando enquanto microfone permanece ativo.

14. Self-playback não pode gerar tradução recursiva.

15. Mudanças rápidas de idioma não podem causar oscillation falsa.

---

# 35. GOLDEN TEST PT-BR ⇄ EN

Criar teste offline que aceite:

tests/data/conversation_ptbr_en.wav

Pipeline completo:

WAV
→ VAD
→ diarization
→ ASR
→ language detection
→ PT/EN routing
→ translation
→ TTS

Gerar saída somente em:

artifacts/test-output/

Imprimir:

Speakers detected
Speaker IDs
Detected language
Source text
Translated text
ASR latency
Translation latency
TTS latency
TTFT approximation
E2E approximation
Peak RAM
ASR RTF

Não inventar valores.

---

# 36. BENCHMARK M2 PRO

Criar:

scripts/benchmark.sh

Detectar hardware.

Benchmarkar modelos candidatos realmente disponíveis.

Especialmente:

Whisper tiny
Whisper base
Whisper small

Comparar quando aplicável:

quantizações diferentes.

Registrar:

RTF
latency P50
latency P95
RSS
peak RSS
thermal state

Atualizar:

BENCHMARKS.md

Somente com números efetivamente medidos.

Se ainda não medido:

NOT MEASURED.

---

# 37. SCRIPTS

Criar:

scripts/bootstrap-local.sh
scripts/build-debug.sh
scripts/build-release.sh
scripts/run-tests.sh
scripts/benchmark.sh
scripts/verify-offline.sh
scripts/audit-machine-impact.sh
scripts/clean-build.sh
scripts/clean-all.sh

Nenhum script pode utilizar sudo.

Nenhum pode instalar Homebrew.

---

# 38. CLEAN ALL

`clean-all.sh` deve remover:

* build;
* DerivedData local;
* caches locais;
* tools locais opcionais;
* modelos opcionais;
* artefatos temporários.

Antes de `rm -rf`:

resolver path com `realpath`.

Confirmar programaticamente que o path está dentro do repository root.

Se não estiver:

ABORT.

Nunca remover caminho arbitrário.

---

# 39. AUDITORIA DA MÁQUINA

Criar:

scripts/audit-machine-impact.sh

Resultado ideal:

PROJECT LOCAL FILES:
[...]

FILES CREATED OUTSIDE PROJECT:
NONE

Documentar que alguns estados internos do próprio macOS/Xcode podem existir, como permissões TCC de microfone, mas o projeto não deve instalar arquivos ou serviços persistentes.

---

# 40. VERIFY OFFLINE

Criar:

scripts/verify-offline.sh

Inspecionar:

* binário;
* linked frameworks;
* strings;
* endpoints;
* URLs;
* dependências;
* uso inesperado de networking.

Release não deve depender de:

HTTP
WebSockets
cloud APIs
analytics.

Testar aplicação com rede desligada.

---

# 41. NÃO USAR IMPLEMENTAÇÃO FAKE

Proibido:

dummyTranslation
fakeSpeaker
fakeLatency
fakeCPU
randomMetrics
hardcodedDetectedLanguage
sleep para simular inferência
mock ASR em release
mock TTS em release

Mocks somente em testes unitários onde explicitamente apropriado.

Se algo ainda não estiver funcionando:

marcar como:

NOT IMPLEMENTED

e continuar trabalhando.

Não declarar funcionalidade concluída.

---

# 42. ORDEM DE IMPLEMENTAÇÃO

Implementar incrementalmente.

## PHASE 1

SwiftUI application shell.

CoreAudio microphone capture.

Audio playback.

Compilar.

Executar testes.

## PHASE 2

Full-duplex.

VoiceProcessingIO/AEC.

Teste contra self-playback.

## PHASE 3

VAD.

## PHASE 4

whisper.cpp + Metal.

PT/EN speech recognition.

Language detection.

## PHASE 5

LanguageStabilizer.

PT-BR ⇄ EN routing.

## PHASE 6

Translation local PT-BR ⇄ EN.

## PHASE 7

Local TTS PT-BR e EN.

## PHASE 8

Pipeline real:

mic
→ ASR
→ translation
→ TTS
→ speaker.

## PHASE 9

Speaker embeddings.

## PHASE 10

Automatic online diarization.

## PHASE 11

Stable SpeakerTracker.

## PHASE 12

Overlap detection.

## PHASE 13

Speech separation.

## PHASE 14

Partial streaming.

SegmentStabilityManager.

## PHASE 15

PerformanceMonitor.

## PHASE 16

Engine screen.

## PHASE 17

MemoryBudgetManager.

## PHASE 18

Benchmark e tuning no M2 Pro.

## PHASE 19

Offline/security audit.

## PHASE 20

Release hardening.

Depois de cada fase:

BUILD
TEST
FIX

antes de continuar.

---

# 43. DEFINITION OF DONE

O projeto somente pode ser chamado de funcional quando em um Apple Silicon real:

1. Compila via xcodebuild.

2. Gera `.app`.

3. Abre normalmente.

4. Solicita permissão de microfone.

5. Funciona sem internet.

6. Usuário fala PT-BR.

7. Sistema detecta português.

8. Sistema transcreve corretamente em nível funcional.

9. Sistema traduz localmente PT-BR → EN.

10. Sistema gera voz inglesa.

11. Voz inglesa sai pelo alto-falante.

12. Usuário fala inglês.

13. Sistema detecta inglês.

14. Sistema traduz EN → PT-BR.

15. Sistema gera voz PT-BR.

16. Voz PT-BR sai pelo alto-falante.

17. Não exige escolha manual da direção.

18. O próprio usuário é tratado como speaker normal.

19. Dois participantes recebem IDs diferentes.

20. Mesmo participante mantém ID ao voltar.

21. Três ou mais participantes podem existir na sessão.

22. Inglês com diferentes sotaques é reconhecido como EN.

23. Fala simultânea é detectada.

24. Speech separation funciona quando modelo estiver disponível.

25. Microfone continua funcionando durante playback.

26. O próprio TTS não entra em loop de tradução.

27. ConversationView mostra resultado em tempo real.

28. EngineView mostra métricas reais.

29. Stop limpa a sessão.

30. Nenhum conteúdo da conversa fica persistido.

31. Nenhuma chamada de rede é realizada pelo app.

32. Uso de RAM no perfil Balanced é compatível com máquina de 16 GB e alvo <=8 GB em operação normal.

33. `clean-all.sh` limpa todo ambiente controlado pelo projeto.

34. `audit-machine-impact.sh` não encontra instalação persistente feita pelo projeto fora de seu diretório.

---

# 44. RELEASE

O objetivo final é produzir:

artifacts/LocalVoiceTranslator.app

e, quando possível:

artifacts/LocalVoiceTranslator.dmg

Preparar estrutura adequada para posteriormente:

* code signing;
* notarization;
* distribuição direta;
* eventual Mac App Store se tecnicamente compatível.

Não invente certificados.

Não tente contornar assinatura.

Se signing identity não estiver disponível:

gerar build local unsigned/ad-hoc quando tecnicamente apropriado e documentar o passo faltante.

---

# 45. COMPORTAMENTO ESPERADO DO CODEX

Você tem acesso ao terminal e aos arquivos do projeto.

NÃO responda apenas com um tutorial.

Trabalhe diretamente no repositório.

Crie arquivos.

Compile.

Execute testes.

Leia os erros.

Corrija os erros.

Recompile.

Repita.

Não faça alterações globais no computador.

Não use Homebrew.

Não use sudo.

Não esconda erros.

Não diga que uma funcionalidade funciona sem tê-la testado.

Quando houver limitação causada por:

* ausência de modelo;
* licença;
* assinatura;
* hardware;
* API do macOS;

documente precisamente.

Nunca substitua a funcionalidade por uma simulação para passar testes.

---

# 46. RELATÓRIO FINAL DO CODEX

Ao terminar a execução possível, apresente exatamente:

## Build

* Debug: PASS/FAIL
* Release: PASS/FAIL
* caminho do `.app`

## Audio

* capture
* playback
* full-duplex
* AEC
* self-playback suppression

Cada item:

PASS / FAIL / NOT IMPLEMENTED

## AI

* VAD
* diarization
* overlap detection
* speech separation
* ASR PT-BR
* ASR EN
* language detection
* translation PT→EN
* translation EN→PT
* TTS PT-BR
* TTS EN

## Performance

Somente resultados medidos:

* RAM idle
* RAM translating
* peak RAM
* CPU
* ASR RTF
* TTFT P50/P95
* E2E P50/P95
* thermal state

## Privacy

* network calls found
* persistent files outside repo
* conversation persistence

## Tests

* number passed
* number failed
* failures

## Remaining issues

Lista objetiva.

## Machine impact

Resultado do:

audit-machine-impact.sh

## Artifacts

Caminhos exatos.

---

# 47. PRIMEIRA AÇÃO

Antes de modificar qualquer coisa:

1. Detecte versão do macOS.
2. Detecte CPU/SoC.
3. Detecte RAM.
4. Detecte Xcode.
5. Detecte Command Line Tools.
6. Inspecione o diretório atual.
7. Não instale nada.

Depois:

crie a estrutura do projeto.

Comece pela PHASE 1.

Só prossiga depois que ela compilar e os testes correspondentes passarem.
