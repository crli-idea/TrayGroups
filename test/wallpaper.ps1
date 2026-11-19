$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) { $mingwBin = Split-Path -Parent (Get-Command g++.exe).Source }
$output = Join-Path $PSScriptRoot 'build\wallpaper'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$binary = Join-Path $output 'wallpaper.tests.exe'
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -DNATIVE_TEST_BUILD -D_WIN32_WINNT=0x0A00 -Os -s -static -fno-exceptions -fno-rtti -municode -o $binary "$PSScriptRoot\wallpaper.tests.cpp" "$projectRoot\src\wallpaper\DesktopWallpaper.cpp" -lole32 -luuid -lshell32 -lsetupapi -ladvapi32 -lgdi32 -luser32
if ($LASTEXITCODE -ne 0) { throw 'Wallpaper test build failed.' }
& $binary $output
if ($LASTEXITCODE -ne 0) { throw 'Wallpaper tests failed.' }
