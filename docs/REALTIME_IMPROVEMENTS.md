# Diagnóstico do teste com vídeo e melhorias em andamento

27/09/2026. Originais em `/Users/morramidy/Downloads/performace` preservados.
O TXT contém **126 amostras de painel em uma gravação de 132,4 s**; o intervalo
entre primeira e última amostra é 131.719 ms. Não contém WAV, textos reconhecidos
ou a sequência individual de eventos de cada inferência.

## Evidência do teste fornecido

`scripts/analyze-performance-report.py` produz
`artifacts/realtime-improvements/user-performance-analysis.json`. Registra
SHA-256 e tamanho dos três arquivos, configurações/modelos observados, mínimos,
máximos e últimos valores medidos, deltas de contadores dentro de cada sessão
e caminho e identificadores de voz. Não recalcula percentis das amostras do
painel: o mesmo valor de última inferência aparece várias vezes enquanto nenhuma
nova inferência termina. Os P50/P95 já presentes no TXT continuam identificados
como snapshots calculados pelo engine, não novos resultados deste parser.

Configuração observada: modo Presencial, Balanced, Whisper small, backend
reportado Metal (`asrBackend=0`, `metalActive=1`), origem automática. O relatório
não demonstra utilização do Neural Engine nessa sessão. A pressão de memória
foi `warning` (1) em todas as 126 amostras; isso é um estado do sistema, não
prova que este processo excedeu seu orçamento de 8 GiB. O footprint observado
ficou entre 1,21 e 1,87 GB decimais.

| Métrica | Faixa observada no painel | Interpretação e limite |
|---|---|---|
| Captura → worker | 28,89–115,96 ms | Idade do último bloco; não inclui a espera para concluir uma frase |
| Classificação VAD | 0,025–0,098 ms | Classificar fala é barato; diferente da pausa esperada para encerrar um trecho |
| Diarização | 29,09–3.776,28 ms | Há picos relevantes no conjunto de processamento de participantes |
| Overlap | 28,78–1.036,22 ms | Contribui ao caminho de diarização; não somar cegamente os máximos |
| Encoder + identificação do idioma, origem automática | 120,39–150,31 ms | Não é o custo isolado da identificação; inclui o encoder |
| Decodificação ASR, origem automática | 37,97–451,69 ms | Encoder já executado na etapa anterior |
| Tradução | até 518,00 ms | Zeros iniciais não são inferências de custo zero |
| Carregar voz | até 860,31 ms | 26 faltas de cache e nenhuma reutilização no período |
| Síntese TTS | até 700,31 ms | Não inclui o carregamento de voz da linha anterior |
| Espera na fila TTS | até 1.476,14 ms | Trabalho aguardando worker |
| Áudio pendente de reprodução | até 14,36 s | Atraso acumulado confirmado na saída |
| Espera até iniciar reprodução | até 13.807,75 ms | Confirma backlog, mesmo depois de preparar áudio |

O primeiro áudio reportado variou de 5.859 a 17.764 ms desde a captura da fala,
e E2E de 8.351 a 24.746 ms até terminar sua reprodução. Esses valores incluem
duração capturada, confirmação e filas; não são tempo puro de inferência nem
medição acústica. A espera pelo mutex/modelo ASR ficou abaixo de 0,006 ms nas
amostras; já a primeira espera de trabalho ASR foi 8.330 ms. Um atraso de entrada
na fila não deve ser confundido com transferência CPU/GPU. O TXT não permite
atribuir todos os picos a uma única causa ou estimar o ganho de mudar backend.

Com origem manual, `asrLanguageMs` fica ausente e `asrDecodeMs` passa a incluir
encoder + decodificação. Desligar a detecção de idioma não elimina o encoder.
Comparações entre origem manual/automática devem usar o tempo total ASR e
eventos equivalentes, sem interpretar a mudança de coluna como ganho de tempo.

