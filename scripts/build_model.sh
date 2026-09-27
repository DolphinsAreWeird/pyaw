#!/bin/bash
# Builds the language model + lexicon into build/model from the cleaned corpora.
#   default:           CC-100 only (the model shipped in public releases)
#   --with-subtitles:  CC-100 + OpenSubtitles weighted 3× (more accurate for chat, personal use)
#   OUT_DIR=…          where to write the model (default: build/model)
set -euo pipefail
cd "$(dirname "$0")/.."
swift build -c release --product build-model
CORPORA=(data/clean/cc100.txt:1)
if [ "${1:-}" = "--with-subtitles" ]; then CORPORA+=(data/clean/subtitles.txt:3); fi
.build/release/build-model build "${OUT_DIR:-build/model}" data/raw/myanglish_mapping.json \
  --lexicon data/lexicon/community.tsv "${CORPORA[@]}" 2>&1 | grep -v "extracted:"
