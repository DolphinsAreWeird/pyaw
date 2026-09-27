#!/bin/bash
# Runs the unit tests. With only the Command Line Tools installed, the Swift Testing framework
# lives outside the default search path, so point the compiler and linker at it.
set -euo pipefail
cd "$(dirname "$0")/.."
FW=/Library/Developer/CommandLineTools/Library/Developer/Frameworks
if [ -d "$FW/Testing.framework" ]; then
  LIB=/Library/Developer/CommandLineTools/Library/Developer/usr/lib
  exec swift test -Xswiftc -F -Xswiftc "$FW" -Xlinker -F -Xlinker "$FW" -Xlinker -rpath -Xlinker "$FW" \
    -Xlinker -rpath -Xlinker "$LIB" "$@"
else
  exec swift test "$@"
fi
