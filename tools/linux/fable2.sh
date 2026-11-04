#!/usr/bin/env bash
# Fable 2 Recompiled - Linux launcher.
#
# Runs fable_2 from its own folder (saves/, cache/, logs/ and fable_2.toml are
# written next to the binary, and game content is looked up there unless
# --game_data_root is given). Any arguments are passed straight through, e.g.
#   ./fable2.sh --fullscreen=true
#   ./fable2.sh --game_data_root=/path/to/extracted/disc
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
exec ./fable_2 "$@"
