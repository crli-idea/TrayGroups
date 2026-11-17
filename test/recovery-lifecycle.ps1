$ErrorActionPreference = 'Stop'
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) { $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source }
$binary = Join-Path $PSScriptRoot 'build\recovery-lifecycle.tests.exe'
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows -o $binary "$PSScriptRoot\recovery-lifecycle.tests.cpp" -lcomctl32 -lshell32 -luser32 -lgdi32 -lpsapi -luxtheme -lz -lwtsapi32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Recovery lifecycle test build failed.' }
$process = Start-Process -FilePath $binary -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(55000)) { throw 'Recovery lifecycle test timeout.' }
Get-Content -LiteralPath "$PSScriptRoot\build\recovery-lifecycle\report.txt" -Encoding UTF8
if ($process.ExitCode -ne 0) { throw "Recovery lifecycle test failed: $($process.ExitCode)" }
