#!/usr/bin/python3
"""Render measured evidence without converting pending acceptance into success."""
from pathlib import Path
import json, datetime, statistics, hashlib
root=Path(__file__).resolve().parents[2]
a=root/'artifacts'
def rows(name):
    p=a/name
    return [json.loads(line) for line in p.read_text().splitlines() if line.startswith('{')] if p.is_file() else []
def last(name):
    r=rows(name); return r[-1] if r else {}
def mib(x): return f'{x/1024**2:.1f}'
def gib(x): return f'{x/1024**3:.2f}'
def state(r): return r.get('result','PENDING')
def num(r,k): return f'{r[k]:.1f}' if r.get(k) is not None else 'não disponível'
profiles=json.loads((a/'profile-comparison.json').read_text())
soak=last('soak-30min.jsonl')
engineHash=hashlib.sha256()
for path in sorted(p for folder in ('engine','bridge') for p in (root/folder).rglob('*') if p.is_file()):
    engineHash.update(str(path.relative_to(root)).encode());engineHash.update(path.read_bytes())
provenance=json.loads((a/'soak-provenance.json').read_text()) if (a/'soak-provenance.json').exists() else {}
soakOK=state(soak)=='PASS' and provenance.get('engineFingerprint')==engineHash.hexdigest()
callGolden=last('call-routing-golden.jsonl')
opposing=last('call-opposing-directions.jsonl')
callSoak=last('call-soak-30min.jsonl')
callProvenance=json.loads((a/'call-soak-provenance.json').read_text()) if (a/'call-soak-provenance.json').exists() else {}
callSoakOK=state(callSoak)=='PASS' and callProvenance.get('engineFingerprint')==engineHash.hexdigest()
customAudio=(json.loads((a/'custom-audio-verification.json').read_text()) if (a/'custom-audio-verification.json').exists() else {})
metrics=[r for r in rows('soak-30min.jsonl') if r.get('metric')]
reliability=last('reliability-tests.log')
golden=last('conversation-golden.jsonl');human=last('human-conversation-golden.jsonl');echo=last('digital-echo.jsonl');network=last('no-network-golden.jsonl')
acceptance=json.loads((a/'automated-acceptance.json').read_text()) if (a/'automated-acceptance.json').exists() else {}
idle=rows('release-final-idle-memory.jsonl') or rows('app-idle-memory.jsonl')
releaseUI=json.loads((a/'release-final-ui.json').read_text()) if (a/'release-final-ui.json').exists() else {}
packaging=json.loads((a/'packaging-only-revalidation.json').read_text()) if (a/'packaging-only-revalidation.json').exists() else {}
app=a/'LocalVoiceTranslator.app'; dmg=a/'LocalVoiceTranslator.dmg'
size=sum(p.stat().st_size for p in app.rglob('*') if p.is_file()) if app.exists() else 0
sha=(a/'LocalVoiceTranslator.dmg.sha256').read_text().split()[0] if (a/'LocalVoiceTranslator.dmg.sha256').exists() else 'PENDING'
profileTable='\n'.join(f"| {Path(r['model']).stem} | {state(r['fullPipeline'])} | {mib(r['fullPipeline']['peakRSSBytes']) if 'peakRSSBytes' in r['fullPipeline'] else 'não medido neste ensaio'} | {100*r['humanMeanWER']:.2f}% | {r['fullPipeline'].get('firstAudioSamples',0)} |" for r in profiles)
mtTable=[]
for direction in ('pt-en','en-pt'):
    for precision in ('float32','int8'):
        data=rows(f'translation-{direction}-{precision}.jsonl')
        mtTable.append(f"| {direction} | {precision} | {len(data)} | {statistics.mean(r['latencyMs'] for r in data):.1f} | {mib(max(r['peakRSSBytes'] for r in data))} |")
