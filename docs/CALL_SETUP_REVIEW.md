# Preparação de chamadas — versão 0.2.1

O usuário relatou que, mesmo após clicar em preparar, os seletores continuavam vazios
sem mensagem visível. A causa exata daquele clique não foi registrada. O fluxo anterior
mostrava seletores virtuais vazios antes de criar os dispositivos, com o botão e as
mensagens depois de vários blocos de instruções.

## Correções

- Microfone e fones físicos aparecem primeiro. Não é necessário selecionar dispositivos
  virtuais antes da preparação; eles serão criados e preenchidos pelo app.
- Botão de preparação, progresso e mensagem de erro ficam fora da área rolável.
  O formulário respeita a altura da janela, inclusive quando surge um erro.
- Erros distinguem microfone ausente, fones ausentes, confirmação ausente, dispositivo
  combinado/incompatível e falha no Core Audio.
- Após criar os dispositivos, o worker verifica por até um segundo a mesma enumeração
  usada pelos seletores. Só declara sucesso com os dois UIDs nativos e os canais esperados.
  Em falha, remove os objetos criados e publica uma mensagem; não instala nada.
- Sobre e Info.plist identificam a entrega como 0.2.1, build 2. O texto do DMG agora
  descreve a preparação nativa e não exige uma dependência externa de áudio.

## Evidência

`artifacts/call-setup-review/ui.json` registra o teste na janela SwiftUI real, em tamanho
mínimo. O diagnóstico envia eventos de mouse aos controles do próprio app, sem permissão
global de Acessibilidade ou automação de outros aplicativos:

1. Começa sem dispositivos virtuais e sem seleções físicas.
2. Pressiona o botão real e verifica a mensagem de microfone ausente, inclusive seus
   limites na janela e captura visual (`missing-microphone.png`).
3. Preenche seleções físicas como fixture de configuração e aciona o controle de fones.
4. Pressiona o mesmo botão e verifica ambos os dispositivos reais e os seletores
   preenchidos (`prepared.png`).
5. Fecha a janela e remove os dispositivos; confirma ausência de ambos no Core Audio.

A revisão visual passou. A fixture usa uma saída estéreo disponível; não comprova que
alguém esteja usando fones. Nenhuma captura de microfone, reprodução, reunião ou chamada
foi iniciada por esse diagnóstico. Os dois novos XCTest cobrem pré-requisitos sem
virtuais preexistentes e saídas combinadas/incompatíveis; os testes de descoberta cobrem
publicação incompleta. O resultado da suíte completa está em `validation.log` e vinculado
aos fontes por `artifacts/automated-acceptance.json` após aprovação.

O teste anterior de PCM entre dois processos continua aplicável ao controlador nativo,
que não foi alterado nesta revisão. As sessões anteriores de 30 minutos cobrem o engine
sem alterações; não foram repetidas por esta correção de interface/preparação.

## Instalação e remoção

Não foi incluída instalação automática de driver: o modo nativo passou pela preparação
real com os dispositivos disponíveis. Nenhum pacote foi aberto e nenhum arquivo foi
instalado em `/Library/Audio/Plug-Ins/HAL`.

Um driver HAL instalado teria vida independente do `.app`. Remover o `.app` pela Lixeira
não executa seu código de desinstalação; seria necessário um desinstalador específico,
com autorização do macOS. Não se promete remoção automática nessa alternativa.
Orientação geral: https://support.apple.com/pt-br/102610.

No modo entregue, os dispositivos são removidos ao desativar o áudio nativo ou sair
normalmente. Restaure os dispositivos no aplicativo da chamada antes de sair do tradutor.
Encerramento forçado pode deixar agregados no estado do macOS; instruções em
`CALL_AUDIO_SETUP.md`. Registros de permissões do macOS podem permanecer.

Ainda pendentes: uso físico com os dispositivos do usuário e teste em cada aplicativo
de chamada, incluindo seleção de saída no navegador. Bluetooth com entrada/saída no
mesmo UID não é suportado pelo modo nativo desta versão.
