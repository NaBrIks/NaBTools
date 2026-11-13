param([string]$CoreDir=$env:PLATFORMIO_CORE_DIR)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!$CoreDir) { $CoreDir = Join-Path $env:USERPROFILE '.platformio' }
$env:PLATFORMIO_CORE_DIR = $CoreDir
$pio = Join-Path $CoreDir 'penv/Scripts/pio.exe'
if (!(Test-Path -LiteralPath $pio)) { $pio = Join-Path $env:USERPROFILE '.platformio/penv/Scripts/pio.exe' }
& $pio run -d (Join-Path $projectRoot 'examples/espidf_smoke') -e s3 -j 8
exit $LASTEXITCODE
