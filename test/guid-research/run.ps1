param([switch]$SelfTest)
$ErrorActionPreference = 'Stop'
$mingwBin = 'C:\Environment\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $mingwBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source
}
$output = Join-Path $PSScriptRoot '..\build\guid-research'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$binary = Join-Path $output 'guid-probe.exe'
& (Join-Path $mingwBin 'g++.exe') -std=c++17 -Os -g -static -fno-exceptions -fno-rtti -municode -mwindows -Wall -Wextra -o $binary "$PSScriptRoot\guid-probe.cpp" "$PSScriptRoot\..\..\src\tray\TrayGuid.cpp" -lole32 -luuid -luser32 -lshell32
if ($LASTEXITCODE -ne 0) { throw 'GUID probe build failed.' }
$report = Join-Path $output 'com-snapshot.txt'
Set-Content -LiteralPath $report -Value 'Probe has not completed yet.' -Encoding UTF8
$start = @{ FilePath = $binary; WindowStyle = 'Hidden'; PassThru = $true }
if ($SelfTest) { $start.ArgumentList = '--self-test' }
$probe = Start-Process @start
if (-not $probe.WaitForExit(15000)) {
    Stop-Process -Id $probe.Id -Force
    throw 'GUID probe timed out; only the isolated probe was terminated.'
}
Get-Content -Encoding UTF8 -LiteralPath $report
if ($probe.ExitCode -ne 0) { throw "GUID probe failed: $($probe.ExitCode)" }
