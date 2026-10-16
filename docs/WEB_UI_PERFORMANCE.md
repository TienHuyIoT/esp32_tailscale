# Web UI Performance - how the embedded ISUI pages were made fast

Scope: the firmware's embedded web UI - the MicroLink component config page
(`/`, `components/microlink/src/ml_config_html.h`) and the pc_control pages
(`/pc`, `/pc/settings`, `/pc/security`). This document records the problems,
the techniques used to fix them, the measured results, and the pitfalls to
keep in mind when editing these files again.

Status of the work: all items below are implemented, device-verified on
COM9, and committed on `feat/isui-space-scene`
(`36307f2` perf, `b870e95` fix, `d9c80a4` fix, `0b1dfea` shared theme +
config page).

## 1. Symptoms before

| Symptom | Root cause |
|---|---|
| Page switches felt laggy / hung | Every page was one self-contained HTML document (116/90/81 KB) with the CSS and JS inline. Nothing was cacheable, so every navigation re-downloaded, re-parsed and re-compiled the whole page. |
| First paint stalled on the Space scene | The WebGL tier (galaxy fragment shader, prism backdrop, power-orb) compiled its GLSL synchronously while the page painted. |
| GPU constantly busy | Full-screen nebula rendered at full `devicePixelRatio` at 60 fps, plus a full-screen `alienPulse` CSS filter animation running forever. |
| Relay cards looked tilted / unbalanced after a reload | The scene CSS carried a static "deck" skew (`perspective(1250px) rotateX(8deg)`); `d9c80a4` flattened it for **fine pointers only**, so a coarse-pointer tablet (iPad, >760px) kept the skew with no tilt module to flatten it - fixed by making flat the default on every pointer (§5). |
| Two different visual languages, duplicated theme | The component config page had its own GitHub-dark inline CSS; the ISUI theme was duplicated inside the pc_control example. |

## 2. One theme, served as cacheable assets

The ISUI theme lives exactly once: `components/microlink/include/ml_ui_theme.h`
(CSS + UI helpers + space-scene JS as three raw strings). The new
`components/microlink/src/ml_ui.c` serves them and the pages that use them:

- Assets (immutable, content never changes between builds):
  - `GET /ml/static/theme.css` - ~46 KB (45.1 KiB) - shared components CSS,
    scene CSS, and a `.cfg`-scoped section for the config page's classes
  - `GET /ml/static/ui.js` - ~3 KB - toast, sheen, blur/parallax prefs
  - `GET /ml/static/space.js` - ~27 KB - galaxy, tilt, planets, prisms, orb
  - Response headers: `Cache-Control: public, max-age=31536000, immutable`
- Pages (never cached): small HTML only, via `ml_ui_serve_page()`
  (chunked `text/html`, `Cache-Control: no-cache`). It emits doctype/head,
  the versioned `<link>`/`<script src>` tags, the page markup and the page
  script - so a page switch only re-fetches the HTML; the ~77 KB of CSS/JS
  comes from the browser cache.
- Cache busting: the URL version is built from `esp_app_desc`
  (`<version>-<build date>-<build time>`, spaces -> dashes, e.g.
  `d9c80a4-dirty-Oct-16-2026-22:51:07`), so every flash automatically
  busts every browser cache without manual bookkeeping.
- One URL space for every page: the component config page and all three
  `/pc` pages reference the same three assets. (Before `0b1dfea` the /pc
  assets lived under `/pc/static/` and the config page had no assets.)

### Gotcha that shipped once and was fixed (`b870e95`)

`esp_http_server` keeps the query string inside `req->uri`, so the initial
exact-name `strcmp()` in the static handler 404'd on every real request
(`/ml/static/theme.css?v=<build id>`) and all pages rendered unstyled. The
handler now strips `?#` with `strcspn` before matching. Regression checks
must use the exact versioned URLs taken from the served page - not only the
bare paths.

## 3. Lazy, two-phase scene boot

`space.js` registers independent modules (`__isuiSpaceAdd`) and a tiny core
boots them through two phases:

- Phase 0 (immediate): the CSS scene paints right away - `html.scene-space`
  glass styling + the injected planetary DOM. No GLSL involved.
- Phase 1 (idle): galaxy canvas + pointer tilt start inside
  `requestIdleCallback` (timeout 900 ms; `setTimeout 120 ms` fallback).
- Phase 2 (delayed): prism backdrop + power-orb start on a 400 ms
  `setTimeout`.

So shader compilation never blocks a page switch or first paint. Related
lifecycle behavior:

- `visibilitychange` pauses every module while the tab is hidden and
  resumes on return.
