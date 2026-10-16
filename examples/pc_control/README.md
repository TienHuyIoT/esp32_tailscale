# MicroLink PC Control (ESP32-S3)

Remote PC control from an ESP32-S3 over an existing Tailscale tailnet:
four commands (POWER_ON, WAKE_UP, PWR_RESET, FORCE_POWER_OFF), an
embedded responsive web UI, a versioned JSON API, and a login-protected
security page for the API token and the admin credentials. The device
only pulses the motherboard front-panel inputs - there is no Windows-side
service.

**Safety stance:** every command drives the real motherboard front-panel GPIO
(no simulation mode). The only runtime guards are the per-function Enable flag
+ GPIO mapping in `/pc/settings` and the 5 s cooldown - see
`docs/PC_CONTROLLER.md` for the full security and honesty model.

Each output also has a runtime **active level** (HIGH default; LOW for
low-trigger circuits), selectable per function in `/pc/settings` - see the
GPIO policy section in `docs/PC_CONTROLLER.md`.

This example is derived from `examples/basic_connect` (same WiFi/NVS/
setup-mode boot flow, plus a UDP echo on :9000 kept for regression
testing). `basic_connect` itself is untouched by this example.

## Where things live

```
examples/pc_control/
  main/
    main.c              boot: NVS WiFi -> (setup mode) -> MicroLink -> pc_control
    pc_cmd.[ch]         command IDs and lifecycle states
    pc_state.[ch]       state store: PC state + command history
    pc_hw.h             hardware layer API (PWR_SW/RESET_SW actions)
    pc_hw.c             real GPIO pulses (PWR_SW / RESET_SW)
    pc_relay.[ch]       relay channels (NVS config + commanded ON/OFF state)
    pc_auth.[ch]        admin login (salted SHA-256) + runtime API token
    pc_controller.[ch]  pc_ctl task: queue, dedup, cooldown, timeouts
    pc_api.[ch]         /pc + /api/v1/* handlers (shared port-80 server)
    pc_web.h            embedded ISUI control + settings pages (vanilla JS)
    pc_ui_theme.h       shared ISUI theme (dark-glass CSS + UI helpers)
    pc_security_web.h   embedded login + API-token page
    Kconfig.projbuild   example options (token, timings, HID)
```

## URLs

| URL | What |
|-----|------|
| `http://<vpn-ip>/`    | the original MicroLink config UI (unchanged) |
| `http://<vpn-ip>/pc`  | the PC control UI (this example) |
| `http://<vpn-ip>/pc/settings` | device Settings: per-function enables, GPIO mapping, timings |
| `http://<vpn-ip>/pc/security` | login (admin/admin) + API token management |
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

1. API token (optional first-boot default; rotate it at runtime from
   `/pc/security` after logging in with admin/admin):

```powershell
python -c "import secrets; print(secrets.token_hex(24))"
```

Paste the value into `CONFIG_PCCTL_API_TOKEN` in
`examples/pc_control/sdkconfig.credentials`, or skip this step and
generate one on the Security page. Values apply on the next sdkconfig
regeneration (delete `sdkconfig`, or first build).

2. Function enables and GPIO mapping (runtime, in `/pc/settings`):
   every function starts **disabled with NC pins** and is rejected until
   enabled *and* given an allowed GPIO. `PWR_RESET` must use a different
   pin than the PWR_SW-family functions. Each function also has an
   **active level** (HIGH default; LOW for low-trigger circuits) -
   functions sharing a pin must use the same level.

3. There is no execution mode to choose - the real GPIO layer is always
   active. Verify the interface circuit before enabling a function in
   `/pc/settings`.

## API quick reference

All mutations require `X-PC-Token: <API token>` (or `Authorization:
Bearer`). Full contract: `docs/PC_CONTROLLER.md`.

```
GET  /api/v1/status                     fw/esp32/pc state, mode, hw_cooldown_ms, auth
GET  /api/v1/commands                   four commands + availability
POST /api/v1/commands                   {"command":"POWER_ON","request_id":"...",
                                         "confirm":"POWER_ON"}
GET  /api/v1/commands/{id}              state/result of a request id
GET  /api/v1/config                     effective runtime configuration
POST /api/v1/config                     partial update (settings page)
POST /api/v1/auth/login                 admin login -> session cookie
POST /api/v1/auth/logout                drop the session
GET  /api/v1/auth/status                login + API-token state
POST /api/v1/auth/credentials           change login (session required)
GET  /api/v1/auth/api-token             token state (session required)
POST /api/v1/auth/api-token             generate/set/clear (session required)
POST /api/v1/device/reboot              restart the ESP32 (token required; keeps config)
GET  /api/v1/relays                     relay channels: config + commanded state
POST /api/v1/relays                     set one channel {"channel":0,"state":"on"|"off"}
POST /api/v1/relays/config              update relay channel configuration (Settings)
```

`POST` replies `202 {"state":"accepted"}` - acceptance is not completion;
poll the id or the UI. **Every** command requires the confirmation echo
(`"confirm":"<COMMAND_NAME>"`).

PowerShell testing tip: pass JSON bodies via file to avoid quoting pain:

```powershell
'{"command":"POWER_ON","request_id":"t1"}' | Set-Content -Encoding ascii req.json
curl.exe -s -w '|%{http_code}' -X POST -H "X-PC-Token: <token>" `
    -H 'Content-Type: application/json' -d "@req.json" `
    http://<vpn-ip>/api/v1/commands
```

## Using the UI (first run)

1. Open `http://<vpn-ip>/pc/security`, log in (admin/admin) and generate
   an API token; copy it. Then open `http://<vpn-ip>/pc` and paste it into
   the **API token (this browser)** card (or open
   `http://<vpn-ip>/pc#t=<token>` on phones / private-mode browsers where
   localStorage is blocked - the fragment is used for this session and
   never sent to the device). Change the login away from admin/admin on
   the Security page after the first login.
2. Press a command button:
   - **Power On / Wake Up** - a confirmation dialog appears; on confirm the
     result lands in the Log + "Last command" card.
   - **PWR Reset / Force Power Off** - the confirm button only unlocks
     after typing the exact command name.
   - After a hardware action a short cooldown (`CONFIG_PCCTL_HW_COOLDOWN_MS`,
     default 5 s) applies. The page shows the remaining seconds but does
     **not** lock the buttons: pressing again reports "cooldown active,
     wait N s" in the log instead of being silently blocked.
3. If the page seems stale after a firmware update, hard-refresh
   (Ctrl+F5; on iOS Safari: hold reload -> "Request Desktop Website"
   toggle or clear site data).

## Security summary

- Sensitive operations require the API token; tailnet membership alone is
  not treated as authorization.
- Plain HTTP is acceptable only because all traffic stays inside the
  WireGuard tunnel - do not port-forward the ESP32.
- The runtime API token lives in NVS (set/rotated on `/pc/security`) and
  in the browser's localStorage for the UI; `sdkconfig.credentials` only
  holds the optional first-boot default.
- The admin login (default admin/admin, changeable) is a separate secret
  from the API token; passwords are stored salted + SHA-256 hashed.
- See `docs/PC_CONTROLLER.md` for the full model.

## USB HID login

/pc includes a "USB HID login" panel: it types the Windows password
through the ESP32-S3 native USB port (USB keyboard) into the focused
window of the PC it is plugged into. RAM-only, token-gated, cancellable
inside the ARM window, and it never claims that Windows accepted the
password - only that keystrokes were sent. See docs/PC_CONTROLLER.md
("USB HID login") for the full API contract, security properties, the
US keyboard-layout caveat and the COM9/console notes. Build with
CONFIG_PCCTL_ENABLE_HID=y (+ CONFIG_TINYUSB_HID_COUNT=1; USB-Serial-JTAG
disabled - see sdkconfig.defaults).

## Relay channels

/pc has a "Relays" section (one card per configured channel with explicit
ON/OFF buttons and the commanded state) and /pc/settings has the "Relay
channels" card (channel count, per-channel name / enable / GPIO / active
level; startup is fixed OFF). Saving relay settings writes NVS once and
drives every configured pin to its idle OFF level - a save can never
pulse, and nothing is ever restored ON after a reboot.

Rules enforced in firmware: relay pins come from the board allowlist, are
unique across channels and never overlap the PC-control function pins; a
channel-count reduction that would drop a channel with a GPIO mapping or
the Enable flag is rejected (clear the channel to NC + disabled first);
ON/OFF is rejected for disabled channels and NC pins. Channels can be
deleted (two-step Delete in the /pc Configure modal and on every
/pc/settings row): the following channels shift up and the freed pin
becomes assignable again. Up to 16 channels (CONFIG_PCCTL_RELAY_MAX_CHANNELS). The API reports the
COMMANDED state only - there is no relay contact feedback. See
docs/PC_CONTROLLER.md -> "Relay control" for the full contract and the
relay-module hardware notes (trigger polarity, AC/DC rating, isolation).

## Interface theme (ISUI Space scene)

All three embedded pages share the ISUI dark-glass theme
(`main/pc_ui_theme.h`). The **Space scene** (Ætherium + Galaxy Drift, adapted
from `docs/design-standard/ISUI_Web_Design_Kit_v2/reference/ISUI_SPACE_SCENE_REFERENCE.html`)
adds: a WebGL nebula/starfield backdrop (own fragment shader, no external
libraries), a WebGL prism backdrop + a WebGL power-orb sphere with orbit
rings (raw WebGL1, ported from the reference), the CSS planetary scene
(giant / ringed / moon / distant + streaks), pointer-driven 2D tilt on
desktop, and depth-styled glass cards.

- Space is the default; `/pc` -> Interface Settings (sliders icon) has a
  **"Space scene"** switch to return to the flat **Glass classic** look
  (stored per browser in `localStorage` as `pcctl_ui_scene`). The Scene,
  Glass-blur and Parallax toggles are local preferences - never sent to the
  device.
- Fallbacks: `prefers-reduced-motion` renders static (CSS animations off,
  WebGL drawn once); browsers without WebGL fall back to a lightweight
  2D-canvas starfield; WebGL is paused while the tab is hidden and disposed
  when switching to Glass.
- The reference is a visual guide: no demo behavior was copied - every
  control stays wired to the real APIs.