O recorte do app mostra dois participantes: um marcado PT-BR com
`en_US-lessac-medium` e outro EN com `pt_BR-jeff-medium`. O TXT observa também
`pt_BR-faber-medium`. Isso comprova mais de uma identidade/voz registrada,
mas não permite dizer quais falas acústicas originaram esses IDs. O usuário
relatou um único falante no vídeo. A imagem do YouTube mostra o canal
`@NetworkChuck` e o título “i got one....and it's FAST!!!”; não fornece URL
completa nem áudio. Não reconstruímos falas ou referência de transcrição a
partir dessa imagem.

## Causa confirmada: recarregamento de voz sob aviso de memória

O cache anterior executava `clear()` a cada `get(..., pressure=true)`, antes
de verificar se a voz solicitada já estava presente. A rotina ociosa de TTS
também liberava o cache sob aviso de pressão. Com `warning` constante, a mesma
voz voltava a ser carregada em novos trechos. O contador do usuário (26 faltas,
zero hits) é compatível com essa condição. A cópia preservada do código anterior
está em `artifacts/realtime-improvements/VoiceCache-before.hpp`.

A correção mantém a voz solicitada sob aviso, remove vozes inativas excedentes
e reserva a limpeza completa da rotina ociosa para pressão crítica. A admissão
de memória continua obrigatória; manter cache não autoriza ultrapassar o limite.
Essa mudança trata tempo de carregamento, não prova resolução da criação
incorreta de participantes ou estabilidade de voz ao vivo.

A comparação isolada usa a mesma voz Lessac local, seis frases repetidas cinco
vezes, voz aquecida antes de medir e aviso de memória injetado em ambos os
executáveis. Foram **30 observações antes e 30 depois**, com hashes de binários
e fontes em `artifacts/realtime-improvements/voice-comparison.json`.

| Estágio TTS isolado | Antes | Depois |
|---|---|---|
| Cargas de voz durante as 30 observações | 30 | 0 |
| Carregar voz, P50 / P95 | 418,84 / 422,26 ms | <0,001 / <0,001 ms |
| Síntese, P50 / P95 | 34,50 / 74,77 ms | 36,18 / 74,46 ms |
| Etapa total, P50 / P95 | 456,08 / 494,55 ms | 36,20 / 74,50 ms |
| Footprint máximo do processo de teste | 335,56 MB | 204,08 MB |

O ganho medido está no cache, não em acelerar o cálculo da síntese ou no ANE.
O pico de memória é do processo de teste TTS, não do aplicativo completo.
Esta comparação não mede E2E, captura física, streaming ou chamada; não deve
ser apresentada como redução de 420 ms garantida para cada fala do usuário.

## Outras causas confirmadas no código e nas regressões

**Evidência de ausência de fala do Whisper:** a leitura anterior de
`no_speech` não usava corretamente a linha do token inicial SOT quando o
decoder só disponibilizava os logits solicitados. A correção solicita essa
linha e calcula a probabilidade sobre seus logits não filtrados, incluindo o
deslocamento de contexto anterior. O valor uniforme anteriormente observado
não era evidência confiável de presença de fala. Também separam-se silêncio
digital, evidência de ausência de fala e confiança dos tokens de texto; tokens
especiais não devem mascarar a confiança lexical. Isso corrige o indicador,
mas não prova que todo ruído ou música jamais produzirá hipótese indevida.

**Contexto de transcrição:** o contexto anterior podia sobreviver ao fim de
uma fala e influenciar outra janela. `ASRContext` agora só transporta texto
entre janelas artificialmente cortadas, adjacentes, do mesmo participante e
idioma, sem overlap. Silêncio, identidade desconhecida, fala vazia ou outra
quebra invalidam a continuação; o histórico é limitado a 512 bytes. A correção
não usa uma lista proibida de “thanks”/“obrigado”.

**Identidade provisória:** hipóteses parciais podiam cadastrar novas identidades
mesmo quando não deveriam atualizar os centroides. O tracker agora impede
cadastro em prévias e overlap, exige evidência final independente e não conta
PCM repetido/sobreposto como outra confirmação. O primeiro participante só é
cadastrado imediatamente quando duas partes distintas da fala final corroboram
a identidade. O caminho de cadastro verifica correspondências conhecidas antes
de criar um novo ID, evitando criar outro nome para uma identidade existente.

