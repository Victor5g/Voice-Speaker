#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/build-release.sh
DMG_STAGE="$(/usr/bin/mktemp -d "$ROOT/build/dmg.XXXXXX")"
DMG_CONTENT="$DMG_STAGE/content"
mkdir -p "$DMG_CONTENT"
scripts/project-local.sh /usr/bin/ditto artifacts/LocalVoiceTranslator.app "$DMG_CONTENT/LocalVoiceTranslator.app"
cat > "$DMG_CONTENT/LEIA-ME.txt" <<'TXT'
LocalVoiceTranslator 0.2.1 — uso pessoal neste Mac.
Abra LocalVoiceTranslator.app. Autorize o microfone e clique em Iniciar escuta.
Fale em português brasileiro ou inglês. STOP encerra áudio e limpa a conversa.
Para chamadas, abra Áudio → Chamada, escolha microfone/fones e confirme o uso de fones.
Clique em Preparar áudio da chamada no rodapé. Os caminhos serão preenchidos.
O modo nativo não instala driver; exige macOS 14.2+ e saída estéreo separada.
No app da chamada: microfone LocalVoiceTranslator Microphone e saída LocalVoiceTranslator Speakers.
Antes de sair do tradutor, restaure os dispositivos de áudio no app da chamada.
Consulte CONECTAR-CHAMADAS.md nesta imagem; use fones de ouvido.
Todos os modelos estão incluídos; o aplicativo não faz downloads.
Esta cópia usa assinatura ad-hoc e não foi notarizada para distribuição.
Os testes acústicos com alto-falantes, dispositivos e rede desligada pelo usuário
precisam ser registrados. Falas simultâneas e nomes próprios podem conter erros.
Relatório e procedimento completos: docs/FINAL_REPORT.md e docs/MANUAL_VALIDATION.md
no projeto de origem. Não distribuir como versão comercial.
TXT
/bin/cp docs/CALL_AUDIO_SETUP.md "$DMG_CONTENT/CONECTAR-CHAMADAS.md"
# Build HFS+ directly without mounting a temporary volume. hdiutil create
# requires operations refused by the project write sandbox; these two operations
# only write image files in the staging directory.
scripts/project-local.sh /usr/bin/hdiutil makehybrid -hfs -hfs-volume-name LocalVoiceTranslator \
    -o "$DMG_STAGE/LocalVoiceTranslator.hfs" "$DMG_CONTENT"
scripts/project-local.sh /usr/bin/hdiutil convert "$DMG_STAGE/LocalVoiceTranslator.hfs.dmg" \
    -format UDZO -o "$DMG_STAGE/LocalVoiceTranslator.dmg"
scripts/project-local.sh /usr/bin/codesign --force --sign - "$DMG_STAGE/LocalVoiceTranslator.dmg"
/usr/bin/hdiutil verify "$DMG_STAGE/LocalVoiceTranslator.dmg"
/usr/bin/codesign --verify --verbose=2 "$DMG_STAGE/LocalVoiceTranslator.dmg"
if [ -e "$ROOT/artifacts/LocalVoiceTranslator.dmg" ]; then /bin/mv "$ROOT/artifacts/LocalVoiceTranslator.dmg" "$DMG_STAGE/previous.dmg"; fi
/bin/mv "$DMG_STAGE/LocalVoiceTranslator.dmg" "$ROOT/artifacts/LocalVoiceTranslator.dmg"
/usr/bin/shasum -a 256 "$ROOT/artifacts/LocalVoiceTranslator.dmg" > artifacts/LocalVoiceTranslator.dmg.sha256
