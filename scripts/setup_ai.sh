#!/usr/bin/env bash
set -euo pipefail
# Assumes Python 3.11, git, and network access. Requires significant disk space.
command -v python3.11 >/dev/null || { echo 'Install Python 3.11 first'; exit 1; }
ROOT="${1:-${HOME}/PianistTransformer}"
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ ! -d "$ROOT/.git" ]]; then
  git clone https://github.com/yhj137/PianistTransformer.git "$ROOT"
fi
cd "$ROOT"
python3.11 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install torch==2.7.1 torchvision==0.22.1 torchaudio==2.7.1
python -m pip install -r requirements.txt
# The VST3 plugin runs `python -m expressive_midi render` with this venv's Python.
python -m pip install -e "$PROJECT"
python -m src.utils.download_model --source huggingface
printf '\nAI repo: %s\nAI Python: %s/.venv/bin/python\nIn the plugin: AI settings > PianistTransformer folder = %s\n' "$PWD" "$PWD" "$PWD"
