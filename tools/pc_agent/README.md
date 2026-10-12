# pc_agent - Windows-side PC agent

A minimal, authenticated HTTP service that executes exactly two Windows
operations on request - **graceful shutdown** and **graceful restart**
(via `shutdown.exe`) - plus cancelling a pending operation. Everything
else is rejected. This is the endpoint the ESP32 firmware's SHUTDOWN and
RESTART commands talk to over the tailnet.

## Safety: dry-run by default, and it stays that way unless a human decides otherwise

Two independent config gates must BOTH be flipped before any real
shutdown/restart command is ever executed:

```json
{ "dry_run": true, "allow_real_execution": false }
```

With the defaults (`dry_run: true` or `allow_real_execution: false`),
requests return structured `"dry_run": true` responses and **no Windows
subprocess is spawned**. The `/api/v1/status` response reports the
effective mode so the ESP32 UI can display it.

Enabling real execution is a deliberate, human-only configuration change
for a machine whose state you control (save your work first). The
automated testing for this project never enables it.

## Requirements

- Windows 10/11, Python 3.11+ (tested on 3.13), **stdlib only** (no pip
  installs).
- Tailscale running on the machine.

## Setup

1. Copy the example config and set a strong token:

```powershell
cd tools\pc_agent
Copy-Item config.example.json config.json
python -c "import secrets; print(secrets.token_hex(24))"
# paste the output as "token" in config.json
```

The same token value goes into the firmware as
`CONFIG_PCCTL_WIN_AGENT_TOKEN` (see `python tools/gen_pc_tokens.py`).

2. Allow inbound from the tailnet (run once, as Administrator - adjust
   the port if you changed it). Until this rule exists, Windows Firewall
   may drop the ESP32's connections:

```powershell
netsh advfirewall firewall add rule name="pc_agent tailnet" dir=in ^
  action=allow protocol=TCP localport=8765 remoteip=100.64.0.0/10
```

3. Configuration reference (`config.json`):

| Key | Default | Meaning |
|-----|---------|---------|
| `bind_host` | `"auto"` | `auto` = the host's Tailscale IPv4; falls back to 127.0.0.1. Or set an explicit address. |
| `port` | `8765` | TCP port. |
| `token` | `""` | Bearer token required on every request. |
| `dry_run` | `true` | Simulate shutdown/restart; never touch `shutdown.exe`. |
| `allow_real_execution` | `false` | Second gate for real execution. |
| `allow_no_token` | `false` | Escape hatch for isolated labs. The agent refuses to start with an empty token unless this is `true`. |
| `allow_clients` | `["100.64.0.0/10","127.0.0.1"]` | Source-CIDR allowlist (Tailscale range + localhost). Disallowed sources get `403` and a logged `REJECT` line. |
| `functions` | `{"shutdown": true, "restart": true}` | Server-side per-function enable flags. A disabled function returns `403 function_disabled` regardless of token validity. |
| `default_delay_s` | `60` | Grace period when a request omits `delay_s`. |
| `log_file` | `"pc_agent.log"` | Rotating log (1 MB x 3). Never contains tokens, Authorization headers or request bodies. |

## Running

Manual (development):

```powershell
python pc_agent.py --config config.json
# console shows: listening on <tailscale-ip>:8765 (dry_run=True, ...)
```

Self-test (validates auth comparison, client allowlist and the
subprocess plumbing with harmless commands only):

```powershell
python pc_agent.py --config config.json --self-test
```

Automatic start (after you have validated manual operation): create a
Task Scheduler task -

- Trigger: *At log on* of your user (the Tailscale IP must exist; if the
  task runs before Tailscale is up, the agent falls back to binding
  127.0.0.1 - start it after logon, or add a short delay, to be safe)
- Action: `pythonw.exe` with arguments `pc_agent.py --config "<full path>\config.json"`
- Start in: the `tools\pc_agent` directory

## Endpoints

All requests require `Authorization: Bearer <token>` (the client
allowlist check comes first).

```
GET  /api/v1/status          {"ok":true, "dry_run":..., "pending":null|{...}}
GET  /api/v1/config/status   effective config: port, bind host, allowlist,
                             function flags, dry-run state, token_set (no secrets)
POST /api/v1/shutdown        {"delay_s": 60}  -> 202 {"scheduled":true,...}
POST /api/v1/restart         {"delay_s": 60}  -> 202 {"scheduled":true,...}
POST /api/v1/cancel-shutdown {}               -> 200 {"cancelled":true|false,...}
```

