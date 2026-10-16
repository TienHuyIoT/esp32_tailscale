# PC Controller (ESP32-S3 + Tailscale)

Status: implemented and hardware-tested on a real tailnet (ESP32-S3,
16 MB flash, COM9). Firmware: `examples/pc_control`. Every command drives the
real motherboard front-panel GPIO - there is no simulation mode.

> The former Windows-side agent (`tools/pc_agent`) and the `SHUTDOWN` /
> `RESTART` software commands were **removed**. The device now only pulses
> the motherboard front-panel inputs; there is no Windows service, no
> `shutdown.exe` and no logon auto-start to install.

## What this is

Four PC-control commands driven from an ESP32-S3 over an existing Tailscale
tailnet, with an embedded responsive web UI and a versioned JSON API:

- `POWER_ON`         - pulse PWR_SW to start a PC that is off
- `WAKE_UP`          - wake a sleeping PC (short PWR_SW press)
- `PWR_RESET`        - pulse RESET_SW (hardware reset; confirmation required)
- `FORCE_POWER_OFF`  - long PWR_SW hold for a frozen PC (strong confirmation
  required)

Every command drives the front-panel inputs through the real GPIO layer
(`pc_hw.c`). All four require a confirmation (`confirm` must echo the command
name).

## System diagram

```
     laptop / phone (any tailnet device)
              |
              |  HTTP through the WireGuard tunnel
              v
  ESP32-S3  <ESP32_VPN_IP>
  +----------------------------+
  | microlink core (unchanged) |
  | port-80 esp_http_server:   |
  |   /            config UI   |
  |   /pc          PC-control  |
  |   /pc/settings settings    |
  |   /pc/security login+token |
  |   /api/v1/*    PC API      |
  | pc_ctl task:               |
  |   -> pc_hw (real GPIO)     |
  +-------------|--------------+
                |  GPIO (PWR_SW / RESET_SW)
                v
        motherboard PWR_SW / RESET_SW front-panel header
```

## Safety and honesty rules (enforced in code, verified on hardware)

- The GPIO hardware layer is always compiled in, but every function still
  needs to be **enabled and GPIO-mapped** in the device Settings UI - that is
  the only runtime guard left (plus the cooldown).
- `succeeded` means exactly one thing: "the hardware action was issued" -
  never a fabricated physical confirmation.
- The device has **no power-sensing input**, so PC power state is reported
  as `unknown` unless something actually observed it (`pc.source` labels
  how any non-unknown claim was obtained).
- `FORCE_POWER_OFF` is only ever executed when explicitly requested;
  nothing in the system invokes it automatically.
- Duplicate-request protection: one active command at a time (409 with the
  active id), idempotent replay by `request_id`, and a hardware cooldown
  window (`PCCTL_HW_COOLDOWN_MS`, default 5 s) that rejects repeat hardware
  commands even with fresh ids. The remaining cooldown is reported as
  `hw_cooldown_ms` in `/api/v1/status`; the control page shows it and keeps
  polling, but it never locks the buttons - pressing again simply reports
  the remaining time.

## Repository layout (this feature)

| Path | Purpose |
|------|---------|
| `examples/pc_control/` | firmware example (see its README) |
| `examples/pc_control/main/pc_*.c/h` | firmware modules (command defs, state store, real-GPIO hw layer, relay manager, auth/login, controller task, API, embedded UIs) |
| `tools/serial_capture.py` | non-interactive COM-port log capture for flash cycles |
| `docs/PC_CONTROLLER.md` | this document |

## Changes to the MicroLink component (fork-local, additive only)

| File | Change | Why |
|------|--------|-----|
| `components/microlink/include/ml_config_httpd.h` | added `struct httpd_data *ml_config_httpd_get_handle(ctx)` (+stub) | exposes the running port-80 `esp_http_server` handle so sibling modules can register routes on the same server instead of starting a second one. The opaque `struct httpd_data *` spelling keeps `esp_http_server.h` out of the header. |
| `components/microlink/src/ml_config_httpd.c` | getter implementation; `max_uri_handlers` 16 -> 40 | config routes + pc_control routes (incl. relay) + margin. |

Nothing else in the component was modified; `examples/basic_connect`
and the other examples are untouched.

## Firmware architecture

One extra FreeRTOS task joins the four microlink tasks:

| Task | Core | Prio | Stack | Role |
|------|------|------|-------|------|
| `pc_ctl` | any | 5 | 8 KB | drains the command queue (non-blocking), executes hardware actions, supervises deadlines |

