# Revisão de áudio, TTS e participantes

Atualizado: 2026-09-12T00:50:15.234325+00:00.

## Conexão sem instalar driver

O app prepara dois agregados/taps nativos Core Audio, publicados como
**LocalVoiceTranslator Microphone** e **LocalVoiceTranslator Speakers**. Na aba Áudio,
escolha Chamada, selecione seu microfone/fones, confirme fones e use **Preparar áudio
da chamada**. Depois selecione esses dois dispositivos no aplicativo da chamada e
inicie a escuta no tradutor. Não foi instalado driver nem alterado dispositivo padrão.

- Teste do controlador de produção em App Sandbox, com um cliente em outro processo:
  call-client: PASS, translator: PASS.
- Remoção com cliente ainda conectado: PASS.
- Interface real: PASS; os dois campos mostram dispositivos Core Audio reais.
  O teste fechou a janela antes de encerrar o app e não deixou dispositivos nativos.
  Foi apenas preparação de rotas: microfone/conversa não foram iniciados.
- Uma segunda instância não assume nem remove dispositivos de outra instância.
- macOS 14.2+; saída estéreo separada do microfone. Bluetooth com entrada/saída no
  mesmo UID não está suportado. O macOS pode pedir permissão de captura de áudio.
- STOP silencia/encerra o pipeline e mantém endpoints até desativação ou saída normal.
  Encerramento forçado pode deixar agregados públicos no estado gerenciado pelo macOS;
  a UI informa remoção em Áudio MIDI. Registros de permissão do sistema podem permanecer.
- Meet, Zoom, Teams, Discord e Slack ainda requerem validação individual numa chamada
  real. O teste de dois processos demonstra transporte de PCM, não essas integrações.

O instalador externo preparado anteriormente é uma alternativa de desenvolvimento
guardada no projeto e **não foi instalado**. O caminho nativo não depende dele.

## Uma voz criando dois participantes

Reprodução com duas fixtures sintéticas e 12 gravações humanas FLEURS licenciadas,
prefixos de 1,5 s e repetições com ganho constante/reduzido/gradual: 231
observações antes e 231 depois. Uma gravação humana criou dois IDs e duas
fontes em um prefixo de seis segundos com variação de volume. Os arquivos preservados
mostram o falso overlap desaparecer na hipótese final da mesma gravação.

Correções: preservar o estado parcial/final até a diarização; não separar nem cadastrar
fontes de overlap provisório; exigir concordância de embeddings de trechos distintos
antes de criar uma identidade limpa; atualizar centroides apenas em resultados finais;
descartar trabalho obsoleto antes da diarização; zerar métricas das etapas não executadas.
Não foram usados volume, pitch ou gênero para atribuir identidade, nem merge de IDs.

Resultado: máximo de 2 IDs antes e
1 depois por gravação. Nenhuma das 14 fixtures criou
duplicatas no reteste. Duas permaneceram sem identificação suficiente. Isso não prova
robustez para toda entonação/sotaque ou substitui testar a voz relatada pelo usuário.
O teste nativo de regressão exige exatamente um ID na gravação que falhava, evitando
aprovar uma solução que simplesmente desligue a identificação.

## TTS e demora percebida

54 sínteses reais, seis vozes e três comprimentos de texto, executadas enquanto
outros testes de desenvolvimento também rodavam. Inferência TTS:
**24.3–239.8 ms**.
Carga inicial de voz: **468.5–710.4 ms**.
Não se reproduziu uma inferência TTS de 5 segundos. Não há percentis por voz: são
apenas nove amostras por voz com textos diferentes. Os números 46→51/70 relatados
pelo usuário precisam ser interpretados junto da unidade/campo exibido.

O painel distingue inferência, duração de áudio gerado, carga de voz, idade das filas,
primeiro áudio e E2E. Primeiro áudio inclui aguardar fala/contexto; E2E inclui reproduzir
a voz. Essas medidas não são diretamente comparáveis a uma inferência de 40–70 ms.

Uma tentativa de atualizar parciais a cada 750 ms reduziu uma medição curta a 5,1 s,
mas falhou em fala com ruído por revisão posterior do texto. Foi rejeitada. O código
entregue mantém o intervalo de 1,5 s e três acordos de prefixo. A evidência da tentativa
fica em `short-cadence-trial/`. Não se promete aquele ganho na versão entregue.

## Validação

- XCTest, C++/sanitizers, regressão de uma voz, ASR, tradução/vozes, cancelamento,
  eco digital e cenários constam em `artifacts/automated-acceptance.json`.
- Sessão presencial de 30 minutos: **PASS**.
- Sessão duplex de 30 minutos: **PASS**.
- Os soaks testam o pipeline nativo com WAV em tempo real; o transporte por taps é
  verificado separadamente pelo teste com dois processos. Nenhum equivale a 30 minutos
  de reunião real, conversa acústica, mudança de dispositivo ou teste offline pelo usuário.
- Uma rodada foi invalidada por pausa de agendamento do host; seus logs foram
  preservados e não contados como aprovação. Não foram relaxados os gates de precisão.

Evidências nesta pasta: `before.jsonl`, `after.jsonl`, `tts-latency.jsonl`,
`native-duplex.jsonl`, `native-duplex-provenance.json`, `native-ui.json`, `native-ui.png`.
Os WAVs/relatórios são exclusivamente de desenvolvimento; o app não grava a conversa.

Referências: [Core Audio taps e agregados, Apple](https://developer.apple.com/documentation/coreaudio/capturing-system-audio-with-core-audio-taps),
[guia de conexão](CALL_AUDIO_SETUP.md).
