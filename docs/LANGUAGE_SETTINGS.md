# Configuração dos idiomas da chamada

27/09/2026. A imagem `Downloads/performace/Screenshot 2026-09-27 at 22.58.27.png`
mostra o seletor “Caminho” do Engine. Esse controle alterava apenas o conjunto
de estatísticas, mas exibia idiomas sem oferecer edição. A configuração real
ficava na janela separada de Ajustes, sem acesso evidente no fluxo principal.

Também foi encontrada uma falha independente: ao inverter um par manual
PT → EN para EN → PT, escolher qualquer campo primeiro produzia um par
intermediário igual e a validação rejeitava a mudança. Isso foi confirmado
pelo fluxo de validação; a imagem, sozinha, não demonstra essa segunda falha.

## Implementação

- Aba **Idiomas e vozes** na barra lateral, compartilhada com Ajustes.
- Atalhos na Conversation, no Engine e na aba Áudio.
- Quatro seletores no início da tela: **Eu falo em**, **Eles ouvem em**,
  **Eles falam em** e **Eu ouço em**. O segundo grupo vale para o áudio de
  todos os participantes recebido da chamada.
- O Engine agora diz **Estatísticas de**, com explicação do filtro.
- Mudanças manuais invertem o outro campo quando necessário, numa operação
  válida e persistente. Automático preserva a escolha do outro campo.
- Preferências das duas direções continuam independentes; identificação,
  voz, pausa e fila não são redefinidas ao trocar idioma.
- Durante uma sessão, a tela explica o bloqueio e oferece **Parar sessão para
  editar**. A preparação de áudio também mantém o bloqueio até terminar.
- Identificação de falantes e demais controles ficam em **Vozes e processamento**.

Exemplo para conversar com pessoas falando inglês:

| Campo | Seleção |
|---|---|
| Eu falo em | Português brasileiro |
| Eles ouvem em | Inglês |
| Eles falam em | Inglês |
| Eu ouço em | Português brasileiro |

Em Áudio, selecione **Chamada** para ativar os dois caminhos. No modo Presencial,
apenas o grupo VOCÊ é aplicado ao microfone compartilhado. As escolhas de idioma
não substituem a preparação dos dispositivos e sua seleção no app da chamada.

## Validação

**43 XCTest passaram**, incluindo seis regressões novas: inversão em ambas as
ordens, origem manual no grupo de participantes, automático, persistência,
independência e bloqueio durante sessão/permissão. Não houve alteração no
motor de inferência; os benchmarks anteriores não foram repetidos.

Debug e Release de diagnóstico compilaram. O pacote passou pela assinatura
local, verificação de 13 modelos/2.187 arquivos e auditoria estática offline.
Logs: `artifacts/language-settings-swift-tests.log`,
`artifacts/language-settings-build-debug.log` e `artifacts/language-settings-release.log`.

A captura visual automatizada não foi concluída. O lançamento direto do probe
sob `project-local.sh` falhou antes de entrar no aplicativo: SIGTRAP em
`_libsecinit_appsandbox`, assinatura `SYSCALL_SET_USERLAND_PROFILE`. Não houve
relaxamento do isolamento nem interação com a instância aberta pelo usuário.
Os testes de estado não equivalem a uma verificação de cliques nos menus;
essa inspeção visual permanece pendente.

O pacote atualizado está em `artifacts/LocalVoiceTranslator-Diagnostic.app`.
Uma instância já aberta precisa ser encerrada e reaberta para carregar a nova
interface. As limitações de reconhecimento e aceitação do tradutor descritas
em `REALTIME_IMPROVEMENTS.md` permanecem; esta correção não aprova o release completo.

## Restaurar configurações padrão

O botão **Restaurar padrões…** fica no topo desta tela, inclusive em Ajustes.
Só funciona com sessão, permissão e preparação de áudio inativas. A confirmação
descreve o que será alterado; cancelar não executa a restauração.

A ação restaura `DirectionPreferences()` nas duas direções, `AudioRouting()`
(Presencial/dispositivos padrão), Balanced, backend padrão Metal e o glossário
original do pacote. Vozes automáticas, identificação e saída falada ficam
ativadas; pausa volta a 300 ms e fila a 6 s. VOCÊ volta a automático → idioma
oposto; PARTICIPANTES volta a automático → PT-BR. Termos/categorias pessoais são
substituídos, conforme aviso. Os dispositivos nativos criados pelo app são
fechados; a chamada exige nova preparação. Não são alterados os dispositivos
de terceiros, permissões do macOS, modelos ou arquivos exportados.

O glossário original é validado antes de alterar as configurações. Se estiver
ausente ou inválido, a operação mostra erro e mantém as preferências anteriores.
Somente as chaves de preferências conhecidas são removidas; a operação não apaga
o domínio inteiro de UserDefaults. O relatório de desempenho em memória é
mantido para exportação, com a gravação encerrada.

Validação posterior: **47 XCTest passaram**, incluindo quatro novos testes de
restauração completa/persistência, proteção da sessão ativa, falha do glossário
original sem restauração parcial e recuperação de preferências corrompidas.
Evidência: `artifacts/restore-settings-swift-tests.log`. A inspeção visual/click
continua separada desses testes de estado, conforme limite acima.
