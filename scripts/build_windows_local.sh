#!/bin/bash
# Compile-checks the Windows input method on a Mac with Zig (x86_64). Official builds use MSVC
# on GitHub Actions (.github/workflows/windows.yml).
set -euo pipefail
cd "$(dirname "$0")/.."
ZIG=.tools/zig/zig
[ -x "$ZIG" ] || { echo "Zig not found in .tools/zig (see CONTRIBUTING.md)"; exit 1; }
mkdir -p build/win
(cd windows/src && "../../$ZIG" rc /fo ../../build/win/pyaw.res pyaw.rc)
"$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 -ffp-contract=off -DUNICODE -D_UNICODE \
  -Wall -Wno-unused-parameter -Wno-missing-field-initializers -Wno-nullability-completeness -shared \
  cpp/pyaw/*.cpp windows/src/*.cpp build/win/pyaw.res windows/src/pyaw.def -o build/win/pyaw64.dll \
  -lole32 -loleaut32 -luuid -luser32 -lgdi32 -ladvapi32 -lshell32 -ldwmapi
"$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 windows/tests/typing_test.cpp -o build/win/typing_test.exe \
  -lole32 -luuid -luser32
echo "Built build/win/pyaw64.dll and build/win/typing_test.exe"
