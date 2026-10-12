# PC Controller (ESP32-S3 + Tailscale + Windows agent)

Status: implemented and hardware-tested on a real tailnet (ESP32-S3,
16 MB flash, COM9). Firmware: `examples/pc_control`.
Windows side: `tools/pc_agent`. Both run in their safe default modes
(firmware MOCK/MIXED, agent `dry_run`).

## What this is

Six PC-control commands driven from an ESP32-S3 over an existing
Tailscale tailnet, with an embedded responsive web UI and a versioned
JSON API:

- `POWER_ON`  - pulse PWR_SW to start a PC that is off (hardware path)
- `WAKE_UP`   - wake a sleeping PC (hardware/wake path, WoL planned)
- `SHUTDOWN`  - graceful Windows shutdown via the PC agent
- `RESTART`   - graceful Windows restart via the PC agent
- `PWR_RESET` - pulse RESET_SW (hardware path, confirmation required)
- `FORCE_POWER_OFF` - long PWR_SW hold for a frozen PC (hardware path,
  strong confirmation required)

Hardware commands and Windows commands are routed strictly separately.

## System diagram

```
     laptop / phone (any tailnet device)
              |
              |  HTTP through the WireGuard tunnel
              v
  ESP32-S3  <ESP32_VPN_IP>                  Windows PC  <PC_VPN_IP>
  +----------------------------+           +---------------------------+
  | microlink core (unchanged) |           | tailscaled                |
  | port-80 esp_http_server:   |           |    |                      |
  |   /          config UI     |           |    v                      |
  |   /pc        PC-control UI |           | pc_agent.py  :8765        |
  |   /api/v1/*  PC API        |<--------->|  (dry_run by default)     |
  | pc_ctl task:               |  HTTP     |  shutdown.exe only if a   |
  |   hw -> mock / GPIO(gated) |  (bearer  |  human enables the two    |
  |   win -> pc_win_client     |   token)  |  real-execution gates     |
  +----------------------------+           +---------------------------+
```

## Safety and honesty rules (enforced in code, verified on hardware)

- `MOCK_ALL` is the build default; the GPIO implementation is compiled
  out by default; the agent's dry-run mode is protected by two
  independent config gates (`dry_run`, `allow_real_execution`).
- `succeeded` means exactly one of: "the agent accepted/scheduled the
  operation" (dry-run flagged explicitly in the result detail) or "the
  hardware action was issued" - never a fabricated physical confirmation.
- Service reachability and PC power state are tracked separately. An
  unreachable agent produces PC state `unknown` with source `agent_lost`,
  never "off". A reachable agent produces `online` labelled as an
  inference (source `agent`); real pending operations from the agent
  yield `shutting_down`/`restarting` - dry-run pendings never do.
- `FORCE_POWER_OFF` is only ever executed when explicitly requested;
  nothing in the system invokes it automatically.
- Duplicate-request protection: one active command at a time (409 with
  the active id), idempotent replay by `request_id`, and a hardware
  cooldown window (`PCCTL_HW_COOLDOWN_MS`, default 5 s) that rejects
  repeat hardware commands even with fresh ids. The remaining cooldown
  is reported as `hw_cooldown_ms` in `/api/v1/status`; the control page
  shows it and fast-polls (1 s) until the hardware buttons unlock.

## Repository layout (this feature)

| Path | Purpose |
|------|---------|
| `examples/pc_control/` | firmware example (see its README) |
| `examples/pc_control/main/pc_*.c/h` | firmware modules (command defs, state store, hw abstraction + mock + gated GPIO, agent HTTP client, controller task, API, embedded UI) |
| `tools/pc_agent/` | Windows agent (Python, stdlib only, dry-run by default) |
| `tools/gen_pc_tokens.py` | generates/rotates both tokens into `sdkconfig.credentials` |
| `tools/serial_capture.py` | non-interactive COM-port log capture for flash cycles |
| `docs/PC_CONTROLLER.md` | this document |

## Changes to the MicroLink component (fork-local, additive only)

