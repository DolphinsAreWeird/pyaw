#!/bin/bash
# Downloads the training data into data/raw/.
#   default:           CC-100 Burmese web text + the Myanglish chat dictionary (the public model)
#   --with-subtitles:  also OPUS OpenSubtitles Burmese (better for chat; its licensing is unclear,
#                      so models built with it are for personal use)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p data/raw
fetch() { [ -f "data/raw/$1" ] && echo "have $1" || { echo "downloading $1"; curl -fL --progress-bar -o "data/raw/$1" "$2"; }; }
fetch cc100-my.txt.xz https://data.statmt.org/cc-100/my.txt.xz
fetch myanglish_mapping.json https://raw.githubusercontent.com/AUNGSWANOOgit/myanglish/main/myanglish_mapping.json
if [ "${1:-}" = "--with-subtitles" ]; then
  fetch opensubtitles-my.txt.gz https://object.pouta.csc.fi/OPUS-OpenSubtitles/v2024/mono/my.txt.gz
fi
