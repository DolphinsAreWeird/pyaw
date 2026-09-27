#!/bin/bash
# One-line installer for Pyaw (ပြော):
#   curl -fsSL https://raw.githubusercontent.com/DolphinsAreWeird/pyaw/main/scripts/get.sh | bash
set -euo pipefail
REPO="DolphinsAreWeird/pyaw"
DEST="$HOME/Library/Input Methods"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "Finding the latest Pyaw release…"
URL=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
  | grep -o '"browser_download_url": *"[^"]*macOS\.zip"' | head -1 | sed 's/.*"\(https[^"]*\)"/\1/')
[ -n "$URL" ] || { echo "Could not find a release download."; exit 1; }

echo "Downloading $URL"
curl -fL --progress-bar -o "$TMP/Pyaw.zip" "$URL"
ditto -x -k "$TMP/Pyaw.zip" "$TMP"

mkdir -p "$DEST"
pkill -x Pyaw 2>/dev/null || true
rm -rf "$DEST/Pyaw.app"
mv "$TMP/Pyaw.app" "$DEST/"
xattr -dr com.apple.quarantine "$DEST/Pyaw.app" 2>/dev/null || true
"$DEST/Pyaw.app/Contents/MacOS/Pyaw" --register

cat <<'MSG'

✓ Pyaw is installed.
Last step: System Settings → Keyboard → Text Input → Input Sources: Edit… → + → Burmese → Pyaw → Add
Then switch to it with Control-Space or the menu bar. Type "br lote ny ll" and press Return: ဘာလုပ်နေလဲ

MSG
