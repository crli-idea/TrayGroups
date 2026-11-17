$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($script in @(
    (Join-Path $PSScriptRoot 'capture-integration.ps1'),
    (Join-Path $PSScriptRoot 'run.ps1'),
    (Join-Path $projectRoot 'ScreenCaptureTimer\test\run.ps1')
)) {
    try {
        & $script
        if ($LASTEXITCODE -ne 0) { throw "Exit code $LASTEXITCODE" }
    } catch {
        $failures.Add("$script`: $_")
    }
}

if ($failures.Count) { throw ($failures -join [Environment]::NewLine) }
Write-Output 'All project tests passed.'
