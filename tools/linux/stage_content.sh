#!/usr/bin/env bash
# Make the game content visible next to a built fable_2 (Linux port of
# tools/stage_content.cmd). Instead of copying ~7 GB it symlinks default.xex,
# data/ and the optional disc folders into the build directory.
#
#   tools/linux/stage_content.sh <build dir> [content root]
#
# content root defaults to the repository root (where extract_iso.sh puts
# the files). Alternatively skip staging and start the game with
#   --game_data_root=<content root>
set -euo pipefail
here="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
dest="$(readlink -f "${1:?usage: $0 <build dir> [content root]}")"
src="$(readlink -f "${2:-$here/../..}")"

[[ -f "$src/default.xex" ]] || {
  echo "error: no default.xex in $src - run tools/linux/extract_iso.sh first" >&2; exit 1; }
[[ -d "$dest" ]] || { echo "error: build dir $dest does not exist" >&2; exit 1; }

for item in default.xex data nxeart '$SystemUpdate'; do
  [[ -e "$src/$item" ]] || continue
  if [[ -e "$dest/$item" && ! -L "$dest/$item" ]]; then
    # The build stages src/lua into <build>/data/scripts/recomp plus a stub
    # dir.manifest, and also into the content root's data/. That build-made
    # stub is safe to replace with a link to the real data/; anything else
    # is left alone.
    if [[ "$item" == data && -z "$(ls -A "$dest/data" | grep -vx -e scripts -e dir.manifest)" ]]; then
      rm -rf "$dest/data"
    else
      echo "skipping $item: $dest/$item already exists (remove it to link the content)" >&2
      continue
    fi
  fi
  ln -sfn "$src/$item" "$dest/$item"
done
echo "Game content linked into $dest"