State is shared only through the mutex-protected store in `pc_state.c`.
HTTP handlers never block on the (blocking) hardware pulse: they validate,
submit to the `pc_ctl` queue (depth 4) and return `202` immediately.
Multi-KB state snapshots are `static` (safe: one `pc_ctl` task; one
serialized httpd task) to keep the task stacks small - this was found the
hard way during bring-up (a snapshot-stack overflow crash-loop was
reproduced and fixed).

### Hardware layer

There is a single implementation: `pc_hw.c` drives the configured GPIOs with
the runtime pin mapping, active level and pulse timings. A non-OK result from
`pc_hw_init()` makes every command report `503 unavailable`.

## Runtime configuration (NVS) and the device Settings UI

Per-function Enable flags, GPIO mapping (pin + active level) and press
timings are **runtime settings** persisted in NVS (namespace `pc_ctl`,
versioned `hw` blob) - Kconfig values are only first-boot defaults. They
are edited at `http://<vpn-ip>/pc/settings` (linked from the control page)
via `GET`/`POST /api/v1/config`.

- `POST /api/v1/config` validates everything first and persists once - a
  failed save changes nothing.
- Defaults follow the safety stance: every function is **disabled** with
  no GPIO mapping. A hardware function needs Enable **and** a mapped GPIO
  (`403 function_disabled` / `403 gpio_not_configured` otherwise).
- Settings survive reboots (e.g. restored `funcs=0xf gpio=[13,13,14,13]
  lvl=[1,1,1,1] short=500 long=5000`), and a reboot can never enable real
  execution.

### Login and API token (`/pc/security`)

The Security page is the only place that manages the API token and the
admin login. It is protected by a username/password login (default
**admin / admin**, changeable from the page):

- `POST /api/v1/auth/login` verifies the credentials and sets a RAM-only
  session cookie (`pcctl_sess`, HttpOnly, SameSite=Strict, 30 min sliding
  expiry, gone on reboot). `POST /api/v1/auth/logout` drops it.
- `POST /api/v1/auth/credentials` changes the username and/or password.
  The current password must be supplied; the new password is stored salted
  (16-byte random salt) and hashed with SHA-256 - the plaintext is never
  stored, echoed or logged.
- `GET`/`POST /api/v1/auth/api-token` reads/sets the runtime API token
  (generate a random one, set a custom one, or clear it). The value is
  returned **once**, at generation/set time.

The API token gates every other mutation (`X-PC-Token` or
`Authorization: Bearer`). The **runtime** token lives in NVS and overrides
the compile-time `CONFIG_PCCTL_API_TOKEN`, which is only the first-boot
default (clearing the runtime token falls back to it). If neither is set,
mutations are rejected with `403 api_token_not_configured` unless
`CONFIG_PCCTL_ALLOW_NO_TOKEN` is explicitly enabled.

NVS is written only on an explicit user action (a credential change or an
API-token generate/set/clear) - never while fields are being edited.


### GPIO policy (verified pin set for this device)

This device is an ESP32-S3 rev v0.2 with 8 MB **octal** PSRAM, 16 MB
flash and a UART0 console on an external USB-UART bridge. The allowed
output pins (also returned by `GET /api/v1/config` as `allowed_gpios`):

`1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 47, 48`

Excluded, with reasons: 0/3/45/46 (strapping), 19/20 (USB-JTAG), 26-32
(SPI flash), 33-37 (octal PSRAM), 38-42 (JTAG), 43/44 (UART0 console).
Validation is enforced in firmware: unknown pins are rejected, and
`PWR_RESET` may never share a pin with a PWR_SW-family function
(`POWER_ON`/`WAKE_UP`/`FORCE_POWER_OFF` may share one PWR_SW output).
Timings are runtime settings with documented bounds (short press
100-2000 ms, default 300; long press 1000-15000 ms, default 5000) - verify
them on the real circuit.

Each hardware function also has an **active level** (runtime setting,
default HIGH): the GPIO level that "presses" the front-panel input; idle
is the opposite level.

- **Active HIGH** (default): drive the line HIGH to press - suits the
  classic transistor/optocoupler stage into an active-low
  PWR_SW#/RESET_SW# input. Idle LOW.
- **Active LOW**: drive the line LOW to press - for low-trigger circuits
  (opto modules with inverted input, PNP stages). Idle HIGH.

Rules enforced in firmware:

- Functions **sharing a pin must use the same active level** (`409
  gpio_conflict` otherwise) - the level belongs to the interface circuit,
  not to the function.
- The pulse is glitch-free by construction: the output latch is
  pre-loaded with the **idle** level *before* the output driver is
  enabled, so switching direction can never emit a false press.
