# MicroLink PC Control (ESP32-S3)

Remote PC control from an ESP32-S3 over an existing Tailscale tailnet:
six commands (POWER_ON, WAKE_UP, SHUTDOWN, RESTART, PWR_RESET,
FORCE_POWER_OFF), an embedded responsive web UI, a versioned JSON API,
and a separate Windows-side agent (`tools/pc_agent`) for graceful
shutdown/restart.

**Safety stance:** the firmware ships in `MOCK_ALL` mode (everything
simulated, no GPIO, no agent contact). The Windows agent ships in
`dry_run` mode (no real shutdown/restart subprocess is ever spawned).
Both must be *deliberately* reconfigured by a human - see
`docs/PC_CONTROLLER.md` for the full security and honesty model.

Each hardware output also has a runtime **active level** (HIGH default;
LOW for low-trigger circuits), selectable per function in
`/pc/settings` - see the GPIO policy section in `docs/PC_CONTROLLER.md`.

This example is derived from `examples/basic_connect` (same WiFi/NVS/
setup-mode boot flow, plus a UDP echo on :9000 kept for regression
testing). `basic_connect` itself is untouched by this example.

## Where things live

```
examples/pc_control/
  main/
    main.c              boot: NVS WiFi -> (setup mode) -> MicroLink -> pc_control
    pc_cmd.[ch]         command IDs, routes, lifecycle states
    pc_state.[ch]       state store: service reachability, PC state, history
    pc_hw.h             hardware abstraction (PWR_SW/RESET_SW actions)
    pc_hw_mock.c        mock hardware: simulated actions only (default)
    pc_hw_gpio.c        real GPIO pulses (compile-gated, OFF by default)
    pc_win_client.[ch]  HTTP client -> Windows agent, over the WG tunnel
    pc_controller.[ch]  pc_ctl task: queue, routing, dedup, timeouts, polling
    pc_api.[ch]         /pc UI + /api/v1/* handlers (shared port-80 server)
    pc_web.h            embedded UI (single HTML page, vanilla JS)
    Kconfig.projbuild   example options (tokens, modes, timings, GPIO)
```

## URLs

| URL | What |
|-----|------|
| `http://<vpn-ip>/`    | the original MicroLink config UI (unchanged) |
| `http://<vpn-ip>/pc`  | the PC control UI (this example) |
| `http://<vpn-ip>/pc/settings` | device Settings: PC host/port, agent token, per-function enables, GPIO mapping, timings, connection test |
| `http://192.168.4.1/` | setup mode (SoftAP) UI when no WiFi is reachable |

## Build / flash / monitor (COM9)

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
cd examples/pc_control
idf.py build
idf.py -p COM9 flash monitor        # never erase flash: NVS holds the
                                    # provisioned WiFi + Tailnet identity
```

Non-interactive serial capture (for logs while away from the keyboard):

```powershell
$env:PYTHONIOENCODING='utf-8:replace'
python ..\..\tools\serial_capture.py COM9 40 --reset |
    Out-File -Encoding utf8 build\boot.log
