#!/usr/bin/env bash
# Extract the Fable 2 game files from your own Xbox 360 disc image (Linux
# replacement for the launcher's "Extract from ISO" button).
#
#   tools/linux/extract_iso.sh <image.iso> [destination]
#
# Like the Windows launcher, it first extracts only default.xex, checks its
# SHA-256 against the supported versions in constants/game_versions.json, and
# only on a match extracts default.xex + data/ into the destination
# (default: the repository root, where the build expects them).
set -euo pipefail
here="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
root="$(cd "$here/../.." && pwd)"
iso="${1:?usage: $0 <image.iso> [destination]}"
dest="${2:-$root}"
[[ -f "$iso" ]] || { echo "error: $iso not found" >&2; exit 1; }

tool="$(command -v x360extract || true)"
[[ -n "$tool" ]] || tool="$root/out/tooling/x360extract/x360extract"
if [[ ! -x "$tool" ]]; then
  echo "x360extract not built yet; building it (needs the .NET 8 SDK) ..." >&2
  "$root/tools/x360extract/build.sh"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$tool" "$iso" --files default.xex -o "$tmp" --quiet
[[ -f "$tmp/default.xex" ]] || { echo "error: no default.xex on this disc image" >&2; exit 2; }

hash="$(sha256sum "$tmp/default.xex" | cut -d' ' -f1)"
name="$(python3 - "$root/constants/game_versions.json" "$hash" <<'PY'
import json, sys
catalog = json.load(open(sys.argv[1]))
for v in catalog["versions"]:
    if v.get("hash") == sys.argv[2] and v.get("compatible"):
        print(v["name"]); break
PY
)"
if [[ -z "$name" ]]; then
  echo "error: default.xex SHA-256 $hash is not a supported Fable 2 version." >&2
  echo "       See docs/GERMAN_GOTY_SUPPORT.md for the supported discs." >&2
  exit 3
fi
echo "Detected: $name ($hash)"

mkdir -p "$dest"
echo "Extracting default.xex and data/ to $dest (about 7 GB) ..."
"$tool" "$iso" --files default.xex,data -o "$dest"
echo "Done."