- The external pull resistor in the interface circuit must match the
  chosen polarity: pulldown for active-HIGH, pullup for active-LOW, so a
  floating pin (boot, reset, ESP32 purely powered off) cannot press.
- Blobs written by an older firmware (NVS `"hw"` v1, no levels) are
  migrated in RAM on first boot - levels default to HIGH (= v1 behavior)
  and the v2 layout is persisted on the next explicit Settings save.

## Command lifecycle and duplicate protection

`accepted -> running -> succeeded | failed | timeout | cancelled`

- Submission (`POST /api/v1/commands`) validates the command name, the
  `request_id` charset (`[A-Za-z0-9_-]{1,32}`), the `confirm` echo and
  (for `FORCE_POWER_OFF`) an optional `hold_ms` in 1000..15000.
- A `request_id` that already exists replays idempotently:
  `200 {"replay":true,"command":{...}}` - no second execution.
  Auto-generated ids look like `c-<boot hex><random>`.
- One active command at a time: a different id while busy is rejected
  (`409 busy` with the active id/command/state).
- Hardware cooldown (`PCCTL_HW_COOLDOWN_MS`, default 5 s) between
  consecutive hardware commands even with different ids (`409 cooldown` +
  `retry_after_ms`). The UI mirrors it: it shows the remaining seconds but
  keeps the buttons usable, so a press during the window reports "wait N s"
  instead of being silently blocked.
- A command that outlives its deadline is marked `timeout` by the `pc_ctl`
  supervisor.

## PC state model

The device has no power-sensing input, so `pc.state` is `unknown` unless a
hardware action just ran; `pc.source` (`none` / `hardware`) and `pc.note`
always label how the state was obtained.

## ESP32 API contract (port 80, `/api/v1/`)

    GET  /pc                          control UI
    GET  /pc/settings                 settings UI
    GET  /pc/security                 login + API-token UI
    GET  /api/v1/status               firmware + PC state, hw_cooldown_ms, auth state
    GET  /api/v1/commands             command list + availability
    POST /api/v1/commands             submit (token required)
    GET  /api/v1/commands/{id}        command state
    GET  /api/v1/config               effective runtime configuration
    POST /api/v1/config               partial update (token required)
    POST /api/v1/hid/login            type the password over USB HID (token)
    POST /api/v1/hid/cancel           cancel the pre-typing ARM window (token)
    POST /api/v1/auth/login           admin login -> session cookie
    POST /api/v1/auth/logout          drop the session
    GET  /api/v1/auth/status          login + API-token state
    POST /api/v1/auth/credentials     change login (session required)
    GET  /api/v1/auth/api-token       API-token state (session required)
    POST /api/v1/auth/api-token       generate/set/clear the token (session)
    POST /api/v1/device/reboot        restart the ESP32 (token required; keeps
                                      all configuration)

    POST /api/v1/commands  {"command":"POWER_ON","request_id":"...","confirm":"POWER_ON"}
         202 {"ok":true,"request_id":"...","state":"accepted"}
         400 missing_command | unknown_command | invalid_request_id |
             confirmation_required | invalid_params | invalid_json
         401 missing_token | invalid_token
         403 api_token_not_configured | function_disabled |
             gpio_not_configured
         409 busy | cooldown
         503 unavailable (route unusable: GPIO not usable)


## Security model

- The HTTP server is plain HTTP, acceptable only because traffic stays
  inside the WireGuard tunnel; **tailnet membership alone is not treated
  as authorization** - every mutation needs the API token.
- The API token is compared in constant time and is never logged or
  echoed. It can be rotated at runtime from `/pc/security`.
- The admin login is a separate secret from the API token; only the
  login can change the token or the credentials. Passwords are stored
  salted + SHA-256 hashed (never in plaintext) and the login form is
  explicit-submit with the field cleared immediately.
- Sessions are RAM-only, cookie-scoped to the device, HttpOnly +
  SameSite=Strict, and expire after 30 minutes of inactivity (and on
  reboot).
- The USB HID password is RAM-only and zeroized after use (see below).
- Secrets: `sdkconfig.credentials` (gitignored) holds only the first-boot
  API token default.

## Operations

### Build / flash / monitor

```bash
cd examples/pc_control
idf.py build
idf.py -p COM9 flash monitor
```

### Provisioning state

WiFi credentials and the device identity live in NVS and survive normal
flashing - do not erase the flash unless a full reprovision is intended.

### GPIO safety

1. Verify the interface circuit (transistor / optocoupler / isolated relay)
   with a meter/scope before enabling any function in `/pc/settings`.
