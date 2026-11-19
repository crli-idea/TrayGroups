$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source
}
New-Item -ItemType Directory -Path "$PSScriptRoot\build" -Force | Out-Null
$binary = "$PSScriptRoot\build\capture-integration.tests.exe"
$coreSources = @('config\TrayConfig.cpp','tray\TrayIdentity.cpp','tray\TrayGuid.cpp','tray\TrayDiagnostics.cpp','ui\TrayInspector.cpp','ui\WallpaperPage.cpp','wallpaper\DesktopWallpaper.cpp') | ForEach-Object { Join-Path $projectRoot "src\$_" }
. (Join-Path $projectRoot 'test\GuiResources.ps1')
$guiResource = New-TestGuiResource $mingwBin $projectRoot (Join-Path $PSScriptRoot 'build')
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -DNATIVE_TEST_BUILD= -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows -o $binary "$PSScriptRoot\capture-integration.tests.cpp" @coreSources $guiResource -lcomctl32 -lshell32 -luser32 -lgdi32 -lpsapi -luxtheme -lz -lwtsapi32 -ladvapi32 -lole32 -luuid -lsetupapi -lcomdlg32
if ($LASTEXITCODE -ne 0) { throw 'Capture integration test build failed.' }
$process = Start-Process -FilePath $binary -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(15000)) { throw 'Capture integration test timed out.' }
Get-Content "$PSScriptRoot\build\capture-integration\report.txt"
if ($process.ExitCode -ne 0) { throw 'Capture integration test failed.' }
