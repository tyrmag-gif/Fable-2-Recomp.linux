#!/usr/bin/env bash
# Start Fable 2 with no usable audio device, to exercise SDL3's missing-device
# path. Affects only this process; no system audio setting is changed.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
export SDL_AUDIO_DRIVER=fable-test-unavailable-backend
exec ./fable_2 "$@"
