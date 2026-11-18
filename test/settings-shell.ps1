$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $PSScriptRoot 'build\settings-shell'
$portable = Join-Path $output 'portable'
New-Item -ItemType Directory -Path $portable -Force | Out-Null
if (-not ('TrayGroupsSettingsShell' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class TrayGroupsSettingsShell {
    public delegate bool Callback(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string name);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    public static IntPtr Dialog(uint pid) {
        IntPtr result=IntPtr.Zero;
        EnumWindows((window, data) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if(owner==pid) {
                var text=new StringBuilder(64); GetClassName(window,text,64);
                if(text.ToString()=="#32770") result=window;
            }
            return true;
        },IntPtr.Zero);
        return result;
    }
}
'@
}
if ([TrayGroupsSettingsShell]::FindWindow('Crli.TrayGroups.Native', $null) -ne [IntPtr]::Zero) {
    throw 'Normally exit the resident TrayGroups before the production settings test.'
}
$binary = Join-Path $portable 'TrayGroups.exe'
Copy-Item -LiteralPath (Join-Path $projectRoot 'test\build\TrayGroups.exe') -Destination $binary -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'defaults\groups.ini') -Destination (Join-Path $portable 'groups.ini') -Force
@{OutputDirectory=(Join-Path $portable 'Captures'); FileNameFormat='HH-mm'; IntervalMinutes=1440} |
    ConvertTo-Json -Compress | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $portable 'config.json')
$checks = [System.Collections.Generic.List[string]]::new()
$app = $null
try {
    $app = Start-Process -FilePath $binary -WindowStyle Hidden -PassThru
    foreach ($page in 0..4) {
        if ($page -gt 0) {
            $opener = Start-Process -FilePath $binary -WindowStyle Hidden -PassThru
            if (-not $opener.WaitForExit(5000)) { throw 'Second-instance settings request timed out.' }
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $dialog = [TrayGroupsSettingsShell]::Dialog($app.Id)
            if ($dialog -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 20
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($dialog -eq [IntPtr]::Zero) { throw "Settings page $page did not open." }
        # PSM_SETCURSEL uses the real production PropertySheet and message loop.
        [void][TrayGroupsSettingsShell]::SendMessage($dialog, 1125, [IntPtr]$page, [IntPtr]::Zero)
        $tabs = [TrayGroupsSettingsShell]::SendMessage($dialog, 1140, [IntPtr]::Zero, [IntPtr]::Zero)
        $selected = [TrayGroupsSettingsShell]::SendMessage($tabs, 4875, [IntPtr]::Zero, [IntPtr]::Zero)
        if ($selected.ToInt32() -ne $page) { throw "Production page $page was not selected." }
        # The title-bar X sends WM_SYSCOMMAND / SC_CLOSE, rather than WM_CLOSE.
        [void][TrayGroupsSettingsShell]::SendMessage($dialog, 274, [IntPtr]61536, [IntPtr]::Zero)
        if ([TrayGroupsSettingsShell]::IsWindow($dialog)) { throw "Production title-bar X did not close page $page." }
        $app.Refresh()
        if ($app.HasExited) { throw 'Closing settings unexpectedly terminated the tray program.' }
        $checks.Add("PASS production Release EXE: X closes page $page and the tray process stays alive")
    }
} finally {
    if ($app -and -not $app.HasExited) {
        $stopper = Start-Process -FilePath $binary -ArgumentList '--stop' -WindowStyle Hidden -PassThru
        if (-not $stopper.WaitForExit(5000) -or -not $app.WaitForExit(15000)) {
            throw 'Production settings test did not exit normally; it was not force-terminated.'
        }
    }
    $checks | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $output 'report.txt')
}
$checks
