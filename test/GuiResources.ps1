# Standalone GUI tests load the same Common Controls v6 manifest as production.
function New-TestGuiResource([string]$CompilerBin, [string]$ProjectDir, [string]$OutputDir) {
    $source = Join-Path $OutputDir 'test-gui.resources.rc'
    $object = Join-Path $OutputDir 'test-gui.resources.o'
    $manifest = (Join-Path $ProjectDir 'resources\TrayGroups.manifest').Replace('\', '/')
    [IO.File]::WriteAllText($source, ('#pragma code_page(65001)' + [Environment]::NewLine + '1 24 "' + $manifest + '"'), [Text.UTF8Encoding]::new($false))
    & (Join-Path $CompilerBin 'windres.exe') -i $source -o $object
    if ($LASTEXITCODE -ne 0) { throw 'Test GUI manifest build failed.' }
    return $object
}