bench=f'''# Medições no Apple M2 Pro

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
{profileTable}

Balanced usa small/INT8 pela precisão observada. Base/INT8 usou menos memória, mas
falhou na reavaliação completa: “reunião” virou “reunir” e a tradução “gathering”.
Eco foi retido após essa regressão, apesar do ensaio anterior aprovado. Tiny excedeu
o limiar exploratório WER 20%. Quality com
tradução FP32 foi medido, mas a admissão conservadora de memória impediu uma síntese;
permanece indisponível. Nenhum perfil foi escolhido apenas pelo nome/tamanho do modelo.
`artifacts/profile-comparison.json` contém as transcrições e configurações completas.

## Sessão contínua pelo pipeline real

Estado: **{'PASS' if soakOK else 'PENDING'}**, fixture sintética repetida em tempo real, sem dispositivo físico.
Duração: {num(soak,'durationSeconds')} s. Falas reproduzidas: {soak.get('playedSegments','PENDING')}.
Primeiro áudio: {soak.get('firstAudioSamples','PENDING')} amostras; P50 {num(soak,'firstAudioP50Ms')} ms,
P95 {num(soak,'firstAudioP95Ms')} ms. E2E: P50 {num(soak,'e2eP50Ms')} ms,
P95 {num(soak,'e2eP95Ms')} ms. Os percentis usam no máximo as últimas 256 amostras.

Footprint físico médio entre minutos 5–10: {mib(soak.get('baselineFootprintBytes',0)) if soakOK else 'PENDING'} MiB;
nos últimos cinco minutos: {mib(soak.get('finalFootprintBytes',0)) if soakOK else 'PENDING'} MiB.
Maior footprint amostrado a cada 10 s: {mib(max((r['footprintBytes'] for r in metrics),default=0))} MiB.
Pico RSS do processo: {mib(soak.get('peakRSSBytes',0)) if soakOK else 'PENDING'} MiB.
Limite de engenharia: 8192 MiB; não é um teto imposto pelo kernel.
Revisões finais recuperadas com aviso, preservando áudio já emitido:
{soak.get('preservedRevisions','PENDING')}. Conflitos sem alinhamento, omissões por ciclo,
duplicação de playback e perdas de filas reprovam esse teste. A qualidade é conferida
por ciclo capturado, reunindo as divisões legítimas de segmentos do VAD.
Duplex de 30 minutos: **{'PASS' if callSoakOK else 'PENDING'}**, duas entradas e duas
saídas de produção, fixture sintética. Pico de footprint: {gib(callSoak.get('peakFootprintBytes',0)) if callSoakOK else 'PENDING'} GiB.
PT→EN primeiro áudio P50/P95: {num(callSoak,'outgoingFirstAudioP50Ms')} / {num(callSoak,'outgoingFirstAudioP95Ms')} ms.
EN→PT-BR primeiro áudio P50/P95: {num(callSoak,'incomingFirstAudioP50Ms')} / {num(callSoak,'incomingFirstAudioP95Ms')} ms.
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
{chr(10).join(mtTable)}

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

App parado, janela real visível: {mib(max((r['physicalFootprintBytes'] for r in idle),default=0))} MiB de footprint
em dez amostras curtas. O diagnóstico físico curto confirmou callbacks simultâneos e
ativação de AEC; não mediu atenuação acústica nem conversa humana completa.
'''
(root/'BENCHMARKS.md').write_text(bench)
report=f'''# Entrega pessoal do LocalVoiceTranslator

Atualizado em {datetime.datetime.now(datetime.timezone.utc).isoformat()}.
Implementado o fluxo nativo local microfone → Whisper → tradução PT-BR ⇄ EN → Piper →
PCM, com escuta contínua, participantes, overlap/separação, revisões e interface.
**Aceitação como tradutor completo: PENDING**, pois faltam os testes físicos e com
rede desligada pelo usuário. Build ou WAV aprovado não substitui esses testes.

## Build

- Debug/XCTest e Release usam xcodebuild ARM64, macOS 14+, SwiftUI/C++20/Objective-C++.
- App pessoal: {'gerado' if app.exists() else 'PENDING'} em `artifacts/LocalVoiceTranslator.app` ({gib(size)} GiB de arquivos).
- DMG: {'gerado' if dmg.exists() else 'PENDING'} em `artifacts/LocalVoiceTranslator.dmg`.
- Assinatura ad-hoc, App Sandbox; sem notarização/distribuição comercial.
- Gates reais verificam hashes/auxiliares, testes atuais e sessão contínua. O relatório
  `artifacts/automated-acceptance.json` vincula resultados ao fingerprint dos fontes.
- O bloqueio antigo de temporários Xcode foi resolvido pela autorização já existente.
- Empacotamento revisado: {packaging.get('revisedPackageScriptResult','PENDING')}.
  O DMG usa makehybrid/convert dentro do sandbox, sem montagem temporária; integridade
  da imagem e assinaturas verificadas. A imagem não foi montada para teste de instalação.
  O diagnóstico histórico da embalagem está em `artifacts/packaging-only-revalidation.json`;
  os gates atuais vinculam os testes ao código da entrega atual.
- Janela principal do Release entregue: {state(releaseUI)}, por inspeção da captura
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
acesso serializado. O teste duplex mediu {gib(callGolden.get('peakFootprintBytes',0))} GiB
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
**{state(opposing)}**, {opposing.get('simultaneousPlaybackBlocks',0)} blocos de 20 ms com
as duas saídas em reprodução. Pico de footprint: {gib(opposing.get('peakFootprintBytes',0))} GiB.
Fonte derivada de teste registrada em `call-opposing-provenance.json`; resultado em
`call-opposing-directions.jsonl`. Não envolve drivers ou aplicativos de chamada.

## Performance

[Benchmarks completos](../BENCHMARKS.md) distinguem ASR isolado, pipeline, RSS,
footprint, qualidade humana/sintética e amostragem. Orçamento de engenharia 8 GiB,
reservas para inferências e liberação de caches/voz/separação sob pressão.
Sessão duplex de 30 minutos: **{'PASS' if callSoakOK else 'PENDING'}**, com modelo compartilhado.
Sessão presencial contínua de 30 minutos: **{'PASS' if soakOK else 'PENDING'}**.
Primeiro áudio P50/P95: {num(soak,'firstAudioP50Ms')} / {num(soak,'firstAudioP95Ms')} ms.
E2E P50/P95: {num(soak,'e2eP50Ms')} / {num(soak,'e2eP95Ms')} ms.
Filas limitadas, estado térmico/CPU/memória e gráficos reais; sem temperatura inventada.

## Privacy

Sem conta, serviço externo, download durante uso ou clonagem. Conversa só em memória;
STOP limpa sessão/filas/UI. Backends sem telemetria e logs de conteúdo no Release.
Dependências e modelos nativos estão no bundle; Python é exclusivo da preparação local.

Auditoria estática: App Sandbox + entrada de áudio, sem entitlement de rede ou APIs
/símbolos diretos de rede identificados. Ensaio anterior do pipeline humano com rede
negada ao processo pelo sandbox (não repetido para esta revisão): **{state(network)}**. A primeira tentativa de sandbox
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
| Timestamps, início audível e duração da fila | 21; `latency-timing-tests.log` |
| Cache de voz, admissão e liberação | 10; `voice-cache-tests.log` |
| Cadência adaptativa e confirmação por áudio novo | 24; `streaming-policy-tests.log` |
| Prioridade e cancelamento do ASR compartilhado | 10; `inference-arbiter-tests.log` |
| Formas de onda sintéticas de regressão de eco | 9; `separated-echo-regression.log` |
| Uma voz, revisões e volume variável | 1 regressão nativa humana; `speaker-revision-tests.log` |
| Modelos inválidos, cancelamento, Start/Stop | 49; `reliability-tests.log` |
| Golden sintético PT/EN, mesmo pipeline | {state(golden)}, {golden.get('playedSegments','?')} unidades faladas |
| Chamada duplex, duas direções locais por WAV | {state(callGolden)}; `call-routing-golden.jsonl` |
| Golden humano FLEURS licenciado | {state(human)}, {human.get('playedSegments','?')} unidades faladas |
| Eco digital com PCM reproduzido | {state(echo)}, {echo.get('selfPlaybackBlocks','?')} blocos de eco suprimidos |
| Sete cenários sintéticos | `scenario-results.json`; inclui 3 vozes/retorno, overlap, ruído/silêncio |
| Chamada duplex contínua de 30 min por WAV | {'PASS' if callSoakOK else 'PENDING'}; `call-soak-30min.jsonl` |
| Sessão contínua real de 30 min por WAV | {'PASS' if soakOK else 'PENDING'}; `soak-30min.jsonl` |

São 256 verificações unitárias/integração básica, preservando as 72 originais, mais
quatro goldens, sete cenários e o soak; medições repetidas de benchmark não inflam essa
contagem. As dependências estáticas não foram todas instrumentadas com sanitizers.
Último ensaio de confiabilidade: {reliability.get('cycles','?')} ciclos,
maior pedido de STOP {num(reliability,'maxStopRequestMs')} ms, maior encerramento de
fundo {num(reliability,'maxBackgroundTeardownMs')} ms. Isto não mede latência acústica.

## Remaining issues

1. Executar e registrar [MANUAL_VALIDATION.md](MANUAL_VALIDATION.md): microfone→voz nas
   duas direções, double-talk/AEC acústico, IDs humanos/retorno/bilinguismo, overlap,
   Start/Stop físico, dispositivos, 30 minutos físicos e rede desligada pelo usuário.
   A aba Áudio e a janela Settings foram abertas e inspecionadas visualmente.
   Captura/reprodução com dispositivos físicos explícitos, modelo pronto, fechamento de
   janela auxiliar e STOP: {state(customAudio)}; `custom-audio-verification.json`.
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
- SHA-256 DMG: `{sha}`
- [Aceitação automatizada](../artifacts/automated-acceptance.json)
- [Medições](../BENCHMARKS.md), [licenças](../MODEL_LICENSES.md),
  [manifesto de modelos](../models/manifest.json), [cobertura das fases/cenários](IMPLEMENTATION_STATUS.md).
- Logs de build, assinatura/auditorias, WAVs de desenvolvimento e JSONLs em `artifacts/`.
  A aplicação não grava esses WAVs/JSONLs durante uso normal.
'''
(root/'docs/FINAL_REPORT.md').write_text(report)
(a/'REPORT.md').write_text(report.replace('../artifacts/','').replace('../BENCHMARKS.md','../BENCHMARKS.md').replace('(MANUAL_VALIDATION.md)','(../docs/MANUAL_VALIDATION.md)').replace('(IMPLEMENTATION_STATUS.md)','(../docs/IMPLEMENTATION_STATUS.md)'))
print('Updated BENCHMARKS.md, docs/FINAL_REPORT.md and artifacts/REPORT.md; pending acceptance remains explicit.')
