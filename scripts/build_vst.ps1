param([string]$JuceRoot = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$target = Join-Path $root 'build\vst3'
$args = @('-S', (Join-Path $root 'vst3\JuceMidiPlayer'), '-B', $target)
if ($JuceRoot -ne '') { $args += "-DJUCE_ROOT=$JuceRoot" }
& cmake @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $target --config Release --target ExpressiveMidiPlayer_VST3
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Built VST3 in $target"