```

## First-time configuration

1. Tokens (gitignored credentials file):

```powershell
python tools\gen_pc_tokens.py     # from repo root; prints both tokens
```

This writes `examples/pc_control/sdkconfig.credentials` with fresh
`CONFIG_PCCTL_API_TOKEN` (browser UI -> ESP32) and
`CONFIG_PCCTL_WIN_AGENT_TOKEN` (ESP32 -> agent). Copy the same agent
token into `tools/pc_agent/config.json` (or re-run the helper and paste).
Values apply on the next sdkconfig regeneration (delete `sdkconfig`,
or first build).

2. Windows agent host/port (runtime defaults; edit live in
   `/pc/settings`):

- `CONFIG_PCCTL_WIN_HOST="<PC_HOSTNAME>"` - tailnet hostname or plain VPN IP (empty by default; set it in /pc/settings)
- `CONFIG_PCCTL_WIN_PORT=8765` (Kconfig seeds the NVS default; the live
  value is what the Settings UI saved)

3. Function enables and GPIO mapping (runtime, in `/pc/settings`):
   `SHUTDOWN`/`RESTART` start enabled; the four hardware functions start
   **disabled with NC pins** and are rejected until enabled *and* given
   an allowed GPIO. `PWR_RESET` must use a different pin than the
   PWR_SW-family functions. Each function also has an **active level**
   (HIGH default; LOW for low-trigger circuits) - functions sharing a
   pin must use the same level.

4. Execution mode (`idf.py menuconfig` -> Example Configuration):

| Mode | Hardware commands | SHUTDOWN / RESTART |
|------|-------------------|--------------------|
| `MOCK_ALL` (default) | simulated | simulated in firmware |
| `MIXED`  | simulated | sent to the Windows agent |
| `FULL`   | real (requires `PCCTL_ENABLE_HW_GPIO=y` + verified circuit) | sent to the agent |

## API quick reference

All mutations require `X-PC-Token: <CONFIG_PCCTL_API_TOKEN>` (or
`Authorization: Bearer`). Full contract: `docs/PC_CONTROLLER.md`.

```
GET  /api/v1/status                     fw/esp32/service/pc state, mode, auth info
GET  /api/v1/commands                   six commands + availability
POST /api/v1/commands                   {"command":"SHUTDOWN","request_id":"..."}
GET  /api/v1/commands/{id}              state/result of a request id
POST /api/v1/commands/{id}/cancel       honest cancellation (409 if too late)
```

`POST` replies `202 {"state":"accepted"}` - acceptance is not completion;
poll the id or the UI. Confirmation is required for `PWR_RESET` and
`FORCE_POWER_OFF` (`"confirm":"<COMMAND_NAME>"`).

PowerShell testing tip: pass JSON bodies via file to avoid quoting pain:

```powershell
'{"command":"POWER_ON","request_id":"t1"}' | Set-Content -Encoding ascii req.json
curl.exe -s -w '|%{http_code}' -X POST -H "X-PC-Token: <token>" `
    -H 'Content-Type: application/json' -d "@req.json" `
    http://<vpn-ip>/api/v1/commands
```

## Using the UI (first run)

1. Open `http://<vpn-ip>/pc` on a laptop or phone on the tailnet.
2. If the yellow banner appears, press **Set token** and paste the token
   value from `sdkconfig.credentials`. You can paste it *with* the
   surrounding quotes or the whole `CONFIG_PCCTL_API_TOKEN=...` line -
   the UI strips those automatically. Press **Save**: the dialog now
   verifies the token against the device (via the `PING` probe) and shows
   green "Token accepted and saved" (or red "Device REJECTED this token"
   if the value has a typo). Shortcut for phones / private-mode browsers
   where localStorage is blocked: open `http://<vpn-ip>/pc#t=<token>`
   directly - the fragment is used for this session and never sent to
   the device.
3. Press a command button:
   - **Power On / Wake Up** - result appears in the Log + "Last command"
     card (in MOCK mode: `succeeded (simulated)`, ~2 s for Power On).
   - **Shut Down / Restart** - confirmation dialog, then the request goes
     to the Windows agent (dry-run by default): the result detail says
     `agent accepted ... (dry-run: nothing real was scheduled)`.
   - **PWR Reset / Force Power Off** - the confirm button only unlocks
     after typing the exact command name; in MOCK mode the result is
     simulated and clearly labelled.
   - Hardware commands have a short cooldown (`CONFIG_PCCTL_HW_COOLDOWN_MS`,
     default 5 s): after a hardware action the hardware buttons show the
     remaining seconds and unlock automatically - the page polls every
     second while a command runs or the cooldown counts down.
4. If the page seems stale after a firmware update, hard-refresh
   (Ctrl+F5; on iOS Safari: hold reload -> "Request Desktop Website"
   toggle or clear site data).

## Security summary

- Sensitive operations require the API token; tailnet membership alone is
  not treated as authorization.
- Plain HTTP is acceptable only because all traffic stays inside the
  WireGuard tunnel - do not port-forward the ESP32.
- The token lives in `sdkconfig.credentials` / `config.json` (both
  gitignored) and in the browser's localStorage for the UI.
- See `docs/PC_CONTROLLER.md` for the full model, and
  `tools/pc_agent/README.md` for the Windows-side firewall/token setup.
