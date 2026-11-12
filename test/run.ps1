$ErrorActionPreference = 'Stop'
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildOutput = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $buildOutput -Force | Out-Null
$resourceObject = Join-Path $buildOutput 'TrayGroups.resources.o'
Push-Location $projectRoot
try {
    & (Join-Path $mingwBin 'windres.exe') -i TrayGroups.rc -o $resourceObject
    if ($LASTEXITCODE -ne 0) { throw 'Icon resource build failed.' }
} finally { Pop-Location }
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -Os -s -static -fno-exceptions -fno-rtti -municode -mwindows '-Wl,--nxcompat,--dynamicbase' -o "$PSScriptRoot\TrayGroups.tests.exe" "$PSScriptRoot\TrayGroups.tests.cpp" $resourceObject -lcomctl32 -lshell32 -luser32 -lgdi32 -lpsapi -luxtheme -lz -lwtsapi32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
$testProcess = Start-Process -FilePath "$PSScriptRoot\TrayGroups.tests.exe" -WindowStyle Hidden -PassThru
if (-not $testProcess.WaitForExit(25000)) { throw 'Test timeout.' }
if ($testProcess.ExitCode -ne 0) { throw 'Test failed; see reports in test directory.' }
$testProcess = Start-Process -FilePath "$PSScriptRoot\TrayGroups.tests.exe" -ArgumentList '--ui' -WindowStyle Hidden -PassThru
if (-not $testProcess.WaitForExit(25000)) { throw 'UI test timeout.' }
if ($testProcess.ExitCode -ne 0) { throw 'UI test failed.' }
$testProcess = Start-Process -FilePath "$PSScriptRoot\TrayGroups.tests.exe" -ArgumentList '--ui96' -WindowStyle Hidden -PassThru
if (-not $testProcess.WaitForExit(25000)) { throw '100% UI test timeout.' }
if ($testProcess.ExitCode -ne 0) { throw '100% UI test failed.' }
Get-Content -LiteralPath "$PSScriptRoot\self-test.txt","$PSScriptRoot\ui-self-test.txt"