Every request is logged as `<METHOD> <path> client=<ip> -> <status>`
(never tokens or headers) - this is also how you *verify* which source
address the service actually sees for a given client.

Status codes: 400 invalid body/delay (delay must be 5..3600 s), 401
missing/bad token, 403 client CIDR not allowed, 404 unknown path, 409 a
different operation is already pending, 413 body too large, 500
subprocess failure (real mode; pending is cleared).

Duplicate policy: the same operation while one is pending returns
`200 {"already_pending": true, "pending": {...}}`. Pending entries
expire by themselves when their deadline passes.

## PowerShell test matrix (all dry-run; harmless)

Tip: write JSON bodies to files and use `-d "@file"` to avoid quoting
issues in PowerShell.

```powershell
$tok = "<your token>"; $base = "http://<pc-tailscale-ip>:8765"

curl.exe -s -w '|%{http_code}' -H "Authorization: Bearer $tok" "$base/api/v1/status"
curl.exe -s -w '|%{http_code}' "$base/api/v1/status"                                  # 401
curl.exe -s -w '|%{http_code}' -H 'Authorization: Bearer wrong' "$base/api/v1/status" # 401

'{"delay_s":60}' | Set-Content -Encoding ascii t60.json
curl.exe -s -w '|%{http_code}' -X POST -H "Authorization: Bearer $tok" `
  -H 'Content-Type: application/json' -d "@t60.json" "$base/api/v1/shutdown"          # 202
curl.exe -s -w '|%{http_code}' -X POST -H "Authorization: Bearer $tok" `
  -H 'Content-Type: application/json' -d "@t60.json" "$base/api/v1/shutdown"          # 200 already_pending
curl.exe -s -w '|%{http_code}' -X POST -H "Authorization: Bearer $tok" `
  -H 'Content-Type: application/json' -d "@t60.json" "$base/api/v1/restart"           # 409 busy
curl.exe -s -w '|%{http_code}' -X POST -H "Authorization: Bearer $tok" `
  "$base/api/v1/cancel-shutdown"                                                      # 200 cancelled
curl.exe -s -w '|%{http_code}' -X POST -H "Authorization: Bearer $tok" `
  "$base/api/v1/cancel-shutdown"                                                      # 200 none_pending
```

## Token rotation

Change `"token"` in `config.json`, restart the agent, and update
`CONFIG_PCCTL_WIN_AGENT_TOKEN` in the firmware credentials
(`python tools/gen_pc_tokens.py` rotates both sides at once; delete
`examples/pc_control/sdkconfig` and rebuild/reflash to apply).

## Manager UI (pc_manager.py) - local-only configuration

```powershell
python pc_manager.py --port 8766        # binds 127.0.0.1 ONLY
# open http://127.0.0.1:8766
```

Smoke-test a running manager with `python e2e_check.py`: it checks the
page, verifies the served `<script>` actually PARSES (`node --check`, if
node is on PATH - this catches Python-escape bugs that silently kill all
UI JavaScript), calls `/api/local/status` and `/api/local/test`, and with
`--start` also exercises the same service-start endpoint the Start button
uses. Run it after any edit to `pc_manager.py`.

The manager is a separate helper process that edits the same
`config.json` and controls the service lifecycle:

- **Detects** the PC Tailscale IPv4 (`tailscale ip -4`) and shows it.
- **Edits** the service port, bind address, allowlist and the
  server-side `functions` flags; writes config.json atomically and
  ACL-hardens it (`icacls` best-effort), then asks you to restart.
- **Token**: generates a new random bearer token, shown **exactly once**
  (copy it into the ESP32 Settings UI, then Test); afterwards only a
  masked form (`abcd...ef12`) is displayed. Rotating invalidates the old
  token as soon as the service restarts.
- **Service control**: Start / Stop / Restart of `pc_agent.py` (via a
  pidfile + `taskkill`; it verifies the pid is really a pc_agent before
  killing). If the agent was started manually, the manager **adopts**
  the running process instead of spawning a duplicate.
- **Test**: probes `/api/v1/status` with the stored token and reports
  reachable vs authenticated (HTTP 401/403 shown explicitly); once
  authenticated it also shows the agent's live runtime mode
  (`runtime: REAL` vs `runtime: DRY-RUN`).
- **Stale-page guard**: if the page cannot reach the manager API (manager
  window closed, PC rebooted, ...) it shows a red **MANAGER API
  UNREACHABLE** card and resets the badges to `?` instead of leaving
  values that look live but are stale. The manager refuses to start when
  `127.0.0.1:8766` is already answering, picks up hand-edits to
  `config.json` live (mtime watch), and appends logs to `manager.log`
  next to the script.
- **Not editable here, by design**: `dry_run` and
  `allow_real_execution`. Those two gates are human-edited config-file
  changes only; the UI intentionally cannot unlock them.

The manager itself never executes Windows operations and never logs
token values.

### One-click launcher (Windows)

Double-click **`start_pc_manager.cmd`** (or a desktop shortcut to it):

1. kills any stale `pc_manager.py` instances first - this prevents two
   managers double-binding `127.0.0.1:8766` (Windows `SO_REUSEADDR`) and
   answering the UI from config one of them loaded long ago;
2. starts one fresh manager in its own **"PC Agent Manager"** window
   (closing that window, or Ctrl+C in it, stops the manager);
3. waits for port 8766, then opens `http://127.0.0.1:8766/` in your
   default browser.