**Vínculo com a voz:** `SessionVoices` fixa o par PT/EN por ID na sessão. Um
trecho com identidade incerta conserva o último par, sem resetar para outra
voz; trocar idioma escolhe o outro idioma do mesmo par. Isso estabiliza a voz
percebida durante incerteza, mas não é prova de identificação correta. Um
participante novo ainda não confirmado pode temporariamente compartilhar a
voz anterior. Os testes controlados abaixo passam; a causa exata dos dois IDs
do vídeo original continua sem confirmação por falta do áudio capturado.

## Como usar os controles implementados

Abra **Idiomas e vozes** na barra lateral (ou **Ajustes, ⌘,**), com a sessão parada. Configure VOCÊ para o microfone local e
PARTICIPANTES para o áudio recebido da chamada; depois volte à janela principal
e inicie a escuta. O modo Presencial usa VOCÊ para todas as vozes do microfone.
As escolhas são restauradas após reiniciar o app.

As preferências Swift já oferecem dois grupos independentes, VOCÊ e
PARTICIPANTES. Origem começa automática em ambos; no modo Presencial as opções
VOCÊ se aplicam ao microfone compartilhado. Os seletores de idioma agora
invertem o par manual junto, evitando rejeitar a escolha durante a inversão;
a API continua rejeitando configurações inválidas. Preferências são persistidas e ficam bloqueadas durante a
sessão; ambos os engines recebem snapshots próprios no START.

Os quatro campos são **Eu falo em / Eles ouvem em** e **Eles falam em / Eu ouço em**.
O antigo seletor “Caminho” do Engine foi renomeado para “Estatísticas de” e ganhou
um botão para abrir a configuração. Veja [correção dos seletores](LANGUAGE_SETTINGS.md)
para a validação posterior da interface e da persistência; os benchmarks deste
relatório não foram repetidos por uma alteração de navegação/edição.

- Identificação de falantes desligada permite escolher um dos três pares de
  vozes; não significa desligar a transcrição.
- Idioma manual configura a origem e dispensa a chamada automática de idioma
  no Whisper integrado. O contador `languageDetections` fica separado do ASR;
  o encoder continua necessário, conforme a distinção de métricas acima.
- Saída somente em texto dispensa TTS e reprodução.
- Espera após pausa: 200–1.000 ms, padrão 300 ms.
- Limite de fila falada: 3–15 s, padrão 6 s. A política omite novos áudios por
  inteiro quando não há orçamento, mantém o texto e não corta a reprodução
  atual. Isso troca completude da voz por menor acúmulo e precisa aparecer
  como estado visível, não descarte oculto.
- O relatório TXT inclui direção configurada, contadores de detecção/rejeição,
  voz manual, saída em texto, limite da fila e atraso por unidade. O painel
  distingue primeiro áudio, espera de saída e atraso por trecho.

## Glossário editável

O recurso inclui **1.477 termos canônicos únicos**, distribuídos em 11 categorias:
843 em “sempre preservar”, 259 contextuais e 375 para tradução normal. Os
778 candidatos únicos do pedido estão representados como termo ou variante;
aliases duplicados foram reunidos e 17 entradas pertencem a mais de uma
categoria. Inventário e hash: `artifacts/realtime-improvements/glossary-inventory.json`.

Em **Ajustes → Editar glossário…**, pesquise termo, variante ou categoria,
adicione/edite/remova termos, escolha modo e direção, habilite regras/categorias
e importe/exporte JSON. “Gerenciar categorias” permite criar, renomear ou
remover categorias. Se remover uma categoria deixaria termos sem vínculo, é
necessário escolher outra; os termos não são apagados. Importação valida o
documento antes de substituir as preferências. Alterações valem na próxima
sessão.

