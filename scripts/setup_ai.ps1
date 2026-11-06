param([string]$Repository = "$HOME\PianistTransformer")
$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
if (!(Test-Path "$Repository\.git")) { git clone https://github.com/yhj137/PianistTransformer.git $Repository }
Push-Location $Repository
try {
    py -3.11 -m venv .venv
    $python = Join-Path $Repository '.venv\Scripts\python.exe'
    & $python -m pip install --upgrade pip
    & $python -m pip install torch==2.7.1 torchvision==0.22.1 torchaudio==2.7.1
    & $python -m pip install -r requirements.txt
    # The VST3 plugin runs `python -m expressive_midi render` with this venv's Python.
    & $python -m pip install -e $Project
    & $python -m src.utils.download_model --source huggingface
    Write-Output "AI repo: $Repository"
    Write-Output "AI Python: $python"
    Write-Output "In the plugin: AI settings > PianistTransformer folder = $Repository"
} finally { Pop-Location }
