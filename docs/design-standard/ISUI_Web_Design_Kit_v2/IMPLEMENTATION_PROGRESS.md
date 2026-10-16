# ISUI Implementation Progress — pc_control embedded UI

Scope: redesign the ESP32 firmware's embedded web UI (`examples/pc_control`)
with the ISUI design language from `reference/ISUI_REFERENCE.html`.
Execution mode: plan-first (approved 2026-10); plan: 3 pages + real relay
configure modal on `/pc`. Extended (2026-10, S10): the shared theme was
hoisted into the `microlink` component (`ml_ui_theme.h` + `ml_ui.c`, served
under `/ml/static/`) and the component's own config page at `/` now uses the
same theme and asset cache.

## Status

| Stage | Scope | Status | Evidence |
|---|---|---|---|
| S1 | `pc_ui_theme.h` — shared tokens/components CSS + UI helpers (toast, orbs, sheen, parallax, prefs) | DONE | build OK; `node --check` on extracted `pc_ui.js` |
| S2 | `/pc` control deck (topbar/sidebar/hero/metrics/action cards/relay cards + real Configure modal/GPIO table/activity log/HID/token/confirm/interface-settings modals) | DONE | build OK; page served 75 KB HTTP 200; `node --check` page script OK |
| S3 | `/pc/settings` (same shell; all save/load flows preserved) | DONE | build OK; page served 50 KB; `node --check` OK |
| S4 | `/pc/security` (same shell; login/token/credentials flows preserved) | DONE | build OK; page served 41 KB; `node --check` OK |
| S5 | Hardware verification + docs + commit | DONE | flashed COM9; see below |
| S6 | Space scene (Ætherium + Galaxy Drift) from `reference/ISUI_SPACE_SCENE_REFERENCE.html`, full-WebGL port (user choice), Space/Glass toggle | DONE | branch `feat/isui-space-scene`; build OK; `node --check` x5; flashed COM9; pages served 116/90/81 KB with scene markers; API regression green |
| S7 | Relay channels: max 16 + per-channel delete (NVS length-driven migration, `delete_channel` API with shift, Delete buttons on /pc modal + /pc/settings rows) | DONE | build+flash; existing config preserved across the 8->16 limit bump; add/shift/delete round-trip + reboot persistence + error cases (404/400) verified on the device; 2 transcription bugs caught & fixed by those tests |
| S8 | Perf: page-switch lag fix - cacheable `/pc/static/` assets (build-id versioned, immutable) + lazy 2-phase WebGL boot + galaxy 30 fps @ 0.66 render scale + alienPulse removed | DONE + follow-up fix | headers verified (immutable assets, correct content types, 404 fallback); page HTML 116/90/81 KB -> 50/24/13 KB; API regression green. Follow-up: the first release 404'd on the exact `?v=` URLs (esp_http_server puts the query in `req->uri`); `handler_static` now strips `?`/`#` before matching and is re-verified against the exact versioned URLs from the served page |
| S9 | Fix: relay cards looked tilted/unbalanced after a page reload until the first mouse move (the static "deck" skew showed before the tilt module marked cards on pointermove) | DONE | theme CSS flattens cards on fine pointers from the first paint; tilt marks every card at module start and `window.isuiTiltRefresh()` is called after each `renderRelays()` grid rebuild; verified inside the served `theme.css`/`space.js` on the device |
| S10 | Shared theme hoisted into the microlink component (`ml_ui_theme.h` + `ml_ui.c`, assets under `/ml/static/`) and the component's own config page `/` redesigned on the ISUI theme (Space scene + cached assets shared with all /pc pages; `/pc/static/` route removed) | DONE | build OK for `pc_control` AND `basic_connect` (component-level path); `node --check` x6; flashed COM9: `/` = 18.8 KB with all 41 element ids + 9 mon-cards, theme CSS/space.js/ui.js immutable 200 (query-stripped URLs), `/pc` = 50 KB using `/ml/static/`, `/pc/settings` + `/pc/security` + `/api/*` 200; `/pc/static/*` and unknown asset names 404 |

## S6 details (space scene)

- Ported: effective-final CSS of the (multi-pass) reference, scoped under
  `html.scene-space`: obsidian/neon glass, card bevel + bottom extrusion,
  hover lift, pointer tilt + translateZ layers, planets (giant/ringed/moon/
  distant + atmosphere/clouds/streaks), galaxy body background, alienPulse.
  NOTE: the reference's tilted grid floor + vignette were deliberately NOT
  ported - its final pass hides them (`display:none`); only the rendered
  result was reproduced.
- Ported JS (`PCCTL_UI_SPACE_JS`): galaxy WebGL fragment-shader nebula +
  star layers + flight streaks (verbatim shader) with a 2D-canvas starfield
  fallback; raw-WebGL prism backdrop (cubes/torus/cuboids) and power-orb
  sphere + 3 orbit rings (own matrix/mesh code, no libraries); scene DOM
  injection (planets/canvases), initial state + toggle via
  `isuiSetScene('space'|'glass')` (localStorage `pcctl_ui_scene`,
  default Space); Interface Settings gains a "Space scene" switch on /pc.
- Deviations from the reference (documented): galaxy RAF pauses while the
  tab is hidden (reference only pauses the prism renderer); WebGL contexts
  are disposed (`WEBGL_lose_context`) when switching back to Glass; each
  canvas fails independently (settings/security pages have no orb canvas).
- Not verified here: visual quality / FPS are hardware-eyeball checks
  (no browser automation in scope); reduced-motion OS setting and the
  Space<->Glass switch need a human pass.

## Hardware verification executed (device on COM9, via LAN HTTP)

- All three pages: HTTP 200 with chunked serving; required markers present
  (relay grid, configure-modal fields, settings save flow, security login).
- Relay configure modal payload (real): `{"channels":[{id,name,enabled,
  gpio,active_level,startup_policy}]}` -> 200; channel configured + shown
  available; `ON` / `OFF` -> 200; `Add channel` payload
  `{"channel_count":N+1}` -> 200; config restored to 0 channels after test.
- Regression: `GET /api/v1/status` ok + `CONNECTED`; 4 commands available;
  relay block intact; fw 0.1.0.

## Limitations / not verified here

- Visual appearance (layout at breakpoints, glass depth, motion) was NOT
  verified with browser automation (none available in scope) - structural +
  behavioral checks only; a human visual pass on desktop + mobile is
  recommended.
- `prefers-reduced-motion` and the blur/parallax toggles are implemented
  (auto-off + toggle) but were not exercised on a device with that OS
  setting enabled.
- No framework/bundle step (none exists in this firmware); pages stay
  vanilla HTML/CSS/JS embedded as C strings (ESP-IDF raw strings).
- Demo-only behavior from the reference (mock toggles, fake events) was
  deliberately NOT copied; designs were adapted to the real APIs.

## How to preview

1. Flash `examples/pc_control` and open `http://<vpn-ip>/` (component config
   page, now ISUI-themed), `http://<vpn-ip>/pc` (control deck),
   `/pc/settings`, `/pc/security`.
2. Set the API token (banner or `/pc#t=TOKEN`) to use mutations.
3. Interface settings (topbar sliders icon on `/pc`) toggles blur/parallax
   locally (localStorage `pcctl_ui_*`); never sent to the device.