As regras iniciais atuam em EN → PT-BR; PT-BR → EN precisa ser marcado na regra.
`unboxing`, `feedback` e `spoiler` são preservados; `coach`, `call`, `Spring` e
outros homônimos usam contexto e exclusões. Palavras comuns como `live`, `push`,
`cloud` e `cover` deixaram de ser preservadas indiscriminadamente. O matcher
considera Unicode, limites de palavra, plurais/variantes explícitos e expressões
compostas, selecionando apenas regras relevantes para cada sentença. Uma
categoria ativa basta para um termo associado a várias.

Não foi implementado reconhecimento geral de entidades nomeadas (NER) fora
dessas regras: nomes e produtos não cadastrados não têm garantia especial de
preservação. O escopo e os limites estão detalhados em [GLOSSARY.md](GLOSSARY.md).

A tradução continua sobre a sentença completa. A reposição usa alinhamento do
tradutor e recusa spans ambíguos, conflitantes ou que apagariam evidência
sensível, como negação. Essa proteção não é garantia universal de sentido.
**Limitação reproduzida:** “We took a coach to the station.” virou “Levamos um
treinador para a estação.” no controle e no candidato. O glossário corretamente
não força a palavra inglesa naquele contexto, mas o modelo de tradução ainda
erra o sentido de ônibus. Esse caso permanece pendente.

Quatro traduções com termos preservados atravessaram TTS e PCM usando o mesmo
participante e `pt_BR-faber-medium`, sem troca de voz. Os WAVs estão nos
artefatos de desenvolvimento; pronúncia e inteligibilidade exigem audição
humana e continuam pendentes.

## Validação executada e limites

| Grupo | Resultado atual | Escopo |
|---|---|---|
| Aplicativo | 60 verificações C++ + 37 XCTest passaram | Build/contratos, preferências, CRUD/importação e sessão |
| Controles ASR / opções | 56 / 57 passaram | Regras e combinações de processamento |
| Temporização/cache/streaming/árbitro/boundary/ledger | 21 / 10 / 24 / 18 / 33 / 13 passaram | Verificações locais; não somar como sessões humanas |
| Conversa / drenagem | 48 / 18 passaram | Fluxo, estados terminais, ordenação e confirmação do coordenador |
| Glossário | 37 verificações + 19 traduções nativas passaram | Inclui plurais/contextos mistos; limitação semântica do ônibus explicitada |
| Glossário → voz | 4/4 passaram | Mesma voz/PCM/STOP; não valida pronúncia |
| Participantes | Uma gravação humana repetida e duas vozes sintéticas alternadas passaram | Estabilidade nas fixtures, não no vídeo/call real |
| Proteções no pipeline | 8/8 passaram | Silêncio, ruído, tom instrumental e eco digital, em origem automática/manual |
| Estabilidade por cinco minutos | PASS; 46/46 traduções reproduzidas | Uma gravação humana repetida, origem EN manual, sem identificação de participantes; não equivale ao teste de 30 minutos |
| Fala com ruído, regressão sintética | PASS funcional; 2/2 reproduzidas | Conflito anterior não reproduzido nesta execução; sentido da frase PT continua incorreto |
| Controles ASR nativos | **37/38; FAIL** | A palavra curta sintética “Sim.” não foi reconhecida corretamente |
| Probe ASR sem fala/curtas | **26/27; FAIL** | Mesmo caso “Sim.”; “Não”, “Ok”, “Thanks”/“Obrigado” têm evidência própria |

Não foi introduzido um corte que rejeite todas as falas curtas para esconder
esse resultado. Experimentos de padding/corte foram registrados separadamente
e não transformam erro lexical em aprovação. Logs centrais:
`app-tests-final.log`, `latency-tests.log`, `glossary-validation.json`,
`glossary-voice-validation.json`, `asr-controls-native-lexical.jsonl` e
`asr-no-speech-lexical.jsonl`, sob `artifacts/realtime-improvements/`.

### Proteções de ponta a ponta e comparação humana pareada

A execução atual de `scenario-speech-noise.jsonl`, configurada por
`build/realtime-speech-noise-current.json`, terminou com duas traduções e duas
reproduções, sem perdas, conflitos, continuações recuperadas ou revisões
preservadas. O conflito anterior de fala já reproduzida não reapareceu nessa
execução sintética. **A qualidade semântica não passou:** a fala PT sobre
começar a reunião foi reconhecida como “Vamos começar a reunir” e virou
“Let's start gathering”. A execução não aprova fala com ruído em geral nem
remove as condições de qualidade para entrega completa.