| File | Change | Why |
|------|--------|-----|
| `components/microlink/include/ml_config_httpd.h` | added `struct httpd_data *ml_config_httpd_get_handle(ctx)` (+stub) | exposes the running port-80 `esp_http_server` handle so sibling modules can register routes on the same server instead of starting a second one. The opaque `struct httpd_data *` spelling keeps `esp_http_server.h` out of the header. |
| `components/microlink/src/ml_config_httpd.c` | getter implementation; `max_uri_handlers` 16 -> 24 | 13 existing routes + 6 pc_control routes + margin. |

Nothing else in the component was modified; `examples/basic_connect`
and the other examples are untouched.

## Firmware architecture

One extra FreeRTOS task joins the four microlink tasks:

| Task | Core | Prio | Stack | Role |
|------|------|------|-------|------|
| `pc_ctl` | any | 5 | 8 KB | commands first (non-blocking drain), then agent polling with capped offline back-off; executes hardware actions and agent HTTP; supervises deadlines |

State is shared only through the mutex-protected store in `pc_state.c`.
HTTP handlers never block on network I/O: they validate, submit to the
`pc_ctl` queue (depth 4) and return `202` immediately. Multi-KB state
snapshots are `static` (safe: one `pc_ctl` task; one serialized httpd
task) to keep the task stacks small - this was found the hard way during
bring-up (a snapshot-stack overflow crash-loop was reproduced and fixed).

Outbound HTTP to the agent uses a MicroLink TCP socket
(`microlink_tcp_connect/send/recv`) through the WireGuard tunnel. The
agent hostname resolves from the local peer table (MagicDNS-style
`microlink_resolve()`, no DNS server needed). While the agent is known
to be offline, connect attempts use a short timeout and the poll
interval backs off (10 s doubling, capped at 30 s) so queued commands
are never delayed by long failing connect attempts; commands submitted
while the task is idle are picked up in milliseconds (verified: with the
agent down, a POWER_ON went `running` at t+0.2 s).

### Execution modes (Kconfig `PCCTL_EXEC_MODE_*`)

| Mode | Hardware commands | SHUTDOWN / RESTART | Typical use |
|------|-------------------|--------------------|-------------|
| `MOCK_ALL` (default) | simulated in firmware (`pc_hw_mock`) | simulated in firmware; the agent is not contacted | development, UI/API testing |
| `MIXED` | simulated in firmware | real requests to `tools/pc_agent` (dry-run by default) | current deployed setup |
| `FULL` | real GPIO pulses - requires `PCCTL_ENABLE_HW_GPIO=y` and a verified interface circuit | real requests to the agent | future, after hardware + human validation |

The active mode is reported by the API (`mode`), shown as a badge in the
UI, and appears in the hardware layer's serial log lines.

## Runtime configuration (NVS) and the device Settings UI

Host/port/timeout, the agent token, per-function Enable flags, GPIO
mapping (pin + active level) and press timings are **runtime settings**
persisted in NVS (namespace `pc_ctl`, versioned blobs) - Kconfig values
are only the first-boot defaults. They are edited at
`http://<vpn-ip>/pc/settings` (linked from the control page) via
`GET/POST /api/v1/config`, `POST /api/v1/config/token` and
`POST /api/v1/config/test`.

- Writes happen only on explicit saves (no NVS churn while editing), and
  `pc_config_apply()` validates everything first and persists both blobs
  before touching RAM - a failed save changes nothing.
- The token is stored in NVS, **never echoed** by any API (only
  `token_set: true/false`), never logged, and can be cleared explicitly
  (once cleared, windows commands report `503 agent token not
  configured` instead of failing obscurely).
- Defaults follow the project plan: `SHUTDOWN`/`RESTART` enabled;
  `POWER_ON`/`WAKE_UP`/`PWR_RESET`/`FORCE_POWER_OFF` disabled with no
  GPIO mapping. Hardware functions need Enable **and** a mapped GPIO
  (`403 function_disabled` / `403 gpio_not_configured` otherwise).
- Settings survive reboots (verified boot log: restored
  `funcs=0x3f gpio=[13,13,14,13] lvl=[1,1,1,1] short=500 long=5000` -
  levels 1/1/1/1 = all four outputs active-HIGH), and a reboot can
  never enable real execution.
