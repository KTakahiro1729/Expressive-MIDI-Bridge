#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"
cmake -S "$ROOT/vst3/JuceMidiPlayer" -B "$ROOT/build/vst3" \
    -DCMAKE_BUILD_TYPE=Release "${@}"
cmake --build "$ROOT/build/vst3" --config Release --target ExpressiveMidiPlayer_VST3 -j 2
printf '\nFind the built .vst3 bundle under %s/build/vst3/\n' "$ROOT"
