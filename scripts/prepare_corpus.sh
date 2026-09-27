#!/bin/bash
# Cleans the downloaded corpora (Zawgyi→Unicode, normalization, syllable splitting) into data/clean/.
set -euo pipefail
cd "$(dirname "$0")/.."
swift build -c release --product build-model
BM=.build/release/build-model
mkdir -p data/clean
echo "== CC-100";  xz -dc data/raw/cc100-my.txt.xz | $BM clean > data/clean/cc100.txt
if [ -f data/raw/opensubtitles-my.txt.gz ]; then
  echo "== OpenSubtitles"; gzip -dc data/raw/opensubtitles-my.txt.gz | $BM clean > data/clean/subtitles.txt
fi
ls -la data/clean
