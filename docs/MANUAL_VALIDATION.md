# Validação física e offline pendente

Use a aplicação `artifacts/LocalVoiceTranslator.app` da entrega, assinada ad-hoc.
Registre data, versão, dispositivos de entrada/saída e o SHA-256 do binário usado.
Não é necessário nem automático salvar áudio ou texto pessoal: anote apenas o
resultado e uma descrição técnica de qualquer falha.

## Preparação

1. Abra o app normalmente e autorize o microfone no diálogo do macOS. Escolha os
   dispositivos pelas configurações habituais do sistema, sem instalar drivers.
2. Consulte Engine: ASR, ECAPA/Pyannote e tradução devem estar prontos; a voz é carregada
   na primeira tradução. Verifique callbacks/nível de entrada e filas.
3. Para o teste offline, desligue a rede você mesmo depois que o aplicativo já estiver
   preparado. O agente não altera Wi-Fi/Ethernet. Os modelos estão incluídos no bundle.

## Conversa e reprodução

- Diga em PT-BR: “Eu não autorizei o pagamento. O total é cento e vinte reais, não
  duzentos.” Confira original, tradução inglesa, negação, números e voz nos alto-falantes.
- Diga em inglês uma frase com negação e números; confira a tradução e voz PT-BR.
  Experimente nomes próprios, sabendo que podem ser traduzidos incorretamente.
- Duas pessoas alternam; depois uma terceira participa. Confirme IDs distintos.
  Uma pessoa volta após alguns minutos; verifique se o ID permanece. A mesma pessoa
  alterna PT/EN; o par de vozes deve permanecer associado ao ID.
- Enquanto o TTS fala, outra pessoa continua falando. A captura deve avançar e a nova
  fala deve ser reconhecida. Depois todos ficam em silêncio durante/apos o TTS:
  o playback isolado não pode criar outra tradução. Em caso de recursão, pressione STOP.
- Duas pessoas falam ao mesmo tempo, com textos previamente conhecidos. Verifique as
  duas transcrições ou a indicação explícita de separação não confiável. Três fontes
  simultâneas excedem o contrato do separador.
- Repita com sotaques, ruído doméstico e música/TV. Não há classificador de intenção:
  fala inteligível na TV pode ser tratada como outra fala do ambiente.

## Estabilidade e dispositivos

- Mantenha conversa real por 30 minutos. Registre footprint/pico e filas; não devem
  crescer progressivamente nem ultrapassar o limite de engenharia de 8 GiB.
- Faça Start/Stop durante carregamento, reconhecimento e reprodução. STOP deve
  silenciar e limpar imediatamente, sem texto antigo reaparecer na sessão seguinte.
- Troque/desconecte o dispositivo. Mudança incompatível deve parar com erro visível;
  após restaurar a rota, uma nova sessão deve funcionar. Não basta um teste WAV para isso.
- Encerre/reabra o app: a conversa anterior não deve reaparecer.
- Complete as duas direções ainda com a rede desligada. Registre o resultado antes de
  restaurar sua conexão normal.

## Registro

| Gate | Resultado atual | Evidência a registrar |
|---|---|---|
| Microfone → tradução → alto-falante PT→EN | PENDING | Frase, sentido e reprodução |
| Microfone → tradução → alto-falante EN→PT | PENDING | Português brasileiro e reprodução |
| Captura de outra pessoa durante TTS | PENDING | Nova transcrição enquanto saída avança |
| Ausência de recursão acústica | PENDING | TTS isolado sem nova tradução |
| Identidade humana / retorno / alternância de idioma | PENDING | IDs e pares de vozes |
| Overlap humano no ambiente | PENDING | Duas fontes ou rejeição explícita |
| Troca/desconexão física de dispositivos | PENDING | Recuperação ou erro adequado |
| Start/Stop e 30 minutos físicos | PENDING | UI limpa, filas e memória |
| Conversa com rede desligada pelo usuário | PENDING | Duas direções e todas as vozes necessárias |

A auditoria estática, os testes WAV, a janela visível e um build bem-sucedido não
preenchem esses campos. A entrega só deve ser chamada de tradutor completo após a
aceitação funcional e esses testes físicos/offline.

## Chamada com dois caminhos — PENDING

Use `docs/CALL_AUDIO_SETUP.md`. Prepare os dois dispositivos nativos dentro do app
e combine o teste com uma pessoa no outro lado. Não há instalação de driver neste modo.
Registre separadamente Meet (com nome
/ versão do navegador), Zoom, Teams, Discord e Slack. Em cada um, confirme:

- Seu PT-BR chega em EN; o EN remoto chega em PT-BR, sem som no destino errado.
- O microfone da chamada recebe PCM, incluindo quando outro participante fala.
- A saída estéreo 1/2 da chamada chega ao tradutor, incluindo uma voz só no canal 2.
- Monitoramento do próprio microfone desativado; reprodução isolada não se traduz novamente.
- Fones físicos, nenhuma mudança global de dispositivo e nenhuma saída para o alto-falante inesperada.
- STOP durante reconhecimento, síntese e playback silencia ambos e elimina resultados atrasados.
- Desconexão / mudança de formato em qualquer um dos quatro dispositivos encerra ambos os caminhos.
- Troca de aplicativo, mute / unmute da chamada e reabertura após preparação dos dispositivos.
- Sessão contínua de 30 minutos em chamada, com memória, filas e latências das duas direções.

O teste duplex com WAV é independente desses itens. A validação offline sem rede
se aplica ao processamento local; uma reunião remota requer a rede do serviço.

Resultados adicionais já medidos nesta implementação de roteamento:

- `artifacts/audio-routing-ui-verification.json`: aba Áudio real inspecionada, PASS.
- `artifacts/settings-ui-verification.json`: janela Settings real inspecionada, PASS;
  não é automação de clique do menu nem substitui testes de todas as opções.
- `artifacts/custom-audio-verification.json`: webcam/fones explícitos, captura
  durante tom, Whisper/Metal pronto, fechar janela auxiliar e STOP, PASS. A primeira
  tentativa parou antes de o modelo ficar pronto; causa não estabelecida e log preservado.
- `artifacts/no-network-golden.jsonl`: fixture humana no pipeline nativo com rede
  negada por processo, PASS. Nenhuma configuração global de rede foi alterada.
- A aceitação dos dois ensaios de 30 minutos é vinculada aos fontes em
  `artifacts/automated-acceptance.json`; não atribua PASS de outra revisão à atual.

- Revisão 0.2.1: `artifacts/call-setup-review/ui.json` verifica cliques no botão real,
  mensagem de erro visível, preenchimento dos dois caminhos e remoção normal.
  Seleções físicas são fixtures de configuração, sem captura nem chamada.
