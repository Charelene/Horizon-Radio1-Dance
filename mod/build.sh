#!/usr/bin/env bash
# Builds mod/build/BBCRadio1Dance.dll (mingw-w64) for the FH2 Mod Platform.
# CC defaults to x86_64-w64-mingw32-gcc; with MSYS2 on Windows use CC=gcc (C:\msys64\mingw64\bin on PATH).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
CC=${CC:-x86_64-w64-mingw32-gcc}
PLATFORM=${FH2MOD_PLATFORM_VERSION:-2.8.0}
$CC -I third_party -DFH2MOD_PLATFORM_VERSION="\"$PLATFORM\"" -O2 -Wall -Wextra -shared -s -static-libgcc \
    -o build/BBCRadio1Dance.dll src/radio1_dance.c -lkernel32 -lwinhttp
echo "built: build/BBCRadio1Dance.dll"