- `POST /api/v1/config/test` queues a one-shot agent probe executed by
  the `pc_ctl` task (never from the HTTP handler); the result appears as
  `last_test` with `reachable` and `authenticated` reported separately
  (401 = reachable with bad token, transport error = not reachable; a
  successful test means the service confirmed authentication).

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
100-2000 ms, default 300; long press 1000-15000 ms, default 5000) and
were NOT physically validated - verify on the real circuit before
enabling `CONFIG_PCCTL_ENABLE_HW_GPIO`.

Each hardware function also has an **active level** (runtime setting,
default HIGH): the GPIO level that "presses" the front-panel input; idle
is the opposite level. Configure it in the same GPIO mapping card:

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
- Blobs written by the first firmware (NVS `"hw"` v1, no levels) are
  migrated in RAM on first boot - levels default to HIGH (= v1 behavior)
  and the v2 layout is persisted on the next explicit Settings save.

## Windows Manager UI (local-only)

`tools/pc_agent/pc_manager.py` (stdlib only) serves a management page on
**127.0.0.1 only** (default port 8766; verified with netstat). It
detects the Tailscale IPv4, edits port/bind/allowlist/function flags
(config.json written atomically + ACL-hardened via icacls), generates
tokens (shown once, then masked), starts/stops/restarts the agent
(adopting an already-running process), and runs the readiness + auth
test. It deliberately CANNOT edit `dry_run`/`allow_real_execution` -
those remain human-only config-file gates.

The page detects a dead manager API (e.g. a browser tab restored after a
PC reboot) and shows a red **MANAGER API UNREACHABLE** banner instead of
stale values; the manager refuses to start when `127.0.0.1:8766` is
already in use, re-reads `config.json` when it changes on disk
(hand-edits appear within one refresh cycle), and appends its log to
`tools/pc_agent/manager.log`.

## Pairing workflow (new installation)

1. Confirm Tailscale on Windows and that the ESP32 exists on the tailnet.
2. `python pc_manager.py`, open `http://127.0.0.1:8766`.
3. Confirm the detected Tailscale IP; set the service port (8765).
4. **Generate new token** and copy it (shown once).
5. Verify the source address the service actually sees: the agent logs
   `client=...` per request (observed: `client=<ESP32_VPN_IP>`); allowlist
   that address or the tailnet CIDR.
6. **Start** the service; **Test** -> expect reachable + authenticated.
7. On the ESP32: `http://<vpn-ip>/pc/settings` -> set PC host, port,
   agent token -> Save -> **Test connection** must report authenticated.
8. Confirm both sides report dry-run before relying on SHUTDOWN/RESTART.

## Command lifecycle and duplicate protection

```
POST /api/v1/commands
        |
        v
   accepted --(pc_ctl dequeues)--> running --> succeeded
        |                              |        failed
        |                              |        timeout (deadline)
        +-- cancel (before dispatch) -> cancelled
                                       |-- cancel via agent (while running)
```

Rules (enforced in `pc_controller.c` / `pc_state.c`):

- Exactly one active command; a second submit while busy is `409`,
  carrying the active `request_id`/command/state.
- Re-submitting a known `request_id` (active or in the 4-entry history)
  is idempotent: `200 {"replay":true,"command":{...}}` - no second
  execution. Auto-generated ids look like `c-<boot hex><random>`.
- Hardware route: configurable cooldown (`PCCTL_HW_COOLDOWN_MS`, default
  5 s) between consecutive hardware commands, even with different ids
  (`409 cooldown` + `retry_after_ms`). The UI mirrors it: while the
  cooldown is live the hardware buttons are disabled with the remaining
  seconds as their reason, the page polls every second until they
  unlock, then returns to the normal cadence.
- `PWR_RESET` / `FORCE_POWER_OFF` require `"confirm":"<COMMAND_NAME>"`;
  `FORCE_POWER_OFF` additionally accepts `params.hold_ms` (1000..15000).
