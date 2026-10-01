# Glossário local de preservação

O recurso inicial `app/Resources/glossary-defaults.json` contém **1.477 termos únicos**, distribuídos em 11 categorias: **843 sempre preservar, 259 conforme o contexto e 375 traduzir normalmente**. A lista incorpora os candidatos fornecidos pelo usuário e amplia tecnologias, marcas, ferramentas, jogos, finanças, criação, moda, esportes e alimentação. Variantes equivalentes são reunidas numa entrada; categorias múltiplas não duplicam o termo.

Todas as regras iniciais são EN → PT-BR. PT-BR → EN pode ser ativado por termo. Outros pares são rejeitados, pois os modelos atuais não os implementam. Não se envia o glossário para nenhum serviço ou prompt de inferência.

## Configuração e persistência

O editor permite pesquisar, adicionar, editar, remover, ativar/desativar termos, selecionar categorias e direções, informar variantes/plurais, contexto favorável e contexto de exclusão. Importação e exportação usam JSON pelo painel de arquivos do macOS; importação substitui o documento inteiro somente depois de validar. Preferências ficam em UserDefaults no contêiner do aplicativo. Nenhum áudio ou conteúdo de conversa é salvo no glossário. Snapshot das regras entra na configuração da próxima sessão, separado das filas de inferência.

O schema 1 usa `enabled`, `categories` e `entries`. Cada entrada tem `id`, `term`, `forms`, `categories`, `mode` (`always`, `context`, `normal`), `directions`, `context`, `excludeContext` e `enabled`. Limites: 5.000 entradas, 128 categorias, 2 MiB por documento. Uma categoria ativa basta para ativar um termo associado a várias. Configuração corrompida falha explicitamente; não desaparece silenciosamente.

## Correspondência e restauração

`Glossary.hpp` aplica limites de palavras Unicode e comparação sem diferença de maiúsculas/minúsculas. Mantém a grafia realmente ocorrida, incluindo capitalização e plural. Expressões mais longas ganham prioridade entre regras de preservação; uma regra sempre preservar aplicável não é escondida por um composto configurado para tradução normal. Formas plurais/variantes são explícitas, sem flexionar automaticamente qualquer palavra.

O contexto considera a cláusula e a ocorrência mais próxima. Expressões que incluem o próprio termo precisam cobrir aquela ocorrência: em “My coach took a coach to the station”, a expressão “my coach” não ativa o ônibus. Exclusões prevalecem sobre pistas favoráveis. Exemplos curados: `coach` profissional versus ônibus, `call` reunião versus verbo chamar, `Spring` framework versus estação, `live` transmissão versus morar, `cloud` computação versus nuvem. As regras de contexto são determinísticas e editáveis; não constituem um classificador semântico universal.

O worker mantém um glossário validado por sessão. Para cada unidade, seleciona apenas as ocorrências aplicáveis. Se não houver termos, usa a tradução anterior sem solicitar atenção. Havendo termos, CTranslate2 traduz a sentença completa com atenção retornada, e SentencePiece fornece offsets de bytes para ligar tokens à origem e ao resultado. O texto preservado é inserido somente no intervalo alinhado; não há substituição por substring em palavras semelhantes nem corte da sentença em traduções independentes.

Termos já preservados exatamente pelo modelo não são reescritos: a comparação exige limites de palavras, grafia original e contagem completa das ocorrências. Para os demais, o alinhamento precisa concentrar pelo menos 60% da massa **lexical** da linha nos tokens do termo, com máximo individual de pelo menos 0,15. Os tokens estruturais `>>pob<<` e EOS, com offsets vazios, não entram no denominador lexical. Estes limites são defesas conservadoras de atenção, **não probabilidade calibrada de tradução correta**. Spans conflitantes, ausência de alinhamento, lacunas alinhadas a palavras alheias ou substituições que removam negações/alterem dígitos geram erro explícito e impedem a reprodução daquele resultado.

O texto resultante passa à mesma voz TTS já atribuída ao participante. O glossário não muda idioma de síntese, identidade, locutor ou modelo de voz. A inteligibilidade auditiva de cada empréstimo em inglês continua exigindo avaliação da voz PT-BR; nenhuma garantia de pronúncia foi inferida apenas por preservar a grafia.

Nomes próprios, marcas e identificadores cadastrados recebem a proteção de sua regra. Não há reconhecimento genérico de entidades para proteger nomes ausentes do glossário: esses casos dependem da tradução do modelo ou de cadastro pelo usuário. Também não há léxico de pronúncia específico por termo; manter a mesma voz e produzir PCM válido não garante inteligibilidade de cada estrangeirismo.

As pistas possessivas de `coach` incluem explicitamente `my/your/our coaches`, mantendo plural e capitalização originais. Pistas de transporte continuam prevalecendo. Essa ampliação trata exemplos definidos; não torna a classificação por contexto uma compreensão irrestrita de qualquer frase.

