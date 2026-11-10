#!/usr/bin/env bash
# Builds the release files into installer/dist: the Linux binary, the Windows engine .exe, install.sh, SHA256SUMS,
# and (with Inno Setup's iscc on PATH) the Windows setup wizard BBCRadio1Dance-Setup.exe.
# The mod DLL, the station data and the HUD icon are compiled in, so run this after changing any of them.
# Needs mingw-w64 and the Rust target:  rustup target add x86_64-pc-windows-gnu
set -euo pipefail
cd "$(dirname "$0")"
../mod/build.sh
cargo build --release
cargo build --release --target x86_64-pc-windows-gnu
rm -rf dist && mkdir dist
cp target/release/r1d-installer dist/r1d-installer-linux-x86_64
cp target/x86_64-pc-windows-gnu/release/r1d-installer.exe dist/r1d-installer-windows-x86_64.exe
# the Windows setup wizard, when Inno Setup's compiler is around (https://jrsoftware.org/isinfo.php)
if command -v iscc >/dev/null; then iscc -Q windows/radio1-dance.iss; fi
cp install.sh dist/
(cd dist && shopt -s nullglob && sha256sum r1d-installer-* BBCRadio1Dance-Setup* > SHA256SUMS)
echo "built:"; ls -l dist
