#!/bin/bash
# Assembles build/Pyaw.app (the input method bundle) and signs it ad hoc.
#   --universal   build for both Apple Silicon and Intel
#   MODEL_DIR=…   which model to bundle (default: build/model)
set -euo pipefail
cd "$(dirname "$0")/.."
MODEL_DIR="${MODEL_DIR:-build/model}"
[ -f "$MODEL_DIR/model.bin" ] || { echo "Missing $MODEL_DIR/model.bin — run scripts/build_model.sh first"; exit 1; }
APP=build/Pyaw.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
if [ "${1:-}" = "--universal" ]; then
  for arch in arm64 x86_64; do swift build -c release --product Pyaw --triple "$arch-apple-macosx13.0"; done
  lipo -create -output "$APP/Contents/MacOS/Pyaw" \
    .build/arm64-apple-macosx/release/Pyaw .build/x86_64-apple-macosx/release/Pyaw
else
  swift build -c release --product Pyaw
  cp .build/release/Pyaw "$APP/Contents/MacOS/Pyaw"
fi
cp Resources/Info.plist "$APP/Contents/Info.plist"
cp Resources/MenuIcon.tiff "$APP/Contents/Resources/"
cp -R Resources/en.lproj Resources/my.lproj "$APP/Contents/Resources/"
cp "$MODEL_DIR/model.bin" "$MODEL_DIR/lexicon.tsv" "$APP/Contents/Resources/"
codesign --force --sign - --timestamp=none "$APP"
echo "Built $APP ($(du -sh "$APP" | cut -f1), $(lipo -archs "$APP/Contents/MacOS/Pyaw"))"
