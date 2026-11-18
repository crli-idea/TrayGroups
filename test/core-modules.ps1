$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source
}
$output = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @(
    (Join-Path $PSScriptRoot 'core-modules.tests.cpp'),
    (Join-Path $projectRoot 'src\config\TrayConfig.cpp'),
    (Join-Path $projectRoot 'src\tray\TrayIdentity.cpp'),
    (Join-Path $projectRoot 'src\tray\TrayDiagnostics.cpp')
)
$binary = Join-Path $output 'core-modules.tests.exe'
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -o $binary @sources -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw 'Core module build failed.' }
& $binary
if ($LASTEXITCODE -ne 0) { throw 'Core module tests failed.' }
