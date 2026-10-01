#!/bin/bash
source "$(dirname "$0")/local-env.sh"
mkdir -p tests/data/generated
# Existing macOS voices, explicit development-only synthesis. No app TTS implementation
# or bundled-voice claim. This script never downloads or installs a voice.
for voice in Luciana Samantha Daniel; do
    /usr/bin/say -v '?' | /usr/bin/awk -v voice="$voice" '$1 == voice { found=1 } END { exit !found }' || { echo "Voice not available: $voice"; exit 2; }
done
scripts/project-local.sh /usr/bin/say -v Luciana -r 155 -o tests/data/generated/ptbr.wav --file-format=WAVE --data-format=LEI16@16000 'Vamos começar a reunião. Eu preciso de cinco minutos para organizar os documentos. Depois podemos conversar sobre o projeto.'
scripts/project-local.sh /usr/bin/say -v Samantha -r 155 -o tests/data/generated/enus.wav --file-format=WAVE --data-format=LEI16@16000 'Let us begin the meeting. I need five minutes to organize the documents. Then we can talk about the project.'
scripts/project-local.sh /usr/bin/say -v Daniel -r 155 -o tests/data/generated/engb.wav --file-format=WAVE --data-format=LEI16@16000 'I think we should wait. Please give me five minutes to check the documents before we start the meeting.'