2. Enable and map one function at a time and test single actions (the
   cooldown protects against repeats).
3. Boot/reset safety: outputs are driven to idle as the controller starts -
   pins are only ever driven on demand inside `pc_hw_execute()`.

## Testing (as executed during bring-up)

| Area | Method | Result |
|------|--------|--------|
| Real GPIO | commands with a mapped pin | `accepted -> running -> succeeded` with the pulse detail |
| Duplicate protection | repeat `request_id`; different id while busy | replay 200; busy 409 with the active id |
| Cooldown | two hardware commands < 5 s apart | `409 cooldown` + `retry_after_ms`; UI reports the remaining time |
| NC guard | command with the GPIO set to NC | `403 gpio_not_configured`, nothing driven |
| Settings persistence | save + reboot | values restored |
| Regression | UDP echo :9000 kept; config UI `/` and `/api/status` untouched; NVS identity retained across flashes | operational |
| Security page | login admin/admin, generate a token, use it on `/pc`, change the password | token gates mutations; wrong password rejected |

Deliberately **not** performed: energizing a real motherboard front panel.

## Known limitations and watch items

- PC power state is unobservable without an added sense input, so it is
  reported as `unknown`.
- Wake-on-LAN over Tailscale cannot work on its own (a sleeping PC's
  `tailscaled` is down); it would need a LAN-side helper. Implement behind
  a config flag and report only "packet sent - outcome unverified".

## Hardware interface (required)

`pc_hw.c` drives the configured GPIOs directly, so the ESP32 must sit behind
an isolated interface stage - transistor / optocoupler / isolated relay -
into the motherboard front-panel header. Never connect a GPIO straight to
`PWR_SW#` / `RESET_SW#`, and add the pull resistor that matches the configured
active level so a floating pin cannot press.


## USB HID login (native USB port)

The ESP32-S3 can act as a USB keyboard and type the Windows password into
the focused window (typically the login screen). The password is always
entered explicitly per request and is never stored.

Flow: browser (/pc, "USB HID login" panel) -> Tailscale/WireGuard -> ESP32
HTTP API (POST /api/v1/hid/login, token-gated like every mutation) -> RAM
buffer -> TinyUSB HID keyboard (native USB port, GPIO19/20) -> focused
window on the PC.

### Security properties

- The password lives only in a small RAM buffer (max PCCTL_HID_MAX_LEN,
  default 64 chars). It is zeroized right after typing, cancellation or
  error; the HTTP request body and the parsed JSON copy are wiped
  immediately after the hand-off to the HID module.
- Never written to NVS, files or backups; never logged; never echoed in
  any response (validation failures report only the 0-based position of an
  unsupported character); never put in a URL.
- The browser field is type=password with autocomplete=new-password,
  cleared immediately on submit and never persisted (no localStorage).
- POST /api/v1/hid/login and POST /api/v1/hid/cancel require the same
  X-PC-Token / Authorization: Bearer as every other mutation.
- A submission first sits in an ARM window (PCCTL_HID_ARM_DELAY_MS,
  default 1000 ms) during which it can be cancelled; typing then takes
  len * 2 * PCCTL_HID_KEY_DELAY_MS (default 30 ms) ms. Concurrent
  submissions are rejected with 409 while one is queued or typing.
- The API reports only that keystrokes were sent - it cannot know whether
  Windows accepted the password (screen state, keyboard layout and other
  authentication factors are not observable from here).

### API

    GET  /api/v1/status   -> "hid": {enabled, mounted, state, busy, max_len,
                             state_age_s, last_error}
                             state: idle|queued|typing|done|cancelled|error
    POST /api/v1/hid/login   {"password": "...", "press_enter": true}
         202 {"ok":true,"state":"queued"}
         400 unsupported_char | too_long | invalid_json | invalid_body
         401 missing_token | invalid_token
         403 hid_disabled    (feature compiled out)
         409 busy            (another job queued or typing)
         503 not_mounted     (native USB port not enumerated by a host)
    POST /api/v1/hid/cancel  (cancels inside the ARM window only)
         200 {"cancelled":true} / 409 {"cancelled":false,state:...}

### Build / hardware notes

- Feature gate: CONFIG_PCCTL_ENABLE_HID=y plus CONFIG_TINYUSB_HID_COUNT=1
  (managed component espressif/esp_tinyusb, see main/idf_component.yml).
- TinyUSB and the USB-Serial-JTAG console share GPIO19/20, so the USJ
  driver and its secondary console are disabled (sdkconfig.defaults). The
  UART console, flashing and debugging are unchanged on COM9 (external
  USB-UART bridge); the native USB port enumerates as a HID keyboard
  (VID 303A, serial string "pcctl-hid") instead of a serial port.
