# ISUI Implementation Progress — pc_control embedded UI

Scope: redesign the ESP32 firmware's embedded web UI (`examples/pc_control`)
with the ISUI design language from `reference/ISUI_REFERENCE.html`.
Execution mode: plan-first (approved 2026-10); plan: 3 pages + real relay
configure modal on `/pc`.

## Status

| Stage | Scope | Status | Evidence |
|---|---|---|---|
| S1 | `pc_ui_theme.h` — shared tokens/components CSS + UI helpers (toast, orbs, sheen, parallax, prefs) | DONE | build OK; `node --check` on extracted `pc_ui.js` |
| S2 | `/pc` control deck (topbar/sidebar/hero/metrics/action cards/relay cards + real Configure modal/GPIO table/activity log/HID/token/confirm/interface-settings modals) | DONE | build OK; page served 75 KB HTTP 200; `node --check` page script OK |
| S3 | `/pc/settings` (same shell; all save/load flows preserved) | DONE | build OK; page served 50 KB; `node --check` OK |
| S4 | `/pc/security` (same shell; login/token/credentials flows preserved) | DONE | build OK; page served 41 KB; `node --check` OK |
| S5 | Hardware verification + docs + commit | DONE | flashed COM9; see below |
| S6 | Space scene (Ætherium + Galaxy Drift) from `reference/ISUI_SPACE_SCENE_REFERENCE.html`, full-WebGL port (user choice), Space/Glass toggle | DONE | branch `feat/isui-space-scene`; build OK; `node --check` x5; flashed COM9; pages served 116/90/81 KB with scene markers; API regression green |

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

1. Flash `examples/pc_control` and open `http://<vpn-ip>/pc` (control deck),
   `/pc/settings`, `/pc/security`.
2. Set the API token (banner or `/pc#t=TOKEN`) to use mutations.
3. Interface settings (topbar sliders icon on `/pc`) toggles blur/parallax
   locally (localStorage `pcctl_ui_*`); never sent to the device.
