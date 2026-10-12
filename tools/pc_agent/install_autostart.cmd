@echo off
rem =====================================================================
rem  install_autostart.cmd - one-click logon auto-start for the PC-side
rem  tooling (tools/pc_agent). Just double-click it. No admin rights
rem  needed; everything it creates lives in your user profile and is
rem  fully reversible.
rem
rem  Usage (the argument is optional):
rem    install_autostart.cmd              manager shortcut + agent task
rem    install_autostart.cmd manager      only the manager Startup shortcut
rem    install_autostart.cmd agent        only the pc_agent Scheduled Task
rem    install_autostart.cmd /status      report what is currently installed
rem    install_autostart.cmd /uninstall   remove both
rem
rem  What it creates:
rem    1. "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\
rem       PC Agent Manager.lnk" -> windowless pc_manager (pyw/pythonw;
rem       no window, no browser tab at logon);
rem    2. Task Scheduler task "MicroLink PC Agent": at logon of this user
rem       with a 30 s delay, windowless python (pyw/pythonw) running
rem       pc_agent.py --config "<full path>\config.json".
rem
rem  The 30 s delay lets Tailscale come up first (bind_host "auto" is
rem  resolved once at agent start). The real logic lives in
rem  install_autostart.ps1 next to this file; see README.md
rem  "Auto-start at logon".
rem =====================================================================
setlocal EnableExtensions
cd /d "%~dp0"

if not exist "install_autostart.ps1" (
  echo [ERROR] install_autostart.ps1 is missing next to this file.
  pause
  exit /b 1
)

set "MODE=%~1"
if not defined MODE set "MODE=all"
if "%MODE:~0,1%"=="/" set "MODE=%MODE:~1%"

echo Running installer (mode: %MODE%) ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_autostart.ps1" "%MODE%"
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
  echo [ERROR] installer exited with code %RC% - see the messages above.
  pause
)
rem keep the window up ~5 s on success so the result stays readable
ping -n 6 127.0.0.1 >nul
exit /b %RC%