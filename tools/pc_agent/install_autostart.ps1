# install_autostart.ps1 - logon auto-start for the MicroLink PC tooling
# (tools/pc_agent). Launched by install_autostart.cmd; see README.md
# "Auto-start at logon". Per-user only, no admin rights needed. Modes:
# all (default), manager, agent, status, uninstall. Idempotent - a
# re-run overwrites the shortcut / task.

param(
    [ValidateSet('all', 'manager', 'agent', 'status', 'uninstall')]
    [string]$Mode = 'all'
)

$ErrorActionPreference = 'Stop'
$HERE = $PSScriptRoot
$TASK_NAME = 'MicroLink PC Agent'
$lnkPath = Join-Path ([Environment]::GetFolderPath('Startup')) 'PC Agent Manager.lnk'
$cfgPath = Join-Path $HERE 'config.json'
$user = "$env:USERDOMAIN\$env:USERNAME"
$fail = 0

function Step($m) { Write-Host ("  - " + $m) }
function Ok($m) { Write-Host ("  + " + $m) -ForegroundColor Green }
function Bad($m) {
    Write-Host ("  ! " + $m) -ForegroundColor Red
    $script:fail = 1
}

function Test-Tailscale {
    # PATH first, then the default install (pc_manager does the same).
    if (Get-Command tailscale -ErrorAction SilentlyContinue) { return $true }
    return (Test-Path 'C:\Program Files\Tailscale\tailscale.exe')
}

function Install-Manager {
    Write-Host "[manager] Startup shortcut -> windowless pc_manager (pyw/pythonw)"
    $py = Find-AgentExe
    if (-not $py) {
        Bad "no windowless Python launcher (pyw.exe / pythonw.exe) on PATH"
        return
    }
    $lnkArgs = $py.Pre + 'pc_manager.py --port 8766'
    $wsh = New-Object -ComObject WScript.Shell
    try {
        $lnk = $wsh.CreateShortcut($lnkPath)
        $lnk.TargetPath = $py.Exe
        $lnk.Arguments = $lnkArgs
        $lnk.WorkingDirectory = $HERE
        $lnk.WindowStyle = 7   # moot for a windowless target; kept consistent
        $lnk.Description = 'MicroLink PC Agent Manager - windowless (UI: http://127.0.0.1:8766)'
        $lnk.Save()
    } finally {
        [void][Runtime.InteropServices.Marshal]::ReleaseComObject($wsh)
    }
    if (Test-Path -LiteralPath $lnkPath) {
        Ok "installed: $lnkPath"
        Step ("target   : " + $py.Exe + " " + $lnkArgs)
        Step "at logon nothing pops up (no window, no browser tab)"
        Step "open http://127.0.0.1:8766 yourself; to stop it end pythonw.exe in Task Manager"
    } else {
        Bad "shortcut was not created"
    }
}

function Remove-Manager {
    if (Test-Path -LiteralPath $lnkPath) {
        Remove-Item -LiteralPath $lnkPath -Force
        Ok "removed: $lnkPath"
    } else {
        Step "manager shortcut: not installed"
    }
}

function Find-AgentExe {
    # Windowless Python launcher: pyw first (survives Python upgrades),
    # then pythonw. Skip the WindowsApps store-alias entries.
    foreach ($n in @('pyw', 'pythonw')) {
        $c = @(Get-Command $n -CommandType Application -ErrorAction SilentlyContinue |
               Where-Object { $_.Source -notmatch '\\WindowsApps\\' })[0]
        if ($c) {
            $pre = ''
            if ($n -eq 'pyw') { $pre = '-3 ' }
            return [pscustomobject]@{ Exe = $c.Source; Pre = $pre }
        }
    }
    return $null
}

function Get-AgentTask {
    Get-ScheduledTask -TaskName $TASK_NAME -ErrorAction SilentlyContinue
}

