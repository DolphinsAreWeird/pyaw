#!/bin/bash
# Builds the universal app with the public model and zips it into dist/ for a GitHub release.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" Resources/Info.plist)
./scripts/build_app.sh --universal
mkdir -p dist
ZIP="dist/Pyaw-$VERSION-macOS.zip"
rm -f "$ZIP"
ditto -c -k --keepParent build/Pyaw.app "$ZIP"
shasum -a 256 "$ZIP" | tee "$ZIP.sha256"
echo "Packaged $ZIP ($(du -h "$ZIP" | cut -f1))"
