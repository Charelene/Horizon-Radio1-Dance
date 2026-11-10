#!/usr/bin/env bash
# BBC Radio 1 Dance installer for Linux (Proton/Wine):  curl -fsSL <url>/install.sh | bash
# Downloads the installer binary from the latest GitHub release, checks its SHA-256, and runs it.
# Extra arguments are passed on:  curl -fsSL <url>/install.sh | bash -s -- --azimuth ~/Games/Azimuth
set -euo pipefail

REPO="Charelene/Horizon-Radio1-Dance"

case "$(uname -m)" in
  x86_64|amd64) asset="r1d-installer-linux-x86_64" ;;
  *) echo "No Linux installer is published for $(uname -m) yet." >&2; exit 1 ;;
esac

base="https://github.com/$REPO/releases/latest/download"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "Downloading the BBC Radio 1 Dance installer ..."
curl -fsSL -o "$tmp/$asset" "$base/$asset"
curl -fsSL -o "$tmp/SHA256SUMS" "$base/SHA256SUMS"

want="$(awk -v f="$asset" '$2 == f { print $1 }' "$tmp/SHA256SUMS")"
got="$(sha256sum "$tmp/$asset" | cut -d' ' -f1)"
if [ -z "$want" ] || [ "$want" != "$got" ]; then
  echo "Checksum mismatch for $asset, not running it." >&2
  exit 1
fi
chmod +x "$tmp/$asset"

# when piped from curl, stdin is the script itself; give the installer the keyboard instead
if [ -r /dev/tty ]; then
  "$tmp/$asset" "$@" </dev/tty
else
  "$tmp/$asset" --yes "$@" </dev/null
fi