O lote de oito cenários de proteção terminou com sucesso, conforme
`audio-protection-results.json`: silêncio, ruído sem fala, tom instrumental e
eco digital, cada um em origem automática e manual. São fixtures controladas;
não representam toda música, TV ou recaptura acústica. A comparação humana
terminou com os mesmos cinco arquivos licenciados de desenvolvimento por
idioma antes/depois. Os **10/10 pares são elegíveis para comparação temporal**:
hashes dos WAVs, referências, idiomas e durações coincidem; nenhum foi excluído.
Houve cinco unidades reproduzidas em EN e seis em PT em cada configuração,
sem omissão, pendências ou erro funcional reportado. Uma gravação PT gerou
duas unidades nas duas configurações. Isso verifica cobertura das unidades
produzidas; não prova fidelidade de todas as palavras da gravação.

`scripts/compare-realtime-corpus.py` calcula um primeiro áudio e um fim de
reprodução por gravação, separa medidas após fim estimado pelo VAD, deduplica
eventos por sessão/segmento e verifica cobertura. Não considera omissão de
áudio como ganho de latência. Com cinco gravações por idioma não publica
P50/P95. O comparador passou cinco testes. Resultado completo, hashes dos logs,
binários/configurações e texto por gravação estão em `corpus-comparison.json`,
com a proveniência de `corpus-before-runs.json` e `corpus-after-runs.json`.
Os JSONL brutos foram preservados.

| Medida no replay nativo | EN antes → depois | PT antes → depois |
|---|---|---|
| Primeiro áudio desde início do WAV, média de 5 gravações | 7.073,67 → 6.911,14 ms | 8.409,94 → 8.053,92 ms |
| Primeiro áudio, mínimo–máximo | 5.536,85–9.021,31 → 5.375,32–8.604,12 ms | 6.904,86–9.522,43 → 6.387,53–9.480,48 ms |
| Fim da última reprodução desde início do WAV, média | 11.859,83 → 11.638,62 ms | 13.310,00 → 12.752,62 ms |
| Fim da última reprodução após fim da captura, média | 5.443,83 → 5.222,62 ms | 5.054,00 → 4.496,62 ms |
| Primeiro áudio após fim estimado pelo VAD, média por unidade | 1.292,51 → 1.132,40 ms (5 unidades) | 1.732,48 → 1.375,66 ms (6 unidades) |
| Footprint máximo do processo no lote | 1.703,12 → 1.483,52 MB | 1.693,47 → 1.399,95 MB |

A diferença pareada média do primeiro áudio foi **−259,27 ms**, variando de
−517,33 a **+117,41 ms**, nas dez gravações; a do fim da reprodução foi
−389,29 ms, variando de −1.467,65 a **+435,15 ms**. O sinal positivo representa
piora. A gravação PT `12857830854717968324` piorou nas duas medidas. Os tempos
desde início do WAV incluem a fala capturada e sua confirmação; não são tempo
puro de cálculo. O fim da reprodução também depende do texto e da duração do
áudio gerado. Quatro traduções mudaram entre configurações, portanto não se
deve atribuir toda redução ao processamento mais rápido. O footprint menor
neste lote não demonstra estabilidade de memória prolongada.

**Revisão textual:** o resultado funcional PASS não é aprovação de sentido.
Em PT `494922209427947514`, “alcalino” passou a “calino” no reconhecimento e
“alkaline” a “caline” na tradução: regressão lexical observada, sobre uma frase
que já tinha erro estrutural antes. Em PT `18401650931916642390`, “ferver”
continua reconhecido como “viver”, produzindo “living water”. Em PT
`10556936069191551163`, “iate” continua como “hiate”, traduzido para “hiatus”.
Em EN `10492135991603561867`, a saída posterior perdeu “vários” em “outros
vários minutos”, em uma construção que já precisava de revisão. Os demais
casos não justificam afirmar qualidade geral por este lote pequeno. A
aceitação semântica continua pendente no JSON, inclusive para pares sem
mudança de texto.