It does **not** start the `pc_agent` service - press **Start** in the UI
when you want the agent up. Tip: right-click the file, then
*Send to -> Desktop (create shortcut)* for a true one-click icon.

### Auto-start at logon (optional)

Double-click **`install_autostart.cmd`** to register the PC-side tooling
so it comes back by itself after a reboot or a logon. **Per-user only,
no admin rights**, fully reversible:

```cmd
install_autostart.cmd            rem manager shortcut + pc_agent task (= "all")
install_autostart.cmd manager    rem only the manager Startup shortcut
install_autostart.cmd agent      rem only the pc_agent Scheduled Task
install_autostart.cmd /status    rem report what is currently installed
install_autostart.cmd /uninstall rem remove both
```

What it creates:

- **Manager**: a Startup-folder shortcut `PC Agent Manager.lnk`
  (`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\`) running
  `start_pc_manager.cmd` minimized - at logon you get the same fresh
  manager window + browser tab as double-clicking the launcher.
- **pc_agent**: a Task Scheduler task `MicroLink PC Agent` - trigger
  *At log on* of the current user with a **30 s delay**; action is the
  windowless launcher (`pyw -3` / `pythonw`) running
  `pc_agent.py --config "<repo>\tools\pc_agent\config.json"`, working
  directory `tools\pc_agent` so a relative `log_file` still lands next
  to the scripts. The delay lets Tailscale come up first, because
  `bind_host: "auto"` is resolved once at agent start - binding too
  early would silently fall back to `127.0.0.1` (see "Automatic start"
  above).

Notes:

- No restart-on-failure, on purpose: **Stop** in the manager UI means
  stopped until you press Start again (or the next logon).
- Only the user who ran the installer is affected - a multi-user PC
  needs this run once per user.
- It never touches `dry_run` / `allow_real_execution` - starting the
  process does not arm real execution.
- Test without a reboot: stop the agent first if the manager shows it
  running (the task cannot detect an agent started outside it), then
  `schtasks /Run /TN "MicroLink PC Agent"`; confirm `pc_agent.log`
  shows the bind line for the Tailscale IP. The full check is a log
  off / log on.
- Inspect any time with `install_autostart.cmd /status`; remove with
  `install_autostart.cmd /uninstall` (or delete the shortcut / task in
  the Windows UIs).

## Limitations (documented, intentional)

- `POST /api/v1/cancel-shutdown` runs `shutdown /a`, which aborts **any**
  pending system shutdown/restart - including one started outside this
  agent. That is inherent to the Windows mechanism used.
- After a *real* shutdown/restart is scheduled, the agent cannot confirm
  the PC powered off - the agent goes away with Windows. The ESP32 UI
  reports "agent unreachable after shutdown request - PC state unknown"
  rather than claiming the PC is off.
- The agent executes only `shutdown.exe` with the arguments you see in
  `pc_agent.py`; there is no shell and no arbitrary command execution.

