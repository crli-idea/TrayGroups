$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($script in @(
    (Join-Path $PSScriptRoot 'core-modules.ps1'),
    (Join-Path $PSScriptRoot 'recovery-lifecycle.ps1'),
    (Join-Path $PSScriptRoot 'capture-integration.ps1'),
    (Join-Path $PSScriptRoot 'run.ps1'),
    (Join-Path $PSScriptRoot 'settings-shell.ps1'),
    (Join-Path $PSScriptRoot 'capture\run.ps1')
)) {
    try {
        & $script
        if ($LASTEXITCODE -ne 0) { throw "Exit code $LASTEXITCODE" }
    } catch {
        $failures.Add("$script`: $_")
    }
}

try {
    & (Join-Path $PSScriptRoot 'guid-research\run.ps1') -SelfTest
} catch {
    $failures.Add("GUID regression: $_")
}
if ($failures.Count) { throw ($failures -join [Environment]::NewLine) }
Write-Output 'All project tests passed.'