function Install-Agent {
    Write-Host "[agent] Task Scheduler task '$TASK_NAME' (at logon + 30 s)"
    $py = Find-AgentExe
    if (-not $py) {
        Bad "no windowless Python launcher (pyw.exe / pythonw.exe) on PATH"
        return
    }
    if (-not (Test-Path -LiteralPath $cfgPath)) {
        Bad "config.json missing in $HERE - create it first (README Setup step 1), then re-run"
        return
    }
    if (-not (Test-Tailscale)) {
        Step "WARNING: tailscale CLI not found - the agent binds the tailnet IP; install it first"
    }
    $xml = @'
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
<RegistrationInfo><Description>MicroLink PC Agent (tools/pc_agent). At logon with a 30 s delay so Tailscale is up before bind_host "auto" is resolved. Windowless (pyw/pythonw). No restart-on-failure: Stop in the manager UI stays stopped. Managed by tools/pc_agent/pc_manager.py.</Description></RegistrationInfo>
<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>__USER__</UserId><Delay>PT30S</Delay></LogonTrigger></Triggers>
<Principals><Principal id="Author"><UserId>__USER__</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>
<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><AllowHardTerminate>true</AllowHardTerminate><StartWhenAvailable>true</StartWhenAvailable><RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable><AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled><Hidden>false</Hidden><RunOnlyIfIdle>false</RunOnlyIfIdle><WakeToRun>false</WakeToRun><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</Priority></Settings>
<Actions Context="Author"><Exec><Command>__EXE__</Command><Arguments>__ARGS__</Arguments><WorkingDirectory>__CWD__</WorkingDirectory></Exec></Actions>
</Task>
'@
    $argLine = $py.Pre + 'pc_agent.py --config "' + $cfgPath + '"'
    $xml = $xml.Replace('__USER__', $user).Replace('__EXE__', $py.Exe).Replace('__ARGS__', $argLine).Replace('__CWD__', $HERE)
    Register-ScheduledTask -TaskName $TASK_NAME -Xml $xml -Force | Out-Null
    $t = Get-AgentTask
    if (-not $t) {
        Bad "task registration failed"
        return
    }
    $a = $t.Actions[0]
    Ok "task '$TASK_NAME' installed"
    Step ("action  : " + $a.Execute + " " + $a.Arguments)
    Step ("start in: " + $a.WorkingDirectory)
    Step ("trigger : logon of " + $user + ", delay 30 s")
    Step ('start it now without a reboot (stop a running agent first): schtasks /Run /TN "' + $TASK_NAME + '"')
}

function Remove-Agent {
    if (Get-AgentTask) {
        Unregister-ScheduledTask -TaskName $TASK_NAME -Confirm:$false
        Ok "task '$TASK_NAME' removed"
    } else {
        Step "agent task: not installed"
    }
}

function Show-Status {
    Write-Host "[status]"
    if (Test-Path -LiteralPath $lnkPath) {
        $wsh = New-Object -ComObject WScript.Shell
        try {
            $sc = $wsh.CreateShortcut($lnkPath)
            $tgt = $sc.TargetPath + " " + $sc.Arguments
        } finally {
            [void][Runtime.InteropServices.Marshal]::ReleaseComObject($wsh)
        }
        Ok "manager shortcut: installed -> $tgt"
    } else {
        Step "manager shortcut: not installed"
    }
    $t = Get-AgentTask
    if ($t) {
        $a = $t.Actions[0]
        Ok ("agent task: installed (state " + $t.State + ") -> " + $a.Execute + " " + $a.Arguments)
    } else {
        Step "agent task: not installed"
    }
    $py = Find-AgentExe
    if ($py) { Step "windowless python: $($py.Exe)" } else { Step "windowless python: NOT FOUND" }
    if (Test-Path -LiteralPath $cfgPath) { Step "config.json: present" } else { Step "config.json: MISSING" }
    if (Test-Tailscale) { Step "tailscale CLI: found" } else { Step "tailscale CLI: NOT FOUND" }
    $live = $false
    $c = New-Object Net.Sockets.TcpClient
    try { $live = $c.ConnectAsync('127.0.0.1', 8766).Wait(500) } catch { }
    try { $c.Close() } catch { }
    if ($live) { Step "manager on 127.0.0.1:8766: listening" } else { Step "manager on 127.0.0.1:8766: not listening" }
}

switch ($Mode) {
    'manager'   { Install-Manager }
    'agent'     { Install-Agent }
    'all'       { Install-Manager; Write-Host ''; Install-Agent }
    'uninstall' { Remove-Manager; Remove-Agent }
    'status'    { Show-Status }
}

Write-Host ''
if ($Mode -eq 'uninstall') {
    Write-Host 'Done - auto-start registrations removed.'
} elseif ($Mode -eq 'status') {
    Write-Host 'Done (report only - nothing was changed).'
} elseif ($fail -ne 0) {
    Write-Host 'Finished WITH ERRORS - see the ! lines above.' -ForegroundColor Red
    exit 1
} else {
    Write-Host 'Done. Test now: double-click start_pc_manager.cmd (or the shortcut'
    Write-Host 'in your Startup folder). Full check: log off and back on.'
}
exit 0
