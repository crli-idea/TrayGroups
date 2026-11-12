$ErrorActionPreference = 'Stop'
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source
}
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows '-Wl,--nxcompat,--dynamicbase' -o "$PSScriptRoot\ScreenCaptureTimer.tests.exe" "$PSScriptRoot\ScreenCaptureTimer.tests.cpp" -lz -lwtsapi32 -ladvapi32 -lshell32 -luser32 -lgdi32 -lpsapi
if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
$testProcess = Start-Process -FilePath "$PSScriptRoot\ScreenCaptureTimer.tests.exe" -WindowStyle Hidden -PassThru
if (-not $testProcess.WaitForExit(25000)) { throw 'Test timeout.' }
if ($testProcess.ExitCode -ne 0) { throw 'Test failed; see reports in test directory.' }
Get-Content -LiteralPath "$PSScriptRoot\native-self-test.txt"
