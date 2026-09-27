#!/bin/bash
# Builds Pyaw from source and installs it into ~/Library/Input Methods.
#   MODEL_DIR=…   which model to bundle (default: build/model)
set -euo pipefail
cd "$(dirname "$0")/.."
./scripts/build_app.sh
DEST="$HOME/Library/Input Methods"
mkdir -p "$DEST"
pkill -x Pyaw 2>/dev/null || true   # the new copy is started on the next keystroke
rm -rf "$DEST/Pyaw.app"
cp -R build/Pyaw.app "$DEST/"
"$DEST/Pyaw.app/Contents/MacOS/Pyaw" --register
echo
echo "Installed to $DEST/Pyaw.app"
echo "First time only: System Settings → Keyboard → Text Input → Edit… → + → Burmese → Pyaw"