- Typing uses US-layout HID keycodes; Windows maps them through the
  keyboard layout active at that moment - like a physical keyboard, a
  non-US layout at the login screen produces different symbols ("@" and
  "\" swap on en-GB, etc.).
- At the secure desktop (Ctrl+Alt+Del prompt) a plain keyboard cannot send
  the SAS; keystrokes only reach the password box once it is shown.

## Relay control (relay channels)

A configurable set of relay channels drives real GPIO outputs (relay
modules / switchable loads) from the same port-80 server and /pc page.
Implemented per `docs/RELAY_CONTROL.md`; module: `pc_relay.[ch]`.

- Channel model: `id` (index), `name` (user text, <= 32 bytes, UTF-8),
  `enabled`, `gpio` (NC or a board-allowlisted pin), `active_level`
  (Active HIGH/LOW - the level driven to energize the relay) and a fixed
  safe `startup_policy` (OFF; other values are rejected).
- Channel count 0..`CONFIG_PCCTL_RELAY_MAX_CHANNELS` (default 16, range
  1-16) is set in `/pc/settings` -> "Relay channels". Stored in the
  existing NVS namespace `pc_ctl`, key `relays` (versioned blob), written
  only on explicit saves; a failed write changes nothing.
- Firmware-side safety rules (not UI-only):
  - every configured pin is driven to its idle (OFF) level at boot and on
    every configuration change - a configuration change can never pulse;
  - relay pins must come from the board allowlist, be unique across
    channels and must not overlap the PC-control function pins; enforced
    on BOTH save paths (`pc_relay_config_apply` and `pc_config_apply`);
  - ON/OFF is rejected for disabled channels and GPIO = NC;
  - a channel-count reduction that would drop a channel with a GPIO
    mapping or the Enable flag is rejected (`409 unsafe_reduction`);
  - saving relay settings switches all channels OFF (safe-start apply)
    and a reboot never restores an energized relay;
  - `state` is the COMMANDED logic level only - there is no relay contact
    feedback, so the physical load state is never claimed.
  - channels can be deleted (`{"delete_channel":N}` or the two-step Delete
    buttons on `/pc` and `/pc/settings`): the channels after it shift up by
    one and its pin becomes assignable again; deletion runs through the same
    validated apply path (all relays are driven OFF).
- Invalid NVS entries are recovered per channel (parked as NC + disabled)
  without erasing anything; unknown blob versions fall back to defaults.

### API

    GET  /api/v1/relays        relay channels: config + commanded state
                               (+ allowed_gpios)
    POST /api/v1/relays        {"channel":0,"state":"on"|"off"}  (token)
         200 {"ok":true,"channel":0,"state":"on","changed":true,
              "note":"command issued - the relay contact state is not sensed"}
         400 missing_channel | invalid_state
         401 missing_token | invalid_token / 403 api_token_not_configured
         403 channel_disabled | gpio_not_configured
         404 unknown_channel  409 state_conflict  500 hardware_failure
    POST /api/v1/relays/config {"channel_count":N,"channels":[{"id":0,
              "name":"...","enabled":true,"gpio":13,"active_level":"high",
              "startup_policy":"off"}, ...]}                    (token)
         200 {"ok":true,"relays":{...}}
         400 invalid_field | invalid_config
         409 gpio_conflict | unsafe_reduction
         500 nvs_error

    POST /api/v1/relays/config {"delete_channel":N}      delete channel N
         200 {"ok":true,"deleted":N,"relays":{...}} - the channels after N
             shift up by one, the tail slot is cleared (standalone value;
             combining with channel_count/channels is rejected 400)

Explicit ON/OFF commands (never a toggle) are idempotent: retried
requests cannot reverse the intended state. The relay block also appears
in `GET /api/v1/status` (active channels, for the /pc page polling) and in
`GET/POST /api/v1/config` (all slots, for the Settings page).

### Hardware notes (relay modules)

- Verify the relay module's trigger polarity (opto modules are commonly
  low-trigger) and set Active HIGH/LOW to match - a wrong level inverts
  every ON/OFF command.
- The firmware cannot know whether the switched load is AC or DC and
  never treats GPIO/commanded state as proof of contact state. Confirm
  the relay contact rating (voltage / current / load type) and use mains
  isolation, an enclosure and inductive-load suppression as required.
- 5 V relay boards (e.g. JD-VCC modules) may need a level-shift stage on
  their inputs when driven from 3.3 V GPIOs; check the input threshold.