- Cancellation is honest: a running hardware action yields `409
  not_cancellable` ("action already in progress beyond the cancellation
  point"); a running windows command forwards a cancel to the agent
  (`202`, `"cancelled":false`, "cancel forwarded...") whose outcome lands
  in the command state on the next poll.
- Every command has a deadline; the supervisor marks overruns `timeout`.

## Service and PC state model

| Layer | States | Labels |
|-------|--------|--------|
| Windows agent (service) | `unknown` / `online` / `offline` | `last_ok_age_s`, `last_check_age_s`, `last_error` |
| Physical PC | `unknown` / `online` / `offline` / `sleeping` / `shutting_down` / `restarting` | `pc.source` (`none`/`agent`/`agent_lost`/`hardware`) + `pc.note` |

Transitions worth knowing:

- agent reachable, nothing pending -> `online`, source `agent`, note
  "inference, no direct power sensing" (explicitly labelled inference);
- agent unreachable -> service `offline`; if the PC was `online` *by
  inference* it becomes `unknown` (`agent_lost`, "previous online state
  was an inference"); if a real shutdown/restart was pending it becomes
  `unknown` with "agent unreachable after shutdown/restart request -
  PC state unknown" (never "off");
- the agent reports a real (non-dry-run) pending shutdown/restart ->
  `shutting_down`/`restarting`; a dry-run pending keeps `online` with an
  explicit note.

## ESP32 API contract (port 80, `/api/v1/`)

All responses are JSON. Mutations require the API token; reads do not
(same exposure level as the pre-existing `/api/status`).

| Route | Method | Auth | Notes |
|-------|--------|------|-------|
| `/pc` | GET | no | embedded UI (single page) |
| `/api/v1/status` | GET | no | fw/esp32/service/pc + active/last command + auth info + `poll_interval_s` / `hw_cooldown_ms` |
| `/api/v1/commands` | GET | no | six commands with `available`/`unavailable_reason`, route, confirm/cancel flags |
| `/api/v1/commands` | POST | **yes** | submit `{"command","request_id"?,"confirm"?,"params"?}` |
| `/api/v1/commands/{id}` | GET | no | state/result for a request id (active or history) |
| `/api/v1/commands/{id}/cancel` | POST | **yes** | honest cancellation result |
| `/api/v1/commands` with `{"command":"PING"}` | POST | **yes** | auth probe used by the UI's token dialog: `200 {"ok":true,"pong":true}` when the token is valid, `401` otherwise. Executes nothing. |

Example status response (trimmed; captured on hardware):

```json
{
  "ok": true, "mode": "MIXED",
  "fw":  {"name":"microlink_pc_control","version":"0.1.0","uptime_s":52},
  "esp32": {"state":"CONNECTED","vpn_ip":"<ESP32_VPN_IP>","wifi_ip":"<ESP32_LAN_IP>","hostname":"<ESP32_HOSTNAME>"},
  "service": {"state":"online","reachable":true,"last_ok_age_s":1.7,"last_error":"","host":"<PC_HOSTNAME>","port":8765,"agent_dry_run":true},
  "pc": {"state":"online","source":"agent","note":"agent reachable (dry-run; PC state is an inference)"},
  "active_command": null,
  "last_command": {"request_id":"t-shut-1","command":"SHUTDOWN","state":"succeeded","simulated":true,
                   "detail":"agent accepted SHUTDOWN request (dry-run: nothing real was scheduled)"},
  "poll_interval_s": 10,
  "hw_cooldown_ms": 0,
  "auth": {"token_configured": true, "mutations_allowed": true}
}
```

Request/error semantics (all verified on hardware):

| Case | Result |
|------|--------|
| accepted | `202 {"ok":true,"request_id":"...","state":"accepted"}` |
| same id again | `200 {"ok":true,"replay":true,"command":{...}}` |
| busy (other id active) | `409 {"error":"busy","active_request_id":...}` |
| hardware cooldown | `409 {"error":"cooldown","retry_after_ms":N}` |
| missing/invalid token | `401 {"error":"missing_token"/"invalid_token"}` |
| no token configured in firmware | `403 api_token_not_configured` (unless the dev escape hatch is enabled) |
| unknown command / bad json / bad id / missing confirm / bad params | `400` with a specific `error` field |
| oversized body / wrong content type | `413` / `415` |
| agent known offline (windows route) | `503 {"error":"unavailable","detail":"Windows agent unreachable"}` |
| cancel impossible | `409 {"error":"not_cancellable","state":...,"detail":...}` |
| unknown id / path | `404` |

The API accepts no shell-like input and returns no secrets.

## Windows agent contract (mapping)

| Firmware command | Agent call | Firmware "succeeded" means |
|------------------|-----------|-----------------------------|
| `SHUTDOWN` | `POST /api/v1/shutdown {"delay_s":N}` | agent answered `202 scheduled` (dry-run flagged); a real `shutdown /s /t N` was only *requested* - completion is unobservable by design |
| `RESTART` | `POST /api/v1/restart {"delay_s":N}` | same, with `shutdown /r /t N` |
| cancel (windows, running) | `POST /api/v1/cancel-shutdown` | `shutdown /a` outcome (or a local dry-run cancel) |

Agent errors map into the command's `error`/`detail` fields verbatim
(e.g. `error:"busy"` + `detail:"agent reports 'shutdown' already pending
(about 57 s remaining)"`); a structured HTTP response from a live agent
never marks the service offline.

## Security model

- Threat model: other tailnet members, LAN devices, anything able to
  reach the ESP32's port 80. Out of scope: someone with admin access to
  the PC itself (they already control it).
- Authorization: shared token compared in constant time
  (`X-PC-Token` or `Authorization: Bearer`), required on every mutation.
  Tailnet membership is not treated as authorization. Reads stay open by
  design (they match the pre-existing config UI's model) and contain no
  secrets.
- Transport: plain HTTP is acceptable *only* because all traffic stays
  inside the WireGuard tunnel. Never port-forward the ESP32 or the
  agent.
- The agent also filters by source CIDR (`allow_clients`, default
  Tailscale range + localhost) and should be firewall-scoped with
  `remoteip=100.64.0.0/10`.
- Secrets: `sdkconfig.credentials` (firmware) and `config.json` (agent),
  both gitignored (verified with `git check-ignore`). Tokens are never
  logged: the agent log records method, path, client address and status
  only.
- Rotation: `python tools/gen_pc_tokens.py`, restart the agent, delete
  `examples/pc_control/sdkconfig`, rebuild + reflash.

## Operations

### Build / flash / monitor

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
cd examples/pc_control
idf.py build
idf.py -p COM9 flash          # NVS is preserved: never erase flash
idf.py -p COM9 monitor        # interactive
```

Non-interactive capture (for regression cycles):

```powershell
$env:PYTHONIOENCODING='utf-8:replace'
python ..\..\tools\serial_capture.py COM9 90 --reset |
    Out-File -Encoding utf8 build\run.log
Select-String -Path build\run.log -Pattern 'rst:0x|Guru|pc_ctl|pc_api'
```

### Provisioning state

WiFi credentials, Tailnet keys and the device name live in NVS
(namespaces `ml_config`, `microlink`) and survive reflashing. The boot
flow falls back to SoftAP setup mode (`MicroLink-Setup-xxxx`,
http://192.168.4.1/) when no known network is reachable; the config UI
at `/` handles WiFi/auth changes. A factory reset is only for a
deliberate re-provision (it destroys the Tailnet identity).

### Windows agent

See `tools/pc_agent/README.md` (setup, firewall rule, Task Scheduler,
test matrix). Current dev setup: started manually in dry-run, bound to
the Tailscale IP.

## Testing (as executed during bring-up)

| Area | Procedure | Observed result |
|------|-----------|-----------------|
| Baseline | `basic_connect` build; 25 s serial capture of the pre-existing firmware | BUILD_EXIT=0; device healthy; DISCO direct path to the PC ~50 ms |
| Firmware | build + flash `pc_control` (MOCK); 90 s boot capture | 6/6 routes registered; VPN CONNECTED at ~8 s; 0 crashes in 90 s (17-reboot crash loop found and fixed during bring-up) |
| Mock API | six-command matrix incl. confirm, cooldown, replay, busy, cancel, auth, validation | all cases matched the contract |
| UI | `GET /pc` over the tailnet; marker checks; same API as the manual tests | 200 OK, ~19 KB page; responsive CSS (grid collapses for phone viewports) |
| Agent | `--self-test` + full curl matrix (auth, validation, duplicates, cancel, subprocess-error paths) | 8/8 self-test PASS; dry-run only; no tokens in logs |
| E2E (MIXED) | SHUTDOWN/RESTART through the tunnel to the dry-run agent; conflict; agent down/up; 503 gating; ESP32 reboot | succeeded with honest dry-run detail; structured `busy` surfaced correctly; recovery verified |
| Regression | UDP echo :9000 kept; config UI `/` and `/api/status` untouched; NVS identity retained across ~8 flashes | operational |
| GPIO path | temporary `PCCTL_ENABLE_HW_GPIO=y` compile check (not flashed, nothing wired) | compiles clean (a shift-count bug was caught and fixed this way) |
| Trigger level + cooldown | live `FULL` device: `GET/POST gpio_levels` matrix (200 / 400 invalid / 409 shared-pin mismatch), NVS v1 -> v2 migration + persistence in boot logs, one user-approved `POWER_ON` pulse with `hw_cooldown_ms` sampled during the cooldown | levels round-trip and survive reboot; the pulse used the configured level (logged active-LOW after the operator switched the outputs in the Settings UI); `hw_cooldown_ms` counted 4372 -> 0 ms; duplicate submit -> `409 cooldown` with matching `retry_after_ms`; control-page JS passes `node --check` |

Deliberately **not** performed: real Windows shutdown/restart
execution, Wake-on-LAN transmission. One `POWER_ON` pulse (500 ms,
GPIO13) WAS issued with explicit user consent during the trigger-level /
cooldown verification above - the command reported `succeeded` and the
cooldown engaged; the electrical effect on the front-panel circuit was
not independently measured (no meter attached).

## Known limitations and watch items

- **One unattributed reboot** was observed during an idle period (no
  backtrace captured). Earlier crash-looping had a clear root cause
  (a state-snapshot stack overflow, fixed and since stable across long
  runs). If it recurs, capture with `tools/serial_capture.py` and
  inspect `rst:0x` / `Guru`.
- Cancel-forwarding while a windows command is *running* is
  time-sensitive (the agent round trip is ~250 ms); the honest
  `409 not_cancellable` path is the common outcome and is fully tested.
- While the agent is offline, a poll attempt can still block for a few
  seconds (`connect()` in the current microLink TCP API is not bounded
  by `SO_SNDTIMEO`). The commands-first loop and the back-off cap keep
  the practical impact to "a command may occasionally wait a few
  seconds"; steady-state behavior is immediate.
- There is no direct power sensing: `online` is always an explicitly
  labelled inference; `offline`/`sleeping` states exist in the model
  awaiting a hardware signal.
- The agent's `cancel-shutdown` aborts ANY pending Windows
  shutdown/restart, including ones started outside the agent.

## Future hardware integration (prepared, not enabled)

The gated pieces already exist: `pc_hw_gpio.c` (pulse/hold), Kconfig
(`PCCTL_ENABLE_HW_GPIO`, pin numbers, pulse widths, force-off hold) and
the mock/hw vtable in `pc_hw.h`. Before enabling:

1. Design an electrically verified interface to the motherboard
   front-panel pins (PWR_SW# / RESET_SW# are active-low logic signals -
   use a transistor/optocoupler/isolated relay; never wire a GPIO
   directly; add an external pull resistor matching the configured active
   level - pulldown for active-HIGH, pullup for active-LOW - so floating
   pins cannot press anything during boot).
2. Verify with a meter/scope, then enable `PCCTL_ENABLE_HW_GPIO=y` in
   `FULL` mode and test one action at a time (the cooldown protects
   against repeats).
3. Boot/reset safety: outputs are driven to idle as the controller
   starts; a reboot during a pulse simply ends the pulse.

Notes:

- PWR_SW/RESET_SW actions are blind pulses; adding a power-sense input
  (e.g. optocoupler across a 5 V rail or the power LED) is the natural
  next step and would let the state model use `hardware` as a source.
- `WAKE_UP` is simulated today. Wake-on-LAN is plausible because the
  ESP32 shares the PC's LAN: broadcast a magic
  packet from the STA interface. Over Tailscale alone it cannot work
  (a sleeping PC's tailscaled is down). Implement behind a config flag
  and report only "packet sent - outcome unverified".