- Switching to Glass classic stops all modules, removes the injected DOM
  and disposes the WebGL contexts (`WEBGL_lose_context`).
- `prefers-reduced-motion` pins the shader time/motion uniforms to 0
  (static render) and disables the CSS animations.
- Modules fail independently: a page without `.power-orb` (the config
  page) simply skips the orb module.

## 4. Rendering budget (galaxy layer)

- Frame rate: the galaxy RAF loop draws at ~30 fps
  (`if (now - lastDraw >= 32)`) instead of 60 - halving GPU work for a
  background layer nobody reads at 60 Hz.
- Backing store: `dpr = min(devicePixelRatio, 1.5) * 0.66` for both the
  WebGL nebula and the 2D-canvas starfield fallback (~0.66x on standard
  displays, <=0.99x even on 2x displays - roughly a quarter of the pixels
  of a naive `devicePixelRatio`-sized canvas on retina). The nebula is a
  soft gradient: the smaller framebuffer is visually indistinguishable.
- `alienPulse` (a permanent full-screen CSS filter pulse) was removed -
  continuous compositor cost for a decorative effect.
- The reference demo's heavier passes (tilted grid floor, vignette) were
  never ported because the reference itself hides them in its final pass.

## 5. Cards flat on every pointer (tilt fix, `d9c80a4` + iPad follow-up)

The scene CSS gave cards a static skew (`perspective(1250px) rotateX(8deg)`)
that was never meant to stand alone: the tilted "deck" look belongs to the
fine-pointer tilt module, which marks cards with `data-tilt`. `d9c80a4`
flattened cards from the first paint, but **only under `@media (pointer:fine)`**
- so a coarse-pointer tablet (iPad, width > 760px) kept the static 8° skew
forever (the module never starts there) and the Relay Control row looked
skewed, compressed and unbalanced after every reload.

Final shape (single source of truth):

- CSS: the card families (`.action-card`, `.relay-card`, `.metric-panel`) carry
  **no** transform at all now - flat on every pointer type from the first paint
  - and `transform-style: preserve-3d` lives **inside the fine-pointer tilt
  block**, so touch devices never build a 3D rendering context.
- CSS: the hover lift with 3D tilt moved into
  `@media (hover:hover) and (pointer:fine) and (prefers-reduced-motion:no-preference)`;
  on iOS a tap leaves a sticky `:hover`, which previously froze a card tilted.
- JS: unchanged - the tilt module marks `data-tilt` at start (reference
  behavior) and exposes `window.isuiTiltRefresh()`; `renderRelays()` calls it
  after rebuilding the relay grid, so dynamically rendered cards are stable
  immediately, before any hover.

Touch behavior: flat and stable at every width; `<=760px` and
`prefers-reduced-motion` keep their extra flattening guards.

## 6. The config page on the shared theme (`0b1dfea`)

`ml_config_html.h` was rewritten as `CONFIG_PAGE_MARKUP` +
`CONFIG_PAGE_SCRIPT` and restyled onto the ISUI shell (topbar, glass
sections, toast, sheen, Space scene auto-boot). Rules that kept the
rewrite risk-free:

- The page script is logically unchanged from the old one (same element
  ids, same APIs, same flows: monitor polling 3s/5s, WiFi list reorder +
  scan, immediate-apply peer allowlist, device settings, two-step restart
  - no JS dialogs, captive-portal webviews block them).
- The JS contract the markup/CSS must preserve:
  - `.mon-card` elements remain the only children of `.mon-grid`, in
    fixed order (the script re-labels `.mon-card:nth-child(2)`),
  - dynamic classes generated by the script are styled in the `.cfg`
    section of `ml_ui_theme.h`: `msg`/`msg-ok`/`msg-err`,
    `task-row`/`tn`/`ts`, `peer-item`, `wifi-item`, `empty`, and the
    legacy `btn-primary`/`btn-add`/`btn-danger`/`btn-outline`/`btn-sm`
    button classes,
  - all `.cfg` rules are scoped to the page root so they cannot leak
    into the /pc pages sharing the same stylesheet.
- Theme integration is automatic: `space.js` boots on every page (it
  self-calls `__isuiSpaceBoot`), `ui.js` adds the hover sheen to any
  `.glass`, and the Space/Glass preference (`localStorage
  pcctl_ui_scene`) applies across all four pages.

## 7. Measured results (device-verified)

| Page | Before | After | Notes |
|---|---|---|---|
| `/pc` | 116 KB HTML | 50,125 B HTML | + shared assets (cached after first load) |
| `/pc/settings` | 90 KB HTML | ~24 KB HTML | same |
| `/pc/security` | 81 KB HTML | ~13 KB HTML | same |
| `/` (config) | 21,563 B HTML (everything inline) | 18,821 B HTML | CSS/JS moved to the shared cache |

