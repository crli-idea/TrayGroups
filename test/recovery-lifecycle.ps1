$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) { $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source }
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
$binary = Join-Path $PSScriptRoot 'build\recovery-lifecycle.tests.exe'
$coreSources = @('config\TrayConfig.cpp','tray\TrayIdentity.cpp','tray\TrayGuid.cpp','tray\TrayDiagnostics.cpp','ui\TrayInspector.cpp') | ForEach-Object { Join-Path $projectRoot "src\$_" }
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows -o $binary "$PSScriptRoot\recovery-lifecycle.tests.cpp" @coreSources -lcomctl32 -lshell32 -luser32 -lgdi32 -lpsapi -luxtheme -lz -lwtsapi32 -ladvapi32 -lole32 -luuid
if ($LASTEXITCODE -ne 0) { throw 'Recovery lifecycle test build failed.' }
$process = Start-Process -FilePath $binary -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(55000)) { throw 'Recovery lifecycle test timeout.' }
Get-Content -LiteralPath "$PSScriptRoot\build\recovery-lifecycle\report.txt" -Encoding UTF8
if ($process.ExitCode -ne 0) { throw "Recovery lifecycle test failed: $($process.ExitCode)" }