## Verificação

```sh
bash scripts/build-glossary-tests.sh
scripts/project-local.sh build/glossary-tests
scripts/project-local.sh build/glossary-translation-tests
```

O primeiro binário verifica regras, Unicode, ambiguidades, categorias, direções e alinhamentos adversariais com sanitizers. O segundo carrega o modelo real EN → PT-BR e compara termos obrigatórios, expressões, variantes e casos normais com o controle. Seus tempos são medições funcionais por frase, sem alegar latência E2E ou percentis de uma amostra pequena. `GlossaryStoreTests.swift` cobre CRUD, importação inválida, round-trip e restauração das preferências. O relatório integrado deve registrar quais testes de fato executaram e separar a inspeção auditiva/manual ainda pendente.

## Execução nativa em 2026-09-27

Passaram **37 verificações com ASan/UBSan** e **19 casos funcionais com o modelo real**. Evidências: `artifacts/realtime-improvements/glossary-tests.log`, `glossary-native.jsonl` e `glossary-validation.json`, com hashes de fontes, binários e seed. Os 778 candidatos únicos da lista do usuário estão representados como termos canônicos ou formas, sem colisões de alias entre entradas; `glossary-inventory.json` registra a contagem.

A primeira rodada descobriu reescritas indevidas de texto que o modelo já preservava: perda de “aplicativo” ao restaurar “Spring Boot” e duplicação “plug-ins-ins”. Os testes agora exigem igualdade com o controle quando este já contém todas as formas originais. O bypass por contagem exata corrigiu ambos. A inspeção de atenção real identificou massa estrutural de aproximadamente 0,329 em `>>pob<<`/EOS no token “chamada”; removê-la apenas do denominador lexical permite preservar “call” sem reduzir os limiares. Os logs anteriores permanecem em `glossary-native-before.jsonl`.

**Limite semântico ainda não resolvido:** “We took a coach to the station” resulta em “Levamos um treinador para a estação” no controle e com glossário. A regra corretamente não preserva o estrangeirismo profissional naquele contexto, mas o modelo traduz “coach” com sentido errado; paridade funcional não aprova essa tradução. A frase precisa significar ônibus. Os casos de `call` verbal e `Spring` estação produziram traduções adequadas nesta pequena amostra. A cobertura ampla de sentidos, a flexão gramatical após restauração e a pronúncia natural continuam exigindo validação.

## Continuidade de voz e PCM

`tests/cpp/GlossaryVoiceNative.cpp` exercita `ConversationPipeline` e `PCMPlayback` de produção com quatro finais ASR roteirizados do mesmo participante: unboxing, feedback, coach profissional e spoilers. A próxima fala só é admitida depois do evento `played` anterior. O teste verifica os termos traduzidos, a voz Faber em `queued`, a identidade correspondente no evento de término, a voz ainda carregada, energia do PCM e ausência de áudio/eventos depois de STOP.

O evento `played` não inclui o nome da voz: a evidência usa a associação explícita com o evento `queued` pelo identificador de sessão/segmento/origem/participante, sem inventar um campo de telemetria. Os WAVs de desenvolvimento `artifacts/realtime-improvements/glossary-voice-1.wav` a `glossary-voice-4.wav` são PCM mono 48 kHz, capturados depois da conversão, ganho e fades de reprodução. A renderização é acelerada; o teste não mede latência física nem valida diarização, pois os finais já trazem participante 1. A inteligibilidade dos termos exige escuta humana e permanece marcada `PENDING` nos resultados.

O teste compila com `scripts/build-production-test.sh` e integra a bateria serial `scripts/test-realtime-improvements.sh`. O gate exige a última linha JSON de `glossary-voice-native.jsonl` com resultado `PASS`; uma compilação isolada não satisfaz esse gate.

Execução realizada em 2026-09-27: **4/4 traduções, filas e reproduções concluídas com Faber**, sem descarte, troca de identidade ou resíduos depois de STOP. Os quatro WAVs foram relidos e confirmados como PCM16 mono 48 kHz, com energia diferente de zero, totalizando 361.920 amostras nesta execução. `glossary-voice-validation.json` registra hashes do teste, binário, fontes, seed e WAVs. O teste valida a associação e o percurso do PCM; **não comprova a inteligibilidade auditiva** dos termos, que continua pendente de escuta humana.

A revisão de plural acrescentou pistas possessivas para `coaches` e ancoragem por qualquer variante da mesma regra, evitando que a exclusão de `a coach` contamine outra ocorrência plural profissional na mesma frase. Seis verificações novas cobrem possessivos, capitalização, transporte e as duas ordens de sentidos mistos. No modelo real, “My coaches helped me” passou de “Meus treinadores me ajudaram” no controle para “Meus coaches me ajudaram”. Os registros anteriores à revisão estão em `artifacts/realtime-improvements/glossary-before-plural-fix/`; o erro semântico de ônibus permanece.