- Shared assets fetched once per flash: `theme.css` 46,167 B +
  `ui.js` 3,106 B + `space.js` 27,390 B = 76,663 B (~75 KiB), then served
  from the browser cache (`immutable`).
- Route budget on the port-80 server: 14 base routes (incl.
  `/ml/static/`) + 21 pc_control routes = 35 of `max_uri_handlers = 40`;
  setup mode additionally registers the captive-portal `/*` catch-all
  last.
- Flash cost of the shared theme when `CONFIG_ML_ENABLE_CONFIG_HTTPD=y`:
  ~77 KB (the three asset strings compiled in; `ml_ui.c` is ifdef-gated,
  and the strings disappear entirely when the option is off).

## 8. How to verify after a change

From a PC on the same tailnet (`IP` = device VPN IP):

```powershell
curl.exe -s http://$IP/            # 200; must reference /ml/static/*?v=<ver>
curl.exe -sD - -o NUL "http://$IP/ml/static/theme.css?v=x"   # 200, text/css, immutable
curl.exe -sD - -o NUL "http://$IP/ml/static/nope.js"         # 404
curl.exe -s http://$IP/pc          # 200; must reference /ml/static/*
```

Embedded-script syntax check: the page scripts are C raw strings, so
`node --check` needs an extraction step. A throwaway harness normally sits
at `examples/pc_control/build/check_js.js` (gitignored build dir - recreate
it from the listing below if cleaned): it pulls every
`static const char NAME[] = R"DELIM(...)DELIM";` block whose name looks
like JS and runs `node --check` on each.

```js
// check_js.js - node --check every embedded *_JS / *_SCRIPT raw string
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const files = process.argv.slice(2);
let failures = 0, checked = 0;
const re = /static const char (\w+)\[\] = R"([A-Za-z]*)\(([\s\S]*?)\)\2";/g;
for (const f of files) {
  const src = fs.readFileSync(f, 'utf8');
  let m;
  while ((m = re.exec(src)) !== null) {
    const name = m[1];
    if (!/_JS\b|_SCRIPT\b/.test(name)) continue;
    checked++;
    const tmp = path.join(__dirname, name + '.check.js');
    fs.writeFileSync(tmp, m[3]);
    const r = spawnSync('node', ['--check', tmp], { encoding: 'utf8' });
    if (r.status !== 0) failures++;
    console.log((r.status === 0 ? 'OK   ' : 'FAIL ') + f + ' :: ' + name);
  }
}
console.log('--- checked: ' + checked + ', failures: ' + failures);
process.exit(failures ? 1 : 0);
```

```powershell
cd examples/pc_control
node build\check_js.js main\pc_web.h main\pc_security_web.h `
  ..\..\components\microlink\src\ml_config_html.h `
  ..\..\components\microlink\include\ml_ui_theme.h
```

Build proof for component-level changes: build `pc_control` **and**
`basic_connect` - the config page ships in every example that enables the
config HTTP server, and `basic_connect` catches compile issues the
pc_control build would hide.

## 9. Pitfalls when editing these files

- Raw strings do no escape processing: write JS escapes literally
  (`\u00b0C`, `\"`). When porting old C-string pages, an extra layer of
  backslashes (`\\u...`) is a known transcription bug.
- Never write `/*` inside a C comment (e.g. the path `/ml/static/*`) in
  the theme httpd/server files - `-Werror=comment` fails the build. Say
  "the /ml/static/ namespace" instead.
- Keep the JS contract above (`ids`, `.mon-card` order, dynamic classes)
  in any markup/CSS restyle.
- The static handler matches names after stripping `?#`; removing that
  strip re-introduces the `?v=` 404 (see `b870e95`).
- Cache busting depends on the build date/time embedded in the version -
  rebuild-and-flash always busts; only edit the version scheme knowingly.
- The theme header defines its constants once (included by `ml_ui.c`
  only). Do not include `ml_ui_theme.h` in a second translation unit
  without revisiting the `static const` model.

## 10. Change history (this branch)

| Commit | What |
|---|---|
| `36307f2` | perf: cacheable static assets + lazy two-phase WebGL + galaxy 30 fps @ 0.66 scale + alienPulse removed |
| `b870e95` | fix: static assets 404 behind `?v=` (query string stripped before matching) |
| `d9c80a4` | fix: relay cards flat on reload (`pointer:fine` flatten + `isuiTiltRefresh()`) |
| `0b1dfea` | feat: shared theme in the component (`/ml/static/`, `ml_ui.c`) + ISUI config page at `/` |