Essa medição não atinge nem demonstra uma conversa traduzida com atraso de
45–75 ms: após o fim estimado pelo VAD ainda há médias superiores a um segundo.
São dez arquivos reproduzidos no pipeline nativo, sem captura acústica ou
chamada real. A tabela de cache acima permanece uma comparação isolada, com
30 observações por configuração, distinta deste replay humano.

O build de diagnóstico de
[`artifacts/LocalVoiceTranslator-Diagnostic.app`](../artifacts/LocalVoiceTranslator-Diagnostic.app)
terminou com sucesso, assinatura local verificada e inventário de 13
modelos/2.187 arquivos únicos, incluindo a auditoria estática offline. Esse é
o aplicativo destinado a testar estas mudanças; `LocalVoiceTranslator.app`
anterior foi preservado. O diagnóstico foi recompilado e verificado novamente
após a correção das pistas/plurais, com o seed final de SHA-256
`33166d43a7e5143112280a9fba01fcde92de44dc9b5b379295e0fd6f954ab8fc`.
O log final é `artifacts/realtime-improvements/release-diagnostic-final.log`.
Isso não substitui abrir o
aplicativo com a rede desligada pelo usuário, testes físicos ou aceitação de
qualidade. Não houve novo modelo/garantia de uso do ANE; o teste fornecido usa
Metal.

O gate normal foi reexecutado após o build e **continua reprovado**:
`FAIL native processing regression: asr-controls-native`, registrado em
`artifacts/realtime-improvements/release-gates-final.log`. A falha de “Sim” não
foi retirada dos testes nem convertida em aprovação. O bundle de diagnóstico
permite experimentar as mudanças; não é uma entrega aprovada como tradutor
completo. `artifacts/realtime-improvements/delivery-validation.json` registra
o pacote final, hashes das evidências e limitações.

### Estabilidade por cinco minutos: replay de uma gravação humana

`soak-300-summary.json` registra o resultado **PASS**, o SHA-256 do log
`soak-300.jsonl`, a configuração e o snapshot de glossário. Foram 300 segundos
de alimentação em tempo real mais drenagem, totalizando **310,78 s**, com
46 segmentos traduzidos e 46 reproduzidos. Não houve perda de captura, VAD,
eventos ou playback, supressão de áudio ou conflito de revisões. A voz Faber
teve 46 hits e nenhuma falta de cache após aquecimento. Os 42 trabalhos
obsoletos foram cancelados/substituídos sem pendências ao final; não representam
42 falas finais perdidas.

O cenário repetiu **o mesmo WAV humano licenciado**
`en_us-10492135991603561867.wav`, em EN → PT manual e com identificação de
participantes desligada. Não exercitou microfone, eco acústico, alternância
de participantes, overlap ou separação. O snapshot usou a versão anterior à
última correção de pistas/plurais de `coach`; essa regra não se aplica à frase
do WAV. A aprovação mede estabilidade operacional, não corrige a limitação
semântica dessa frase já descrita na comparação humana.

| Medida no cenário repetido | Resultado |
|---|---|
| Primeiro áudio após fim estimado pelo VAD, 46 observações | P50 1.100,54 ms; P95 1.168,14 ms |
| Média pós-VAD nas primeiras / últimas dez falas | 1.110,55 / 1.102,66 ms |
| Primeiro áudio desde captura da fala | P50 5.402,47 ms; P95 5.478,05 ms |
| Final da reprodução desde captura da fala | P50 11.074,13 ms; P95 11.429,22 ms |
| Footprint máximo nas 32 amostras | 1.541,49 MB decimais |
| Footprint médio entre 60–120 s / 240–300 s | 1.466,35 / 1.470,65 MB |
| Diferença entre essas janelas de memória | +4,30 MB, aproximadamente +0,3% |
| Máximo amostrado das filas | Captura 1; ASR, tradução, TTS e playback 0 |

