$ErrorActionPreference = 'Stop'
$buildOutput = Join-Path $PSScriptRoot 'test\build'
New-Item -ItemType Directory -Path $buildOutput -Force | Out-Null
& 'C:\Environment\msys64\mingw64\bin\g++.exe' -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows '-Wl,--nxcompat,--dynamicbase' -o "$buildOutput\ScreenCaptureTimer.exe" "$PSScriptRoot\ScreenCaptureTimer.cpp" -lz -lwtsapi32 -ladvapi32 -lshell32 -luser32 -lgdi32 -lpsapi
if ($LASTEXITCODE -ne 0) { throw 'Production build failed.' }
Write-Output "Production executable: $buildOutput\ScreenCaptureTimer.exe"
