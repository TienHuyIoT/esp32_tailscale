@echo off
rem =====================================================================
rem  start_pc_manager.cmd - one-click launcher for the PC Agent Manager
rem  (tools/pc_agent). Just double-click this file.
rem
rem  Modes:
rem    start_pc_manager.cmd          manager window + browser tab
rem    start_pc_manager.cmd quiet    windowless manager (pyw/pythonw),
rem                                  no browser tab; open the UI yourself
rem                                  at http://127.0.0.1:8766
rem
rem  What it does:
rem    1. kills any stale pc_manager.py instances (and their console
rem       windows) first - this prevents two managers double-binding
rem       127.0.0.1:8766 (Windows SO_REUSEADDR) and answering the UI
rem       from stale in-RAM config;
rem    2. starts ONE fresh manager (visible window, or windowless with
rem       the quiet mode);
rem    3. waits for the port; opens the UI in your browser (the quiet
rem       mode skips the browser and prints how to stop the manager).
rem
rem  It does NOT start the pc_agent service - use the Start button in
rem  the UI. The logon auto-start (install_autostart.cmd) uses the
rem  windowless form of this script.
rem =====================================================================
setlocal EnableExtensions
cd /d "%~dp0"

set "QMODE="
set "ARG1=%~1"
if /i "%ARG1%"=="quiet" set "QMODE=1"
if /i "%ARG1%"=="/quiet" set "QMODE=1"

echo [1/3] Cleanup: stopping stale pc_manager instances (if any) ...
powershell -NoProfile -Command "$p = @(Get-CimInstance Win32_Process | Where-Object { (($_.Name -eq 'python.exe' -or $_.Name -eq 'pythonw.exe') -and $_.CommandLine -match 'pc_manager') -or ($_.Name -eq 'cmd.exe' -and $_.CommandLine -match 'pc_manager\.py') }); if ($p.Count -eq 0) { Write-Host '      - none running' } else { $p | ForEach-Object { Write-Host ('      - stopped PID ' + $_.ProcessId + ' (' + $_.Name + ')'); Stop-Process -Id $_.ProcessId -Force } }"

if defined QMODE goto :start_quiet

rem --- pick a Python 3 interpreter (py launcher first, then python) ---
set "PYCMD="
where py >nul 2>&1 && set "PYCMD=py -3"
if not defined PYCMD (where python >nul 2>&1 && set "PYCMD=python")
if not defined PYCMD (
  echo [ERROR] No Python 3 launcher found on PATH ^(tried: py -3, python^).
  pause
  exit /b 1
)

echo [2/3] Starting pc_manager in a new window ^(interpreter: %PYCMD%^) ...
start "PC Agent Manager" cmd /k "%PYCMD% pc_manager.py --port 8766"
goto :started

:start_quiet
rem --- windowless Python: pyw first, then pythonw ---
set "PYQUIET="
where pyw >nul 2>&1 && set "PYQUIET=pyw -3"
if not defined PYQUIET (where pythonw >nul 2>&1 && set "PYQUIET=pythonw")
if not defined PYQUIET (
  echo [ERROR] No windowless Python found on PATH ^(tried: pyw, pythonw^).
  pause
  exit /b 1
)
echo [2/3] Starting pc_manager windowless ^(interpreter: %PYQUIET%^) ...
start "" %PYQUIET% pc_manager.py --port 8766

:started

echo [3/3] Waiting for http://127.0.0.1:8766 ...
set /a tries=0
:wait
set /a tries+=1
powershell -NoProfile -Command "try { $c = New-Object Net.Sockets.TcpClient('127.0.0.1',8766); $c.Close(); exit 0 } catch { exit 1 }" >nul 2>&1
if not errorlevel 1 goto :open
if %tries% geq 20 (
  echo [ERROR] Manager did not answer on port 8766 within 20 s.
  echo         Check manager.log / console_err.log next to the scripts.
  pause
  exit /b 1
)
ping -n 2 127.0.0.1 >nul
goto :wait

:open
if defined QMODE goto :open_quiet
echo       - manager is up. Opening the UI in your browser...
start "" "http://127.0.0.1:8766/"
echo.
echo Done. The manager keeps running in the "PC Agent Manager" window.
echo Close that window (or press Ctrl+C in it) to stop the manager.
echo NOTE: the pc_agent service is NOT started - press Start in the UI.
ping -n 6 127.0.0.1 >nul
exit /b 0

:open_quiet
echo       - manager is up (windowless). No browser tab was opened.
echo.
echo Open http://127.0.0.1:8766 yourself when you need the UI.
echo To stop it: re-run start_pc_manager.cmd (visible window) - it stops
echo the windowless instance first - or end pythonw.exe in Task Manager.
echo NOTE: the pc_agent service is NOT started - press Start in the UI.
ping -n 6 127.0.0.1 >nul
exit /b 0