Não se observou acúmulo progressivo de atraso nesse replay. Os percentis
descrevem 46 repetições de uma única gravação, não 46 falantes ou frases
independentes. As filas foram amostradas aproximadamente a cada dez segundos;
zero nessas amostras não significa pico contínuo zero. A pequena diferença
de memória entre duas janelas não prova ausência de vazamento, e os campos
legados de footprint inicial/final iguais a zero não foram usados como
medição. O teste de 30 minutos e a validação física permanecem pendentes.

## Arquivos principais

- `engine/tts/VoiceCache.hpp`, `engine/tts/SessionVoices.hpp`: reutilização e vínculo de voz.
- `engine/asr/ASRContext.hpp`, `WhisperASR.cpp`, `ASRWorker.cpp` e patch local de
  `third_party/whisper.cpp/src/whisper.cpp`: contexto, controles e evidência SOT.
- `engine/speakers/SpeakerTracker.hpp`, `OnlineDiarizer.cpp`: cadastro final,
  evidência independente e identidade incerta.
- `engine/conversation/ConversationPipeline.cpp` e opções de processamento:
  texto sem voz, limite de backlog e estados visíveis.
- `engine/translation/Glossary.hpp`, `TranslationEngine.cpp`: seleção de regras
  e reposição alinhada sobre tradução completa.
- `app/DirectionSettings.swift`, `AppState.swift`, `Views/SettingsView.swift`:
  preferências independentes e snapshot por sessão.
- `app/GlossaryStore.swift`, `Views/GlossaryView.swift`,
  `Resources/glossary-defaults.json`: glossário persistente e editável.
- `app/PerformanceRecording.swift`, `Views/EngineView.swift` e scripts de
  análise: métricas com escopo explícito, exportação e comparações.

## Hipóteses e verificações pendentes

- **Voz duplicada:** o print e os contadores confirmam duas identidades, mas
  não distinguem erro do embedding, reatribuição de segmento incerto, áudio
  recapturado ou uma segunda fonte acústica. Falta gravação com verdade de
  referência e validação física de um falante variando entonação, além de dois
  falantes alternados.
- **“Thanks”/“obrigado” sem fala:** o TXT não contém a cadeia de eventos nem
  áudio. Não confirma se o erro surgiu no ASR, no contexto, na tradução ou
  por recaptura. As proteções devem manter agradecimentos reais e falas curtas
  legítimas; bloquear essas palavras não é solução.
- **Backlog:** o atraso original está medido, e o replay de cinco minutos
  terminou sem acúmulo observado ou omissões. O teste determinístico de
  congestionamento preservou seis traduções, reproduziu uma fala inteira e
  sinalizou cinco novos áudios omitidos. Ainda faltam 30 minutos, fontes
  variadas, sobrecarga real e inspeção da indicação na interface, além de
  validação física de ordem e STOP/cancelamento.
- **Glossário:** os testes de persistência, CRUD, seleção e preservação passaram
  nos casos citados, mas diálogos manuais de importação/exportação, pronúncia
  dos termos e revisão semântica ampla continuam pendentes. O erro de `coach`
  como ônibus não foi resolvido. Um vocabulário extenso não prova qualidade.
- **Latência total:** a comparação nativa pareada está acima; falta repetir
  captura/reprodução física e comparar a configuração completa da interface,
  com glossário ativo e duas direções simultâneas. O corpus pareado usa o mesmo
  pipeline de produção, mas não configura glossário em nenhum dos dois lados.
  O TXT original permanece diagnóstico observacional; não é um baseline
  experimental E2E controlado.
- **Aplicativo completo:** chamada real, áudio físico, offline, cenário original
  do vídeo e sessão prolongada continuam separados da compilação e dos smokes.

O parser passou cinco testes pequenos: amostras repetidas sem percentis novos,
separação de sessões/caminhos, reset de contador, dados ausentes/não finitos,
exclusão de conteúdo, hash e rejeição de métrica duplicada. Log em
`artifacts/realtime-improvements/performance-parser-tests.log`.
