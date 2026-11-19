$ErrorActionPreference = 'Stop'
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildOutput = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $buildOutput -Force | Out-Null
$resourceObject = Join-Path $buildOutput 'TrayGroups.resources.o'
Push-Location (Join-Path $projectRoot 'resources')
try {
    & (Join-Path $mingwBin 'windres.exe') -i TrayGroups.rc -o $resourceObject
    if ($LASTEXITCODE -ne 0) { throw 'Icon resource build failed.' }
} finally { Pop-Location }
$coreSources = @('config\TrayConfig.cpp','tray\TrayIdentity.cpp','tray\TrayGuid.cpp','tray\TrayDiagnostics.cpp','ui\TrayInspector.cpp','ui\WallpaperPage.cpp','wallpaper\DesktopWallpaper.cpp') | ForEach-Object { Join-Path $projectRoot "src\$_" }
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -DNATIVE_TEST_BUILD= -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows '-Wl,--nxcompat,--dynamicbase' -o "$buildOutput\TrayGroups.tests.exe" "$PSScriptRoot\TrayGroups.tests.cpp" @coreSources $resourceObject -lcomctl32 -lshell32 -luser32 -lgdi32 -lpsapi -luxtheme -lz -lwtsapi32 -ladvapi32 -lole32 -luuid -lsetupapi -lcomdlg32
if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
$testProcess = Start-Process -FilePath "$buildOutput\TrayGroups.tests.exe" -WindowStyle Hidden -PassThru
if (-not $testProcess.WaitForExit(25000)) { throw 'Test timeout.' }
if ($testProcess.ExitCode -ne 0) { throw 'Test failed; see reports in test directory.' }
$testProcess = Start-Process -FilePath "$buildOutput\TrayGroups.tests.exe" -ArgumentList '--ui' -WindowStyle Hidden -PassThru
if (-not $testProcess.WaitForExit(45000)) { throw 'UI test timeout.' }
if ($testProcess.ExitCode -ne 0) { throw 'UI test failed.' }
$testProcess = Start-Process -FilePath "$buildOutput\TrayGroups.tests.exe" -ArgumentList '--ui96' -WindowStyle Hidden -PassThru
if (-not $testProcess.WaitForExit(45000)) { throw '100% UI test timeout.' }
if ($testProcess.ExitCode -ne 0) { throw '100% UI test failed.' }
Get-Content -LiteralPath "$buildOutput\self-test.txt","$buildOutput\ui-self-test.txt"
