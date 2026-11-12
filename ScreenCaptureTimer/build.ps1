$ErrorActionPreference = 'Stop'
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source
}
$buildOutput = Join-Path $PSScriptRoot 'test\build'
New-Item -ItemType Directory -Path $buildOutput -Force | Out-Null
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows '-Wl,--nxcompat,--dynamicbase' -o "$buildOutput\ScreenCaptureTimer.exe" "$PSScriptRoot\ScreenCaptureTimer.cpp" -lz -lwtsapi32 -ladvapi32 -lshell32 -luser32 -lgdi32 -lpsapi
if ($LASTEXITCODE -ne 0) { throw 'Production build failed.' }
Write-Output "Production executable: $buildOutput\ScreenCaptureTimer.exe"
