/**
 * @file pc_web.h
 * @brief Embedded ISUI pages for the PC controller: the control deck (/pc)
 *        and the device Settings page (/pc/settings).
 *
 * The pages share the ISUI theme (pc_ui_theme.h: CSS + helper JS); this
 * header holds only the per-page markup and page script, which pc_api.c
 * streams through send_ui_page(). Include pc_ui_theme.h once (pc_api.c)
 * before this header.
 *
 * Behavior contract (unchanged from the pre-ISUI page):
 *  - PC power state is only ever reported with an explicit source/note -
 *    the device has no power-sensing input, so it is usually "unknown";
 *  - every command drives the real motherboard front-panel GPIO;
 *  - commands are disabled only while another command is active (the HW
 *    cooldown runs in the background and is reported, not enforced by locks);
 *  - POWER_ON / WAKE_UP ask for confirmation; PWR_RESET and FORCE_POWER_OFF
 *    require typing the command name (inline modal - no JS confirm/alert);
 *  - relay cards show the COMMANDED state only (no contact feedback); the
 *    configure modal writes through POST /api/v1/relays/config and the
 *    firmware re-validates everything (pins, conflicts, ranges);
 *  - status polling backs off on failure and pauses while the tab is hidden;
 *  - the USB HID login panel is explicit-submit only and never claims that
 *    Windows accepted the password.
 */

#pragma once

/* ---- /pc : control deck ---- */

static const char PCCTL_PAGE_MARKUP[] = R"PCM(
<body>
<div class="ambient" aria-hidden="true"><div class="orb one"></div><div class="orb two"></div><div class="orb three"></div></div>
<div class="app">
  <header class="topbar">
    <div class="brand">
      <div class="logo" aria-hidden="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6"><rect x="4" y="4" width="16" height="16" rx="4"/><path d="M9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m12 0h4M2 15h4m12 0h4"/><path d="m9 12 2 2 4-5"/></svg></div>
      <div><h1>Control Deck <span class="acc">/ S3</span></h1><p>ESP32-S3 PC Controller</p></div>
    </div>
    <div class="top-actions">
      <div class="connection" id="bNet"><span class="dot"></span><span id="netTxt">TAILNET &hellip;</span></div>
      <button class="icon-btn" id="uiSettingsBtn" type="button" title="Interface settings" aria-label="Interface settings"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M4 8h10M18 8h2M4 16h2M10 16h10"/><circle cx="16" cy="8" r="2.2"/><circle cx="8" cy="16" r="2.2"/></svg></button>
      <a class="icon-btn" href="/pc/settings" title="Device settings" aria-label="Device settings"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><circle cx="12" cy="12" r="3"/><path d="M12 2v3m0 14v3M2 12h3m14 0h3M4.9 4.9l2.1 2.1m10 10 2.1 2.1M19.1 4.9 17 7m-10 10-2.1 2.1"/></svg></a>
      <a class="icon-btn" href="/pc/security" title="Security" aria-label="Security"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/></svg></a>
    </div>
  </header>
  <div class="layout">
    <aside class="sidebar" aria-label="Main navigation">
      <div class="nav-label">Workspace</div>
      <a class="nav active" href="#overview" title="Overview"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/></svg><span>Overview</span></a>
      <a class="nav" href="#pc-control" title="PC Control"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8m-4-4v4"/></svg><span>PC Control</span></a>
      <a class="nav" href="#relay-control" title="Relay Control"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M5 12h4m6 0h4M9 8l6 8m-6 0 6-8"/><circle cx="5" cy="12" r="2"/><circle cx="19" cy="12" r="2"/><circle cx="12" cy="12" r="2"/></svg><span>Relay Control</span></a>
      <a class="nav" href="#gpio-map" title="GPIO Map"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="7" y="7" width="10" height="10" rx="2"/><path d="M9 2v5m6-5v5M9 17v5m6-5v5M2 9h5m-5 6h5m10-6h5m-5 6h5"/></svg><span>GPIO Map</span></a>
      <a class="nav" href="#hid-login" title="USB HID login"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="2" y="7" width="20" height="12" rx="2"/><path d="M6 11h.01M10 11h.01M14 11h.01M18 11h.01M7 15h10"/></svg><span>USB HID</span></a>
      <a class="nav" href="/pc/settings" title="Device settings"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><circle cx="12" cy="12" r="3"/><path d="M12 2v3m0 14v3M2 12h3m14 0h3M4.9 4.9l2.1 2.1m10 10 2.1 2.1M19.1 4.9 17 7m-10 10-2.1 2.1"/></svg><span>Settings</span></a>
      <a class="nav" href="/pc/security" title="Security"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/></svg><span>Security</span></a>
      <div class="sidebar-foot"><strong>Linked device</strong><span id="hdrDev">&hellip;</span></div>
    </aside>
    <main id="overview">
      <div id="authBanner" class="banner warn hidden"><span id="authMsg"></span><button class="soft-btn" id="btnTokOpen" type="button">Set token</button></div>
      <div id="staleBanner" class="banner err hidden"><span>&#9888; Status is not updating (last refresh failed). Values may be stale.</span></div>
      <div class="page-heading">
        <div><div class="eyebrow">SYSTEM OVERVIEW</div><h2>Remote access.<br>At your command.</h2><p class="subtitle">Front-panel control and relay channels, live over the tunnel.</p></div>
        <button class="outline-btn" id="refreshBtn" type="button"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M20 7v5h-5M4 17v-5h5"/><path d="M5.5 9a7 7 0 0 1 11.7-2L20 12M4 12l2.8 5a7 7 0 0 0 11.7-2"/></svg>Refresh</button>
      </div>
      <section class="hero-grid">
        <article class="glass hero">
          <div class="card-top"><div class="section-tag"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8m-4-4v4"/></svg> PC STATE</div><span class="status-chip mid" id="pcChip"><span class="dot"></span><span id="pcChipTxt">unknown</span></span></div>
          <div class="hero-body">
            <div>
              <h3 id="vPc">&hellip;</h3>
              <p class="hero-copy" id="dPc">waiting for the first status poll</p>
              <div class="eyebrow" id="activeLine" style="margin:0 0 12px"><span id="activeSpin"></span><span id="activeText"></span></div>
              <div class="hero-buttons">
                <button class="primary-btn" id="q-POWER_ON" type="button"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 2v10"/><path d="M5.2 5.8a9 9 0 1 0 13.6 0"/></svg>Power On</button>
                <button class="soft-btn" id="q-WAKE_UP" type="button"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M12 3v9l6 3"/><circle cx="12" cy="12" r="9"/></svg>Wake Up</button>
              </div>
            </div>
            <div class="power-orb" aria-hidden="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M12 2v10"/><path d="M6.1 5.8a8 8 0 1 0 11.8 0"/></svg></div>
          </div>
        </article>
        <article class="glass metric-panel">
          <div class="card-top"><div><h3>System snapshot</h3><div class="small-copy">Live device values</div></div><span class="count">LIVE</span></div>
          <div class="metrics">
            <div class="metric"><div class="metric-head">Relay channels <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M5 12h4m6 0h4m-9-4 6 8m-6 0 6-8"/></svg></div><div class="metric-value" id="mChannels">0</div><div class="metric-foot" id="fChannels">configured</div></div>
            <div class="metric"><div class="metric-head">Outputs ON <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="m5 12 4 4L19 6"/></svg></div><div class="metric-value" id="mOn">0</div><div class="metric-foot">commanded state</div></div>
            <div class="metric"><div class="metric-head">GPIO assigned <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="7" y="7" width="10" height="10" rx="2"/><path d="M9 2v5m6-5v5M9 17v5m6-5v5M2 9h5m-5 6h5m10-6h5m-5 6h5"/></svg></div><div class="metric-value" id="mGpio">0</div><div class="metric-foot">relay channels</div></div>
            <div class="metric"><div class="metric-head">Link state <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M10 13a5 5 0 0 0 7.1 0l3-3a5 5 0 0 0-7.1-7.1l-1.7 1.7"/><path d="M14 11a5 5 0 0 0-7.1 0l-3 3A5 5 0 0 0 11 21.1l1.7-1.7"/></svg></div><div class="metric-value" id="mLink" style="font-size:19px">?</div><div class="metric-foot" id="fLink">tailnet</div></div>
          </div>
        </article>
      </section>
      <section class="content-section" id="pc-control">
        <div class="section-heading"><div><h3>PC Control</h3><p>Each action pulses the motherboard front-panel inputs through the real GPIO stage.</p></div><span class="count">4 ACTIONS</span></div>
        <div class="pc-grid" id="cmdGrid"></div>
        <p class="hint" id="cdHint"></p>
        <p class="hint" id="lastHint"></p>
      </section>

      <section class="content-section" id="relay-control">
        <div class="section-heading"><div><h3>Relay Control</h3><p>Explicit ON/OFF per channel &middot; the page shows the commanded state only (no contact feedback).</p></div>
          <div class="relay-toolbar"><span class="count" id="relayCountLabel">0 CHANNELS</span><button class="primary-btn" id="addRelay" type="button"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 5v14M5 12h14"/></svg>Add channel</button></div>
        </div>
        <div class="relay-grid" id="relayGrid"></div>
        <article class="glass empty-card" id="relayEmpty"><div class="small-copy">No relay channels configured yet - use <b>Add channel</b> here or set the channel count on the <a href="/pc/settings">Settings</a> page.</div></article>
      </section>
      <section class="lower-grid">
        <article class="glass lower-card" id="gpio-map">
          <div class="card-top"><div><h3>GPIO mapping</h3><div class="desc">Board-verified pins only, enforced by the firmware.</div></div><span class="count">READ ONLY</span></div>
          <table class="gpio-table"><thead><tr><th>Output</th><th>GPIO</th><th>Level</th><th>Enabled</th></tr></thead><tbody id="gpioTable"></tbody></table>
        </article>
        <article class="glass lower-card">
          <div class="card-top"><div><h3>Activity log</h3><div class="desc">This browser session only.</div></div><span class="count">SESSION</span></div>
          <div class="event-list" id="logList"></div>
        </article>
      </section>

      <section class="content-section" id="hid-login">
        <div class="section-heading"><div><h3>USB HID login</h3><p>Types the password through the ESP32 native USB port into the focused window of the target PC.</p></div></div>
        <article class="glass lower-card">
          <div class="small-copy" id="hidState">HID: loading...</div>
          <div class="form-grid" style="margin-top:12px">
            <div class="field full"><label for="hidPw">Windows password (RAM only, never stored)</label><input id="hidPw" type="password" autocomplete="new-password" spellcheck="false" placeholder="password" disabled></div>
            <div class="field full"><div class="switch-row"><span>Press Enter after typing</span><button type="button" class="switch" id="hidEnterSwitch" role="switch" aria-checked="true" aria-label="Press Enter after typing"><span></span></button></div><input type="checkbox" id="hidEnter" checked class="hidden"></div>
          </div>
          <div class="row" style="margin-top:12px"><button class="primary-btn" id="hidGo" type="button" disabled>Type password</button><button class="soft-btn" id="hidCancel" type="button" disabled>Cancel</button><span class="hint" id="hidMsg" style="margin:0"></span></div>
          <p class="form-note">This reports that keystrokes were sent - it cannot know whether Windows accepted the password (screen state, keyboard layout and other factors are invisible from here).</p>
        </article>
      </section>

      <footer class="footer"><div class="safe"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 22s8-4 8-11V5l-8-3-8 3v6c0 7 8 11 8 11Z"/><path d="m9 12 2 2 4-4"/></svg>Commands drive the real front-panel GPIO stage; PC power state is reported as unknown without a sense input.</div><div>Control Deck &middot; MicroLink &middot; ISUI dark glass</div></footer>
    </main>
  </div>
</div>

<div class="modal-backdrop" id="modal" role="dialog" aria-modal="true" aria-labelledby="mTitle"><div class="modal">
  <div class="modal-head"><h3 id="mTitle">Confirm</h3><button class="icon-btn" id="mNoX" type="button" aria-label="Close">&#10005;</button></div>
  <p id="mBody"></p>
  <div class="field full"><input id="mInput" class="hidden" autocomplete="off" spellcheck="false" placeholder=""></div>
  <div class="modal-actions"><button class="soft-btn" id="mNo" type="button">Cancel</button><button class="primary-btn" id="mYes" type="button" disabled>Confirm</button></div>
</div></div>

<div class="modal-backdrop" id="tokModal" role="dialog" aria-modal="true" aria-labelledby="tokTitle"><div class="modal">
  <div class="modal-head"><h3 id="tokTitle">API token</h3><button class="icon-btn" id="tokX" type="button" aria-label="Close">&#10005;</button></div>
  <p>Paste the token from <code>sdkconfig.credentials</code> or the Security page - surrounding quotes and the <code>CONFIG_PCCTL_API_TOKEN=</code> prefix are stripped automatically. It is stored only in this browser (localStorage) and sent as the <code>X-PC-Token</code> header. Browser storage blocked (private mode)? Open <code>/pc#t=TOKEN</code> instead.</p>
  <div class="field full"><input id="tokInput" autocomplete="off" spellcheck="false" placeholder="token"></div>
  <p class="form-note" id="tokMsg" style="min-height:1em"></p>
  <div class="modal-actions"><button class="soft-btn" id="tokNo" type="button">Cancel</button><button class="primary-btn" id="tokYes" type="button">Save</button></div>
</div></div>

<div class="modal-backdrop" id="relayModal" role="dialog" aria-modal="true" aria-labelledby="relayModalTitle"><div class="modal">
  <div class="modal-head"><h3 id="relayModalTitle">Configure relay</h3><button class="icon-btn" id="relayX" type="button" aria-label="Close">&#10005;</button></div>
  <input type="hidden" id="relayId">
  <div class="form-grid">
    <div class="field full"><label for="relayName">Channel name</label><input id="relayName" maxlength="32" placeholder="e.g. Desk lamp"></div>
    <div class="field"><label for="relayGpio">GPIO assignment</label><select id="relayGpio"><option value="">NC - not configured</option></select></div>
    <div class="field"><label for="relayLogic">Active level</label><select id="relayLogic"><option value="high">Active-HIGH</option><option value="low">Active-LOW</option></select></div>
    <div class="field full"><div class="switch-row"><span>Enable this channel</span><button type="button" class="switch" id="relayEnabled" role="switch" aria-checked="false" aria-label="Enable this channel"><span></span></button></div></div>
  </div>
  <p class="form-note">Validated and enforced by the firmware: allowlisted pins, unique across channels, never shared with a PC-control pin. Saving stores the configuration in NVS and drives every channel to its idle OFF level (safe start).</p>
  <p class="form-note" id="relayFormMsg" style="min-height:1em;color:#fda4af"></p>
  <div class="modal-actions"><button class="soft-btn" id="relayCancel" type="button">Cancel</button><button class="primary-btn" id="relaySave" type="button">Save channel</button></div>
</div></div>

<div class="modal-backdrop" id="settingsModal" role="dialog" aria-modal="true" aria-labelledby="uiSetTitle"><div class="modal">
  <div class="modal-head"><h3 id="uiSetTitle">Interface settings</h3><button class="icon-btn" id="settingsX" type="button" aria-label="Close">&#10005;</button></div>
  <div class="settings-list">
    <div class="setting-row"><div><strong>Parallax depth</strong><small>Subtle pointer movement on desktop</small></div><button type="button" class="switch" id="parallaxSwitch" role="switch" aria-checked="true" aria-label="Toggle parallax"><span></span></button></div>
    <div class="setting-row"><div><strong>Glass blur</strong><small>Translucent card backdrop effects</small></div><button type="button" class="switch" id="blurSwitch" role="switch" aria-checked="true" aria-label="Toggle glass blur"><span></span></button></div>
  </div>
  <p class="form-note">Local preferences for this browser only; they are never sent to the device.</p>
  <div class="modal-actions"><button class="primary-btn" id="settingsDone" type="button">Done</button></div>
</div></div>
</body>
)PCM";

/* ---- /pc : control-deck page script ---- */

static const char PCCTL_PAGE_SCRIPT[] = R"PCJS(
'use strict';
const $ = function(id){ return document.getElementById(id); };
const esc = function(v){ return String(v == null ? '' : v).replace(/[&<>"']/g, function(c){ return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]; }); };
const CMDS = [
  { id:'POWER_ON', label:'Power On', desc:'Start the PC (PWR_SW pulse)', confirm:'soft',
    icon:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 2v10"/><path d="M5.2 5.8a9 9 0 1 0 13.6 0"/></svg>' },
  { id:'WAKE_UP', label:'Wake Up', desc:'Short PWR_SW press (wake)', confirm:'soft',
    icon:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M12 3v9l6 3"/><circle cx="12" cy="12" r="9"/></svg>' },
  { id:'PWR_RESET', label:'PWR Reset', desc:'Hardware RESET_SW pulse', confirm:'hard',
    icon:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M4 7h16M7 4v6m10-6v6"/><rect x="4" y="7" width="16" height="13" rx="2"/><path d="M9 13h6"/></svg>' },
  { id:'FORCE_POWER_OFF', label:'Force Power Off', desc:'Hold PWR_SW (frozen PC)', confirm:'strong',
    icon:'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M12 3 2.8 20h18.4L12 3Z"/><path d="M12 9v5m0 3h.01"/></svg>' }
];
let st = null, fails = 0, pollTimer = null, busy = false, memTok = '';
const relayPending = {};
let relaySig = '', allowedGpios = null, pcCfg = null, relaySaveBusy = false;
function sanitizeTok(v){
  v = String(v || '').trim();
  v = v.replace(/^CONFIG_PCCTL_API_TOKEN\s*=\s*/, '');
  v = v.replace(/^[\s"']+|[\s"']+$/g, '');
  return v;
}
function tok(){ try { const v = localStorage.getItem('pcctl_token'); if (v) return v; } catch(e){} return memTok; }
function storeTok(v){ memTok = v; try { localStorage.setItem('pcctl_token', v); return true; } catch(e){ return false; } }
function genId(){ return 'c-ui-' + Date.now().toString(16) + '-' + Math.random().toString(16).slice(2,8); }
function fmtAge(v){ return (v===null||v===undefined) ? 'never' : (v<10 ? v.toFixed(1) : Math.round(v)) + ' s ago'; }
function nowTime(){
  const d = new Date();
  return String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0')+':'+String(d.getSeconds()).padStart(2,'0');
}
function log(msg, kind){
  const list = $('logList');
  const el = document.createElement('div');
  el.className = 'event';
  const ic = kind === 'err'
    ? '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M12 3 2.8 20h18.4L12 3Z"/><path d="M12 9v5m0 3h.01"/></svg>'
    : kind === 'tx'
      ? '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M5 12h14m-6-6 6 6-6 6"/></svg>'
      : '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="m5 12 4 4L19 6"/></svg>';
  el.innerHTML = '<span class="event-icon">' + ic + '</span><div><strong>Activity</strong><small></small></div><time>' + nowTime() + '</time>';
  el.querySelector('small').textContent = msg;
  list.insertBefore(el, list.firstChild);
  while (list.children.length > 20) list.removeChild(list.lastChild);
}
async function fetchJson(path, opts){
  opts = opts || {};
  const headers = opts.headers || {};
  if (tok()) headers['X-PC-Token'] = tok();
  opts.headers = headers;
  try {
    const r = await fetch(path, opts);
    let j = null;
    try { j = await r.json(); } catch(e){}
    return {status:r.status, json:j};
  } catch (e) {
    return {status:0, json:null, error:String(e)};
  }
}
function buildButtons(){
  const g = $('cmdGrid');
  if (!g) return;
  g.innerHTML = '';
  CMDS.forEach(function(c){
    const el = document.createElement('article');
    el.className = 'glass action-card' + (c.confirm === 'strong' ? ' danger' : '');
    el.innerHTML = '<div class="action-icon">' + c.icon + '</div><h4>' + c.label + '</h4><p>' + c.desc + '</p>' +
      '<div class="action-bottom"><span class="mini-status" id="ms-' + c.id + '"><span class="dot"></span><span id="msTxt-' + c.id + '">checking</span></span>' +
      '<button class="run-btn" id="btn-' + c.id + '" type="button" aria-label="' + c.label + '"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M5 12h14m-6-6 6 6-6 6"/></svg></button></div>';
    g.appendChild(el);
    el.querySelector('.run-btn').addEventListener('click', function(){ onCommand(c); });
  });
}
function commandDisabled(){
  if (busy) return 'another request is in flight';
  if (st && st.active_command) return 'a command is already running';
  return null;
}
function render(s){
  st = s;
  const esp = s.esp32 || {};
  $('hdrDev').textContent = (esp.hostname || '') + (esp.vpn_ip ? '  (' + esp.vpn_ip + ')' : '');
  const espOk = esp.state === 'CONNECTED';
  const bn = $('bNet');
  bn.className = 'connection' + (espOk ? '' : ((esp.state === 'ERROR' || esp.state === 'AUTH_FAILED') ? ' bad' : ' warn'));
  $('netTxt').textContent = espOk ? 'TAILNET OK' : ('TAILNET ' + (esp.state || '?'));
  const pc = s.pc || {};
  const chip = $('pcChip');
  chip.className = 'status-chip' + (pc.state === 'online' ? '' : (pc.state === 'offline' ? ' bad' : ' mid'));
  $('pcChipTxt').textContent = pc.state || 'unknown';
  $('vPc').textContent = (pc.state || 'unknown') === 'unknown' ? 'PC state: unknown' : ('PC is ' + pc.state);
  $('dPc').textContent = 'source: ' + (pc.source || 'none') + ' - ' + (pc.note || '');
  const ac = s.active_command;
  if (ac) {
    $('activeLine').style.display = '';
    $('activeSpin').innerHTML = (ac.state === 'accepted' || ac.state === 'running') ? '<span class="spin"></span>' : '';
    $('activeText').textContent = ac.command + ' - ' + ac.state + '  ' + (ac.detail || '') + '  [' + ac.request_id + ']';
  } else {
    $('activeLine').style.display = 'none';
  }
  const lc = s.last_command;
  $('lastHint').textContent = lc ? ('Last command: ' + lc.command + ' ' + lc.state + (lc.detail ? ' - ' + lc.detail : '') + '  (' + fmtAge(lc.age_s) + ')') : '';
  const rels = (s.relays && s.relays.channels) || [];
  const relCount = (s.relays && s.relays.channel_count) || 0;
  $('mChannels').textContent = String(relCount).padStart(2, '0');
  $('fChannels').textContent = (s.relays && s.relays.max_channels) ? ('of ' + s.relays.max_channels + ' max') : '';
  $('mOn').textContent = String(rels.filter(function(c){ return c.state === 'on'; }).length).padStart(2, '0');
  $('mGpio').textContent = String(rels.filter(function(c){ return c.gpio !== null; }).length).padStart(2, '0');
  const ml = $('mLink');
  ml.textContent = esp.state || '?';
  ml.style.color = espOk ? 'var(--green)' : 'var(--amber)';
  $('fLink').textContent = 'wifi ' + (esp.wifi_ip || '-') + ' - vpn ' + (esp.vpn_ip || '-');
  $('relayCountLabel').textContent = relCount + ' CHANNEL' + (relCount === 1 ? '' : 'S');
  renderRelays(rels);
  renderGpioTable(rels);
  const why = commandDisabled();
  updateMiniStatus(why);
  CMDS.forEach(function(c){
    const b = $('btn-' + c.id);
    if (b) { b.disabled = !!why; b.title = why || ''; }
  });
  ['POWER_ON', 'WAKE_UP'].forEach(function(id){
    const q = $('q-' + id);
    if (q) { q.disabled = !!why; q.title = why || ''; }
  });
  const cdms = (s.hw_cooldown_ms && s.hw_cooldown_ms > 0) ? s.hw_cooldown_ms : 0;
  $('cdHint').textContent = cdms > 0 ? ('Cooldown: ' + Math.ceil(cdms / 1000) + ' s remaining - a press now is refused until it expires.') : '';
  const auth = s.auth || {};
  const ab = $('authBanner');
  if (auth.mutations_allowed === false) {
    ab.classList.remove('hidden');
    $('authMsg').textContent = 'Mutations are disabled: no API token configured in the firmware (CONFIG_PCCTL_API_TOKEN).';
  } else if (auth.token_configured && !tok()) {
    ab.classList.remove('hidden');
    $('authMsg').textContent = 'No token stored in this browser yet - set it before sending commands.';
  } else {
    ab.classList.add('hidden');
  }
  renderHid(s.hid || {});
  ensurePcCfg();
}
function updateMiniStatus(why){
  CMDS.forEach(function(c){
    const ms = $('ms-' + c.id), txt = $('msTxt-' + c.id);
    if (!ms || !txt) return;
    ms.className = 'mini-status' + (why ? ' warn' : ' ok');
    txt.textContent = why ? 'busy' : 'ready';
  });
}
async function ensurePcCfg(){
  if (pcCfg || !st) return;
  const r = await fetchJson('/api/v1/config');
  if (r.status === 200 && r.json && r.json.gpios) {
    pcCfg = r.json;
    if (st) renderGpioTable((st.relays && st.relays.channels) || []);
  }
}
function renderGpioTable(rels){
  const tb = $('gpioTable');
  if (!tb) return;
  const rows = [];
  const fnDefs = [
    ['POWER_ON', 'PWR_SW pulse'],
    ['WAKE_UP', 'PWR_SW wake'],
    ['PWR_RESET', 'RESET_SW pulse'],
    ['FORCE_POWER_OFF', 'PWR_SW hold']
  ];
  fnDefs.forEach(function(f){
    const name = f[0];
    const pin = (pcCfg && pcCfg.gpios && pcCfg.gpios[name] != null) ? pcCfg.gpios[name] : null;
    const lvlLow = !!(pcCfg && pcCfg.gpio_levels && pcCfg.gpio_levels[name] === 'low');
    const en = !!(pcCfg && pcCfg.functions && pcCfg.functions[name]);
    rows.push('<tr><td>' + name + ' <span class="small-copy">(' + f[1] + ')</span></td>' +
      '<td class="pin">' + (pin === null ? 'NC' : ('GPIO' + pin)) + '</td>' +
      '<td><span class="logic' + (lvlLow ? ' low' : '') + '">Active-' + (lvlLow ? 'LOW' : 'HIGH') + '</span></td>' +
      '<td style="color:' + (en ? 'var(--green)' : '#8498ae') + '">' + (en ? 'Yes' : 'No') + '</td></tr>');
  });
  (rels || []).forEach(function(c){
    rows.push('<tr><td>' + esc(c.name || ('Relay ' + (c.id + 1))) + '</td>' +
      '<td class="pin">' + (c.gpio === null ? 'NC' : ('GPIO' + c.gpio)) + '</td>' +
      '<td><span class="logic' + (c.active_level === 'low' ? ' low' : '') + '">Active-' + (c.active_level === 'low' ? 'LOW' : 'HIGH') + '</span></td>' +
      '<td style="color:' + (c.enabled ? 'var(--green)' : '#8498ae') + '">' + (c.enabled ? 'Yes' : 'No') + '</td></tr>');
  });
  tb.innerHTML = rows.join('');
}
function renderRelays(rels){
  const grid = $('relayGrid');
  const empty = $('relayEmpty');
  empty.classList.toggle('hidden', rels.length > 0);
  grid.classList.toggle('hidden', rels.length === 0);
  const sig = rels.map(function(c){ return [c.id, c.name, c.gpio, c.active_level, c.enabled].join(':'); }).join('|');
  if (sig !== relaySig) {
    relaySig = sig;
    grid.innerHTML = rels.map(function(c){
      const on = c.state === 'on';
      const unavailable = !c.enabled || c.gpio === null;
      const label = esc(c.name || ('Relay ' + (c.id + 1)));
      return '<article class="glass relay-card" data-relay="' + c.id + '">' +
        '<div class="relay-head"><div class="relay-name-wrap"><div class="relay-icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M5 12h4m6 0h4m-9-4 6 8m-6 0 6-8"/><circle cx="5" cy="12" r="2"/><circle cx="19" cy="12" r="2"/><circle cx="12" cy="12" r="2"/></svg></div>' +
        '<div style="min-width:0"><div class="relay-name">' + label + '</div><div class="relay-id">CHANNEL ' + (c.id + 1) + '</div></div></div>' +
        '<div class="state-label' + (on ? ' on' : '') + '" id="rStateLabel-' + c.id + '"><span class="dot"></span><span id="rStateTxt-' + c.id + '">' + (on ? 'ON' : (unavailable ? 'UNAVAILABLE' : 'OFF')) + '</span></div></div>' +
        '<div class="relay-mid"><div class="relay-state' + (on ? ' on' : '') + '" id="rState-' + c.id + '">' + (on ? 'ON' : 'OFF') + '<small>commanded state</small></div>' +
        '<button class="switch" type="button" role="switch" aria-checked="' + (on ? 'true' : 'false') + '" aria-label="Toggle ' + label + '" data-toggle="' + c.id + '"' + (unavailable ? ' disabled title="Channel is disabled or GPIO is NC"' : '') + '><span></span></button></div>' +
        '<div class="relay-meta"><span class="meta-chip"><b>' + (c.gpio === null ? 'NC' : ('GPIO' + c.gpio)) + '</b></span>' +
        '<span class="meta-chip"><span class="logic' + (c.active_level === 'low' ? ' low' : '') + '">Active-' + (c.active_level === 'low' ? 'LOW' : 'HIGH') + '</span></span>' +
        '<span class="meta-chip">' + (c.enabled ? 'Enabled' : 'Disabled') + '</span>' +
        '<span class="meta-chip" id="rWhy-' + c.id + '" style="display:none"></span></div>' +
        '<div class="relay-actions"><button class="on-btn" type="button" data-set="' + c.id + ':ON"' + (unavailable ? ' disabled' : '') + '>Turn ON</button>' +
        '<button class="off-btn" type="button" data-set="' + c.id + ':OFF"' + (unavailable ? ' disabled' : '') + '>Turn OFF</button>' +
        '<button class="off-btn config-btn" type="button" data-config="' + c.id + '" aria-label="Configure ' + label + '" title="Configure channel"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="m14 5 5 5M4 20l4-.8L19 8a2.1 2.1 0 0 0-3-3L5 16l-1 4Z"/></svg></button></div>' +
        '<p class="hint" id="rMsg-' + c.id + '" style="min-height:1em"></p></article>';
    }).join('');
  }
  rels.forEach(function(c){
    const on = c.state === 'on';
    const unavailable = !c.enabled || c.gpio === null;
    const sv = $('rState-' + c.id);
    if (sv) { sv.className = 'relay-state' + (on ? ' on' : ''); sv.innerHTML = (on ? 'ON' : 'OFF') + '<small>commanded state</small>'; }
    const stl = $('rStateLabel-' + c.id);
    if (stl) stl.className = 'state-label' + (on ? ' on' : '');
    const stt = $('rStateTxt-' + c.id);
    if (stt) stt.textContent = on ? 'ON' : (unavailable ? 'UNAVAILABLE' : 'OFF');
    const why = $('rWhy-' + c.id);
    if (why) {
      if (!c.available && c.unavailable_reason) { why.style.display = ''; why.textContent = c.unavailable_reason; }
      else why.style.display = 'none';
    }
    const inflight = !!relayPending[c.id];
    const can = !!c.available && !inflight;
    const tip = inflight ? 'sending...' : (c.available ? '' : (c.unavailable_reason || 'unavailable'));
    const sw = grid.querySelector('[data-toggle="' + c.id + '"]');
    if (sw) { sw.setAttribute('aria-checked', on ? 'true' : 'false'); sw.disabled = !can; sw.title = tip; }
    grid.querySelectorAll('[data-set="' + c.id + ':ON"],[data-set="' + c.id + ':OFF"]').forEach(function(b){
      b.disabled = !can;
      b.title = tip;
    });
  });
}
let modalCb = null;
function openModal(title, body, needType, cb, goLabel){
  $('mTitle').textContent = title;
  $('mBody').innerHTML = body;
  const inp = $('mInput');
  if (needType) {
    inp.classList.remove('hidden');
    inp.value = '';
    inp.placeholder = 'type ' + needType + ' to enable the button';
    inp.oninput = function(){ $('mYes').disabled = (inp.value.trim() !== needType); };
    $('mYes').disabled = true;
  } else {
    inp.classList.add('hidden');
    inp.oninput = null;
    $('mYes').disabled = false;
  }
  $('mYes').textContent = goLabel || 'Confirm';
  modalCb = cb;
  $('modal').classList.add('open');
}
function closeModal(){ $('modal').classList.remove('open'); modalCb = null; }
$('mNo').addEventListener('click', closeModal);
$('mNoX').addEventListener('click', closeModal);
$('modal').addEventListener('click', function(e){ if (e.target === $('modal')) closeModal(); });
$('mYes').addEventListener('click', function(){ const cb = modalCb; closeModal(); if (cb) cb(); });

function onCommand(c){
  if (busy || (st && st.active_command)) return;
  if (c.confirm === 'strong') {
    openModal('Force power off?',
      'This holds the motherboard PWR_SW input to force a frozen PC off. It bypasses the operating system and can cause data loss. This is a real hardware action.<br><br>Type <code>' + c.id + '</code> to enable the button.',
      c.id, function(){ doSubmit(c, c.id); }, 'Force power off');
  } else if (c.confirm === 'hard') {
    openModal('PWR Reset?',
      'This pulses the motherboard RESET_SW input - an immediate hardware reset. Unsaved work will be lost. This is a real hardware action.<br><br>Type <code>' + c.id + '</code> to enable the button.',
      c.id, function(){ doSubmit(c, c.id); }, 'Pulse reset');
  } else if (c.confirm === 'soft') {
    const msg = (c.id === 'POWER_ON')
      ? 'This pulses the motherboard PWR_SW input to start the PC. Confirm to avoid an accidental start.'
      : 'This pulses the motherboard PWR_SW input to wake the PC from sleep. Confirm to avoid an accidental press.';
    openModal(c.label + '?', msg, null, function(){ doSubmit(c, c.id); }, c.label);
  } else {
    doSubmit(c, c.id);
  }
}
async function doSubmit(c, confirmValue){
  if (busy) return;
  busy = true;
  if (st) render(st);
  const body = { command: c.id, request_id: genId() };
  if (confirmValue) body.confirm = confirmValue;
  try {
    const r = await fetchJson('/api/v1/commands', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    const j = r.json || {};
    if (r.status === 202) {
      log('[' + body.request_id + '] ' + c.id + ' accepted', 'tx');
    } else if (r.status === 200 && j.replay) {
      log(c.id + ' already known - state: ' + (j.command ? j.command.state : '?'));
    } else if (r.status === 401) {
      log(c.id + ' rejected: ' + (j.error || r.status) + ' - set the API token', 'err');
      openTok();
    } else if (r.status === 409 && j.error === 'cooldown') {
      log(c.id + ' not sent - cooldown active, wait ' + Math.ceil((j.retry_after_ms || 0) / 1000) + ' s before pressing again', 'err');
    } else {
      let m = c.id + ' rejected (' + r.status + '): ' + (j.error || r.error || '?');
      if (j.detail) m += ' - ' + j.detail;
      if (j.retry_after_ms) m += ' (retry in ' + j.retry_after_ms + ' ms)';
      if (j.active_command) m += ' [active: ' + j.active_command + ']';
      log(m, 'err');
    }
  } catch (e) {
    log(c.id + ' request failed: ' + e, 'err');
  }
  busy = false;
  refresh();
}

/* relay cards: delegated switch / ON / OFF / configure */
$('relayGrid').addEventListener('click', function(e){
  const t = e.target;
  const sw = t.closest('[data-toggle]');
  if (sw) { const c = currentRelay(parseInt(sw.dataset.toggle, 10)); if (c) relaySet(c.id, c.state !== 'on'); return; }
  const set = t.closest('[data-set]');
  if (set) { const parts = set.dataset.set.split(':'); relaySet(parseInt(parts[0], 10), parts[1] === 'ON'); return; }
  const cfg = t.closest('[data-config]');
  if (cfg) openRelayConfig(parseInt(cfg.dataset.config, 10));
});
function currentRelay(id){
  const rels = (st && st.relays && st.relays.channels) || [];
  for (let i = 0; i < rels.length; i++) if (rels[i].id === id) return rels[i];
  return null;
}
async function relaySet(id, on){
  if (relayPending[id]) return;
  const c = currentRelay(id);
  if (!c || !c.available) return;
  relayPending[id] = true;
  if (st) render(st);
  const m = $('rMsg-' + id);
  if (m) m.textContent = 'Sending ' + (on ? 'ON' : 'OFF') + '...';
  try {
    const r = await fetchJson('/api/v1/relays', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ channel: id, state: on ? 'on' : 'off' })
    });
    const j = r.json || {};
    if (r.status === 200 && j.ok) {
      const note = (on ? 'ON' : 'OFF') + ' commanded' + (j.changed === false ? ' (was already ' + (on ? 'on' : 'off') + ')' : '') + '.';
      if (m) m.textContent = note;
      log('relay ' + id + ' -> ' + (on ? 'ON' : 'OFF') + ' commanded', 'tx');
      isuiToast('Relay ' + (id + 1) + ': ' + note);
    } else if (r.status === 401) {
      if (m) m.textContent = 'Rejected: API token missing or invalid.';
      openTok();
    } else {
      let msg = 'Rejected (' + r.status + '): ' + (j.error || r.error || '?');
      if (j.error === 'channel_disabled') msg = 'Rejected: channel disabled in Settings.';
      else if (j.error === 'gpio_not_configured') msg = 'Rejected: no GPIO configured (NC).';
      else if (j.error === 'unknown_channel') msg = 'Rejected: unknown channel.';
      else if (j.detail) msg += ' - ' + j.detail;
      if (m) m.textContent = msg;
      log('relay ' + id + ' rejected: ' + (j.error || r.status), 'err');
    }
  } catch (e) {
    if (m) m.textContent = 'Request failed: ' + e;
  }
  relayPending[id] = false;
  refresh();
}
function setSwitch(el, on){ el.setAttribute('aria-checked', on ? 'true' : 'false'); }
function switchOn(el){ return el.getAttribute('aria-checked') === 'true'; }
async function openRelayConfig(id){
  const c = currentRelay(id);
  if (!c) return;
  if (!allowedGpios) {
    const r = await fetchJson('/api/v1/relays');
    if (r.status === 200 && r.json && r.json.relays && r.json.allowed_gpios) allowedGpios = r.json.allowed_gpios;
  }
  const sel = $('relayGpio');
  sel.innerHTML = '<option value="">NC - not configured</option>' +
    (allowedGpios || []).map(function(p){ return '<option value="' + p + '">GPIO' + p + '</option>'; }).join('');
  $('relayModalTitle').textContent = 'Configure relay ' + (id + 1);
  $('relayId').value = id;
  $('relayName').value = c.name || '';
  sel.value = (c.gpio === null) ? '' : String(c.gpio);
  $('relayLogic').value = (c.active_level === 'low') ? 'low' : 'high';
  setSwitch($('relayEnabled'), !!c.enabled);
  $('relayFormMsg').textContent = '';
  $('relayModal').classList.add('open');
  $('relayName').focus();
}
function closeRelayConfig(){ $('relayModal').classList.remove('open'); }
$('relayEnabled').addEventListener('click', function(){ setSwitch(this, !switchOn(this)); });
$('relayX').addEventListener('click', closeRelayConfig);
$('relayCancel').addEventListener('click', closeRelayConfig);
$('relayModal').addEventListener('click', function(e){ if (e.target === $('relayModal')) closeRelayConfig(); });
$('relaySave').addEventListener('click', saveRelayConfig);
async function saveRelayConfig(){
  if (relaySaveBusy) return;
  const id = parseInt($('relayId').value, 10);
  if (isNaN(id)) return;
  const gv = $('relayGpio').value;
  const patch = {
    channels: [{
      id: id,
      name: $('relayName').value,
      enabled: switchOn($('relayEnabled')),
      gpio: (gv === '') ? null : parseInt(gv, 10),
      active_level: $('relayLogic').value,
      startup_policy: 'off'
    }]
  };
  relaySaveBusy = true;
  $('relaySave').disabled = true;
  $('relayFormMsg').textContent = 'Saving...';
  const r = await fetchJson('/api/v1/relays/config', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(patch)
  });
  relaySaveBusy = false;
  $('relaySave').disabled = false;
  const j = r.json || {};
  if (r.status === 200 && j.ok) {
    $('relayFormMsg').textContent = '';
    closeRelayConfig();
    isuiToast('Relay ' + (id + 1) + ' saved - all relays driven OFF (safe start).');
    log('relay ' + id + ' configuration saved', 'tx');
    refresh();
  } else if (r.status === 401) {
    $('relayFormMsg').textContent = 'Rejected: API token missing or invalid.';
    openTok();
  } else {
    $('relayFormMsg').textContent = 'Save failed (' + r.status + '): ' + (j.detail || j.error || r.error || '?');
  }
}
async function addRelay(){
  if (!st || !st.relays || !st.relays.max_channels) return;
  const rc = st.relays;
  if (rc.channel_count >= rc.max_channels) {
    isuiToast('Channel limit reached (max ' + rc.max_channels + ').', true);
    return;
  }
  const newId = rc.channel_count;
  const r = await fetchJson('/api/v1/relays/config', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ channel_count: newId + 1 })
  });
  const j = r.json || {};
  if (r.status === 200 && j.ok) {
    log('relay channel ' + (newId + 1) + ' added', 'tx');
    await refresh();
    openRelayConfig(newId);
  } else if (r.status === 401) {
    openTok();
  } else {
    isuiToast('Add failed (' + r.status + '): ' + (j.detail || j.error || '?'), true);
  }
}
$('addRelay').addEventListener('click', addRelay);
$('refreshBtn').addEventListener('click', function(){ refresh(); });
async function refresh(){
  if (pollTimer) { clearTimeout(pollTimer); pollTimer = null; }
  try {
    const r = await fetchJson('/api/v1/status');
    if (r.status === 200 && r.json && r.json.ok) {
      fails = 0;
      $('staleBanner').classList.add('hidden');
      render(r.json);
    } else {
      throw new Error('HTTP ' + r.status);
    }
  } catch (e) {
    fails++;
    if (fails >= 2) $('staleBanner').classList.remove('hidden');
  }
  schedule();
}
function schedule(){
  if (pollTimer) clearTimeout(pollTimer);
  const base = (st && st.poll_interval_s ? st.poll_interval_s : 10) * 1000;
  const mult = Math.min(Math.max(fails, 1), 8);
  let delay = document.hidden ? Math.max(base, 15000) : base * mult;
  const busyNow = Object.keys(relayPending).length > 0 || !!(st && (st.active_command || (st.hw_cooldown_ms > 0) ||
    (st.hid && (st.hid.state === 'queued' || st.hid.state === 'typing'))));
  if (!document.hidden && busyNow && fails === 0) delay = 1000;
  pollTimer = setTimeout(refresh, delay);
}
document.addEventListener('visibilitychange', function(){ if (!document.hidden) refresh(); });
function renderHid(h){
  h = h || {};
  const en = !!h.enabled, mo = !!h.mounted;
  const busyH = (h.state === 'queued' || h.state === 'typing');
  let d;
  if (!en) d = 'USB HID login: disabled in this firmware build.';
  else if (!mo) d = 'USB HID login: NOT connected - plug the ESP32 native USB port into the target PC.';
  else d = 'USB HID login: ready on the native USB port.';
  d += '  state: ' + (h.state || 'idle');
  if (h.last_error) d += '  -  last error: ' + h.last_error;
  $('hidState').textContent = d;
  const canType = en && mo && !busyH && !busy;
  $('hidPw').disabled = !canType;
  $('hidGo').disabled = !canType;
  $('hidCancel').disabled = (h.state !== 'queued');
}
$('hidEnterSwitch').addEventListener('click', function(){
  const cb = $('hidEnter');
  cb.checked = !cb.checked;
  setSwitch(this, cb.checked);
});
async function hidSubmit(){
  if (busy) return;
  const pw = $('hidPw');
  if (!pw.value) { $('hidMsg').textContent = 'Password is empty.'; return; }
  const payload = { password: pw.value, press_enter: $('hidEnter').checked };
  pw.value = '';
  busy = true;
  if (st) render(st);
  $('hidMsg').textContent = 'Submitting...';
  const r = await fetchJson('/api/v1/hid/login', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  payload.password = '';
  const j = r.json || {};
  if (r.status === 202) {
    $('hidMsg').textContent = 'Queued - typing starts shortly; Cancel works until the first keystroke.';
    log('HID login typing queued', 'tx');
  } else if (r.status === 401) {
    $('hidMsg').textContent = 'Rejected: API token missing or invalid.';
    openTok();
  } else if (j.error === 'unsupported_char') {
    $('hidMsg').textContent = 'Unsupported character at position ' + j.position + ' - printable ASCII only.';
  } else {
    let m = 'Rejected (' + r.status + '): ' + (j.error || r.error || '?');
    if (j.detail) m += ' - ' + j.detail;
    if (j.max_len) m += '  (max ' + j.max_len + ' characters)';
    $('hidMsg').textContent = m;
  }
  busy = false;
  refresh();
}
async function hidCancel(){
  const r = await fetchJson('/api/v1/hid/cancel', { method: 'POST' });
  const j = r.json || {};
  if (j.cancelled) { $('hidMsg').textContent = 'Cancelled before typing started.'; log('HID login typing cancelled'); }
  else $('hidMsg').textContent = 'Not cancelled (' + r.status + '): ' + (j.detail || j.error || '?') + (j.state ? '  [state: ' + j.state + ']' : '');
  refresh();
}
$('hidGo').addEventListener('click', hidSubmit);
$('hidCancel').addEventListener('click', hidCancel);

/* API-token modal */
function openTok(){
  $('tokInput').value = tok();
  $('tokModal').classList.add('open');
}
function closeTok(){ $('tokModal').classList.remove('open'); }
$('btnTokOpen').addEventListener('click', openTok);
$('tokX').addEventListener('click', closeTok);
$('tokNo').addEventListener('click', closeTok);
$('tokModal').addEventListener('click', function(e){ if (e.target === $('tokModal')) closeTok(); });
$('tokYes').addEventListener('click', async function(){
  const msg = $('tokMsg');
  const v = sanitizeTok($('tokInput').value);
  if (!v) { msg.textContent = 'Token is empty.'; return; }
  $('tokInput').value = v;
  const stored = storeTok(v);
  msg.textContent = 'Checking with the device...';
  const r = await fetchJson('/api/v1/commands', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command: 'PING' })
  });
  if (r.status === 200 && r.json && r.json.pong) {
    msg.textContent = stored ? 'Token accepted and saved. This browser is ready.' : 'Token accepted. Browser storage is blocked - valid for this session only (use the /pc#t=... link to restore it).';
    log('API token ' + (stored ? 'saved' : 'set for this session') + ' - device accepted it');
    isuiToast('API token accepted.');
    setTimeout(function(){ closeTok(); msg.textContent = ''; refresh(); }, 900);
  } else if (r.status === 401) {
    msg.textContent = 'Device REJECTED this token (401). Check the value - quotes/spaces are stripped automatically, so a rejection means a typo.';
  } else {
    msg.textContent = 'Could not verify (HTTP ' + r.status + (r.error ? ': ' + r.error : '') + '). Is the device reachable?';
  }
});
function applyHashToken(){
  const m = location.hash.match(/[#&]t=([^&]+)/);
  if (!m) return false;
  const v = sanitizeTok(decodeURIComponent(m[1]));
  if (!v) return false;
  storeTok(v);
  try { history.replaceState(null, '', location.pathname); } catch(e){}
  log('Token applied from URL');
  return true;
}
/* interface settings modal (local UI preferences only) */
function openUiSettings(){
  const p = isuiGetPrefs();
  setSwitch($('parallaxSwitch'), p.parallax);
  setSwitch($('blurSwitch'), p.blur);
  $('settingsModal').classList.add('open');
}
function closeUiSettings(){ $('settingsModal').classList.remove('open'); }
$('uiSettingsBtn').addEventListener('click', openUiSettings);
$('settingsX').addEventListener('click', closeUiSettings);
$('settingsDone').addEventListener('click', closeUiSettings);
$('settingsModal').addEventListener('click', function(e){ if (e.target === $('settingsModal')) closeUiSettings(); });
$('parallaxSwitch').addEventListener('click', function(){
  const on = !switchOn(this); setSwitch(this, on); isuiSetParallax(on);
  isuiToast('Parallax ' + (on ? 'enabled' : 'disabled') + (isuiGetPrefs().reduced ? ' (reduced motion is respected)' : ''));
});
$('blurSwitch').addEventListener('click', function(){
  const on = !switchOn(this); setSwitch(this, on); isuiSetBlur(on);
  isuiToast('Glass blur ' + (on ? 'enabled' : 'disabled'));
});
document.addEventListener('keydown', function(e){
  if (e.key === 'Escape') { closeModal(); closeTok(); closeRelayConfig(); closeUiSettings(); }
});

buildButtons();
applyHashToken();
window.addEventListener('hashchange', function(){ if (applyHashToken()) refresh(); });
if (!tok()) {
  $('authBanner').classList.remove('hidden');
  $('authMsg').textContent = 'No API token in this browser yet - paste it (or open /pc#t=TOKEN); commands and relay writes require it.';
}
refresh();
)PCJS";

/* ---- /pc/settings : device settings page ---- */

static const char PCCTL_SETTINGS_MARKUP[] = R"PSM(
<body>
<div class="ambient" aria-hidden="true"><div class="orb one"></div><div class="orb two"></div><div class="orb three"></div></div>
<div class="app">
  <header class="topbar">
    <div class="brand">
      <div class="logo" aria-hidden="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6"><rect x="4" y="4" width="16" height="16" rx="4"/><path d="M9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m12 0h4M2 15h4m12 0h4"/><path d="m9 12 2 2 4-5"/></svg></div>
      <div><h1>Control Deck <span class="acc">/ S3</span></h1><p>Device Settings</p></div>
    </div>
    <div class="top-actions">
      <div class="connection" id="bDev"><span class="dot"></span><span id="netTxt">&hellip;</span></div>
      <a class="icon-btn" href="/pc" title="Control deck" aria-label="Control deck"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/></svg></a>
      <a class="icon-btn" href="/pc/security" title="Security" aria-label="Security"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/></svg></a>
    </div>
  </header>
  <div class="layout">
    <aside class="sidebar" aria-label="Main navigation">
      <div class="nav-label">Workspace</div>
      <a class="nav" href="/pc#overview" title="Overview"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/></svg><span>Overview</span></a>
      <a class="nav" href="/pc#pc-control" title="PC Control"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8m-4-4v4"/></svg><span>PC Control</span></a>
      <a class="nav" href="/pc#relay-control" title="Relay Control"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M5 12h4m6 0h4M9 8l6 8m-6 0 6-8"/><circle cx="5" cy="12" r="2"/><circle cx="19" cy="12" r="2"/><circle cx="12" cy="12" r="2"/></svg><span>Relay Control</span></a>
      <a class="nav active" href="/pc/settings" title="Device settings"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><circle cx="12" cy="12" r="3"/><path d="M12 2v3m0 14v3M2 12h3m14 0h3M4.9 4.9l2.1 2.1m10 10 2.1 2.1M19.1 4.9 17 7m-10 10-2.1 2.1"/></svg><span>Settings</span></a>
      <a class="nav" href="/pc/security" title="Security"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/></svg><span>Security</span></a>
      <div class="sidebar-foot"><strong>Linked device</strong><span id="hdrDev">&hellip;</span></div>
    </aside>
    <main id="overview">
      <div id="authBanner" class="banner warn hidden"><span id="authMsg"></span></div>
      <div class="page-heading">
        <div><div class="eyebrow">DEVICE CONFIGURATION</div><h2>Settings</h2><p class="subtitle">Validated and enforced in firmware, stored in NVS on explicit saves only.</p></div>
        <a class="outline-btn" href="/pc"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M19 12H5m6 6-6-6 6-6"/></svg>Control deck</a>
      </div>

      <article class="glass lower-card">
        <div class="card-top"><div><h3>API token (this browser)</h3><div class="desc">Required for saving anything on this page.</div></div><span class="count">LOCAL</span></div>
        <div class="row"><input class="txt" id="apiTok" placeholder="paste API token" autocomplete="off" spellcheck="false" style="max-width:420px"><button class="primary-btn" id="apiTokSave" type="button">Save</button><span class="hint" id="apiTokMsg" style="margin:0"></span></div>
      </article>

      <section class="content-section">
        <div class="section-heading"><div><h3>Function permissions</h3><p>Independent Enable flag per PC-control function.</p></div></div>
        <article class="glass lower-card" id="funcList"></article>
      </section>
      <section class="content-section">
        <div class="section-heading"><div><h3>GPIO mapping</h3><p>Only board-verified pins are listed. PWR_RESET must not share a pin with a PWR_SW function; slots sharing a pin must use the same active level.</p></div></div>
        <article class="glass lower-card" id="gpioList"></article>
      </section>

      <section class="content-section">
        <div class="section-heading"><div><h3>Press timings</h3><p>Pulse widths for the front-panel actions.</p></div></div>
        <article class="glass lower-card">
          <div class="form-grid">
            <div class="field"><label>Short press <span id="shortRange"></span></label><input id="shortMs" type="number"></div>
            <div class="field"><label>Long press (FORCE_POWER_OFF hold) <span id="longRange"></span></label><input id="longMs" type="number"></div>
          </div>
        </article>
      </section>
        <div class="row" style="margin-top:14px"><button class="primary-btn" id="saveAll" type="button">Save settings</button></div>
        <p class="hint" id="saveMsg"></p>
      </section>

      <section class="content-section">
        <div class="section-heading"><div><h3>Relay channels</h3><p>Each channel needs its own board pin - never shared with another relay channel or a PC-control pin. Saving drives all channels OFF (safe start).</p></div></div>
        <article class="glass lower-card">
          <div class="row">
            <div class="field" style="max-width:200px"><label>Channel count <span id="relayMax"></span></label><input id="relayCount" type="number" min="0"></div>
            <span class="hint" id="relayCountHint" style="margin:0"></span>
          </div>
          <div id="relayList"></div>
        </article>
        <div class="row" style="margin-top:14px"><button class="primary-btn" id="saveRelays" type="button">Save relay settings</button></div>
        <p class="hint" id="relaySaveMsg"></p>
      </section>

      <section class="content-section">
        <div class="section-heading"><div><h3>Device</h3><p>Restart the ESP32 firmware - WiFi, the Tailnet identity, the GPIO mapping, the login and the API token are all kept.</p></div></div>
        <article class="glass lower-card">
          <div class="row"><button class="soft-btn" id="rebootBtn" type="button">Reset Device</button><span class="hint" id="rebootMsg" style="margin:0"></span></div>
        </article>
      </section>

      <section class="content-section">
        <div class="section-heading"><div><h3>Activity log</h3><p>This browser session only.</p></div></div>
        <article class="glass lower-card"><div class="event-list" id="logList"></div></article>
      </section>

      <footer class="footer"><div class="safe"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 22s8-4 8-11V5l-8-3-8 3v6c0 7 8 11 8 11Z"/><path d="m9 12 2 2 4-4"/></svg>Every change is validated and enforced in firmware; NVS is written only on explicit saves.</div><div>Control Deck &middot; MicroLink &middot; ISUI dark glass</div></footer>
    </main>
  </div>
</div>
</body>
)PSM";

/* ---- /pc/settings : settings page script ---- */

static const char PCCTL_SETTINGS_SCRIPT[] = R"PJS(
'use strict';
const $ = function(id){ return document.getElementById(id); };
const FUNCS = ['POWER_ON', 'WAKE_UP', 'PWR_RESET', 'FORCE_POWER_OFF'];
const FUNC_DESC = {
  POWER_ON: 'short PWR_SW press (start the PC)',
  WAKE_UP: 'short PWR_SW press (wake)',
  PWR_RESET: 'RESET_SW press (hardware reset)',
  FORCE_POWER_OFF: 'long PWR_SW hold (frozen PC)'
};
const HW_FUNCS = ['POWER_ON', 'WAKE_UP', 'PWR_RESET', 'FORCE_POWER_OFF'];
let cfg = null, memTok = '';
function sanitizeTok(v){
  v = String(v || '').trim();
  v = v.replace(/^CONFIG_PCCTL_API_TOKEN\s*=\s*/, '');
  v = v.replace(/^[\s"']+|[\s"']+$/g, '');
  return v;
}
function tok(){ try { const v = localStorage.getItem('pcctl_token'); if (v) return v; } catch(e){} return memTok; }
function storeTok(v){ memTok = v; try { localStorage.setItem('pcctl_token', v); return true; } catch(e){ return false; } }
async function fetchJson(path, opts){
  opts = opts || {};
  const headers = opts.headers || {};
  if (tok()) headers['X-PC-Token'] = tok();
  opts.headers = headers;
  try {
    const r = await fetch(path, opts);
    let j = null;
    try { j = await r.json(); } catch(e){}
    return { status: r.status, json: j };
  } catch (e) {
    return { status: 0, json: null, error: String(e) };
  }
}
function nowTime(){
  const d = new Date();
  return String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0')+':'+String(d.getSeconds()).padStart(2,'0');
}
function log(msg, kind){
  const list = $('logList');
  const el = document.createElement('div');
  el.className = 'event';
  const ic = kind === 'err'
    ? '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M12 3 2.8 20h18.4L12 3Z"/><path d="M12 9v5m0 3h.01"/></svg>'
    : '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="m5 12 4 4L19 6"/></svg>';
  el.innerHTML = '<span class="event-icon">' + ic + '</span><div><strong>Activity</strong><small></small></div><time>' + nowTime() + '</time>';
  el.querySelector('small').textContent = msg;
  list.insertBefore(el, list.firstChild);
  while (list.children.length > 20) list.removeChild(list.lastChild);
}
function buildFuncList(){
  const fl = $('funcList');
  fl.innerHTML = FUNCS.map(function(name){
    return '<div class="switch-row" style="margin-bottom:8px"><div><strong style="font-size:12px">' + name + '</strong><small style="display:block;color:#8199b2;font-size:10px;margin-top:2px">' + FUNC_DESC[name] + '</small></div>' +
      '<label class="chkrow" style="margin:0"><input type="checkbox" id="f-' + name + '"></label></div>';
  }).join('');
}
function buildGpioRows(){
  const gl = $('gpioList');
  gl.innerHTML = HW_FUNCS.map(function(name){
    return '<div class="frm"><label>' + name + '</label><div class="row">' +
      '<select class="txt" id="g-' + name + '" style="flex:1 1 130px"></select>' +
      '<select class="txt" id="l-' + name + '" style="flex:0 1 130px" title="level driven to press the front-panel input">' +
      '<option value="high">Active HIGH</option><option value="low">Active LOW</option></select></div></div>';
  }).join('');
}
function relayCfg(){ return (cfg && cfg.relays) || { channel_count: 0, max_channels: 0, channels: [] }; }
function relayHighestConfigured(){
  let h = -1;
  relayCfg().channels.forEach(function(c){
    if (c.enabled || (c.gpio !== null && c.gpio !== undefined)) h = Math.max(h, c.id);
  });
  return h;
}
function relayRowCount(n){ return Math.max(n, relayHighestConfigured() + 1); }
function updateRelayHint(n){
  const rc = relayCfg();
  if (!rc.max_channels) { $('relayCountHint').textContent = 'relay support not reported by the firmware'; return; }
  const extra = relayRowCount(n) - n;
  $('relayCountHint').textContent = extra > 0
    ? (extra + ' row(s) are above the count because they still have settings - clear their GPIO/enable, then reduce')
    : ('0-' + rc.max_channels + ' channels');
}
function buildRelayRows(n){
  const rc = relayCfg();
  const list = $('relayList');
  list.innerHTML = '';
  for (let i = 0; i < n; i++) {
    const c = rc.channels[i] || { id: i, name: '', enabled: false, gpio: null, active_level: 'high' };
    const row = document.createElement('div');
    row.className = 'switch-row';
    row.style.marginBottom = '8px';
    row.style.flexWrap = 'wrap';
    const lbl = document.createElement('span');
    lbl.className = 'relay-id';
    lbl.style.minWidth = '86px';
    lbl.textContent = 'Relay ' + (i + 1) + (i >= rc.channel_count ? ' (above count)' : '');
    const name = document.createElement('input'); name.className = 'txt'; name.id = 'rn-' + i;
    name.maxLength = 32; name.placeholder = 'name'; name.value = c.name || '';
    name.style.flex = '1 1 130px';
    const sel = document.createElement('select'); sel.className = 'txt'; sel.id = 'rg-' + i;
    sel.style.flex = '0 1 130px';
    sel.innerHTML = '<option value="">NC (not configured)</option>' +
      (cfg.allowed_gpios || []).map(function(p){ return '<option value="' + p + '">GPIO' + p + '</option>'; }).join('');
    sel.value = (c.gpio === null || c.gpio === undefined) ? '' : String(c.gpio);
    const lvl = document.createElement('select'); lvl.className = 'txt'; lvl.id = 'rl-' + i;
    lvl.style.flex = '0 1 120px'; lvl.title = 'level driven to switch the relay ON';
    lvl.innerHTML = '<option value="high">Active HIGH</option><option value="low">Active LOW</option>';
    lvl.value = (c.active_level === 'low') ? 'low' : 'high';
    const en = document.createElement('label'); en.className = 'chkrow'; en.style.margin = '0';
    en.innerHTML = '<input type="checkbox" id="re-' + i + '"' + (c.enabled ? ' checked' : '') + '><span>enabled</span>';
    const so = document.createElement('span'); so.className = 'relay-id'; so.textContent = 'startup: OFF';
    row.appendChild(lbl); row.appendChild(name); row.appendChild(en); row.appendChild(sel); row.appendChild(lvl); row.appendChild(so);
    list.appendChild(row);
  }
}
function render(){
  $('hdrDev').textContent = 'device settings';
  $('shortRange').textContent = '(' + cfg.short_press_min_ms + '-' + cfg.short_press_max_ms + ' ms)';
  $('longRange').textContent = '(' + cfg.long_press_min_ms + '-' + cfg.long_press_max_ms + ' ms)';
  $('shortMs').value = cfg.short_press_ms;
  $('longMs').value = cfg.long_press_ms;
  FUNCS.forEach(function(name){
    const cb = $('f-' + name);
    if (cb) cb.checked = !!cfg.functions[name];
  });
  HW_FUNCS.forEach(function(name){
    const sel = $('g-' + name);
    if (!sel) return;
    sel.innerHTML = '<option value="">NC (not configured)</option>' +
      cfg.allowed_gpios.map(function(p){ return '<option value="' + p + '">GPIO' + p + '</option>'; }).join('');
    sel.value = (cfg.gpios[name] === null || cfg.gpios[name] === undefined) ? '' : String(cfg.gpios[name]);
    const lvl = $('l-' + name);
    if (lvl) lvl.value = (cfg.gpio_levels && cfg.gpio_levels[name] === 'low') ? 'low' : 'high';
  });
  renderRelaySection();
}
function renderRelaySection(){
  const rc = relayCfg();
  $('relayMax').textContent = '(0-' + (rc.max_channels || 0) + ')';
  $('relayCount').max = rc.max_channels || 0;
  $('relayCount').value = rc.channel_count;
  buildRelayRows(relayRowCount(rc.channel_count));
  updateRelayHint(rc.channel_count);
}
async function refreshDev(){
  const r = await fetchJson('/api/v1/status');
  if (r.status === 200 && r.json && r.json.esp32) {
    const esp = r.json.esp32;
    const ok = esp.state === 'CONNECTED';
    $('bDev').className = 'connection' + (ok ? '' : ' warn');
    $('netTxt').textContent = ok ? 'TAILNET OK' : (esp.state || '?');
    $('hdrDev').textContent = (esp.hostname || '') + (esp.vpn_ip ? '  (' + esp.vpn_ip + ')' : '');
  }
}
async function load(){
  const rc = await fetchJson('/api/v1/config');
  if (rc.status !== 200 || !rc.json || !rc.json.ok) {
    $('relaySaveMsg').textContent = 'Cannot load config (HTTP ' + rc.status + (rc.error ? ': ' + rc.error : '') + ').';
    return;
  }
  cfg = rc.json;
  render();
}
async function saveAll(){
  const functions = {};
  FUNCS.forEach(function(n){ functions[n] = $('f-' + n).checked; });
  const gpios = {};
  HW_FUNCS.forEach(function(n){
    const v = $('g-' + n).value;
    gpios[n] = (v === '') ? null : parseInt(v, 10);
  });
  const levels = {};
  HW_FUNCS.forEach(function(n){
    const l = $('l-' + n);
    levels[n] = (l && l.value === 'low') ? 'low' : 'high';
  });
  const body = {
    functions: functions,
    gpios: gpios,
    gpio_levels: levels,
    short_press_ms: parseInt($('shortMs').value, 10),
    long_press_ms: parseInt($('longMs').value, 10)
  };
  if (isNaN(body.short_press_ms) || isNaN(body.long_press_ms)) {
    isuiToast('Fill every field with a valid value first.', true);
    return;
  }
  $('saveMsg').textContent = 'Saving...';
  const r = await fetchJson('/api/v1/config', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (r.status === 200 && r.json && r.json.ok) {
    cfg = r.json;
    render();
    $('saveMsg').textContent = 'Saved to NVS and applied.';
    isuiToast('Device settings saved.');
    log('settings saved');
  } else if (r.status === 401) {
    $('saveMsg').textContent = 'Rejected: API token missing or invalid - set it in the top card.';
  } else {
    $('saveMsg').textContent = 'Save failed (' + r.status + '): ' +
      ((r.json && (r.json.detail || r.json.error)) || r.error || '?');
  }
}
function onRelayCountChange(){
  const rc = relayCfg();
  let n = parseInt($('relayCount').value, 10);
  if (isNaN(n) || n < 0) n = 0;
  if (n > rc.max_channels) n = rc.max_channels;
  $('relayCount').value = n;
  buildRelayRows(relayRowCount(n));
  updateRelayHint(n);
}
async function saveRelays(){
  const rc = relayCfg();
  let n = parseInt($('relayCount').value, 10);
  if (isNaN(n) || n < 0 || n > rc.max_channels) {
    isuiToast('Channel count must be 0-' + rc.max_channels + '.', true);
    return;
  }
  const rows = $('relayList').children.length;
  const channels = [];
  for (let i = 0; i < rows; i++) {
    const gv = $('rg-' + i).value;
    channels.push({
      id: i,
      name: $('rn-' + i).value,
      enabled: $('re-' + i).checked,
      gpio: (gv === '') ? null : parseInt(gv, 10),
      active_level: $('rl-' + i).value,
      startup_policy: 'off'
    });
  }
  $('relaySaveMsg').textContent = 'Saving...';
  const r = await fetchJson('/api/v1/relays/config', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ channel_count: n, channels: channels })
  });
  const j = r.json || {};
  if (r.status === 200 && j.ok && j.relays) {
    cfg.relays = j.relays;
    renderRelaySection();
    $('relaySaveMsg').textContent = 'Saved to NVS and applied - all relays were driven OFF (safe start).';
    isuiToast('Relay settings saved.');
    log('relay settings saved');
  } else if (r.status === 401) {
    $('relaySaveMsg').textContent = 'Rejected: API token missing or invalid - set it in the top card.';
  } else if (r.status === 409 && j.error === 'unsafe_reduction') {
    $('relaySaveMsg').textContent = 'Rejected: ' + (j.detail || 'clear the dropped channels first');
  } else {
    $('relaySaveMsg').textContent = 'Save failed (' + r.status + '): ' +
      (j.detail || j.error || r.error || '?');
  }
}
$('apiTokSave').addEventListener('click', async function(){
  const v = sanitizeTok($('apiTok').value);
  if (!v) { $('apiTokMsg').textContent = 'Empty.'; return; }
  storeTok(v);
  $('apiTokMsg').textContent = 'Checking...';
  const r = await fetchJson('/api/v1/commands', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command: 'PING' })
  });
  if (r.status === 200 && r.json && r.json.pong) {
    $('apiTokMsg').textContent = 'Token OK - saved.';
    $('authBanner').classList.add('hidden');
    $('apiTok').value = '';
    isuiToast('API token accepted.');
    log('API token saved');
  } else if (r.status === 401) {
    $('apiTokMsg').textContent = 'Device REJECTED it (401).';
  } else {
    $('apiTokMsg').textContent = 'Could not verify (HTTP ' + r.status + ').';
  }
});
/* Device reset: no confirm()/alert() - captive-portal webviews block JS
 * dialogs - so the button uses a two-step inline confirm: click once to arm
 * ("Confirm reset?"), click again within 4 s to fire. */
async function rebootNow(){
  $('rebootMsg').textContent = 'Restarting...';
  const r = await fetchJson('/api/v1/device/reboot', { method: 'POST' });
  if (r.status === 200 && r.json && r.json.rebooting) {
    $('rebootMsg').textContent = 'Device is restarting - this page stops responding for a few seconds.';
    log('device reset requested');
  } else if (r.status === 401) {
    $('rebootMsg').textContent = 'Rejected: API token missing or invalid - set it in the top card.';
  } else {
    $('rebootMsg').textContent = 'Reset failed (' + r.status + '): ' +
      ((r.json && (r.json.detail || r.json.error)) || r.error || '?');
  }
}
function doReboot(){
  const b = $('rebootBtn');
  if (b.dataset.armed === '1') {
    b.dataset.armed = '';
    b.textContent = 'Reset Device';
    rebootNow();
    return;
  }
  b.dataset.armed = '1';
  b.textContent = 'Confirm reset?';
  $('rebootMsg').textContent = 'Click again within 4 s to restart the device.';
  setTimeout(function(){
    if (b.dataset.armed === '1') {
      b.dataset.armed = '';
      b.textContent = 'Reset Device';
      $('rebootMsg').textContent = '';
    }
  }, 4000);
}
$('rebootBtn').addEventListener('click', doReboot);
$('saveAll').addEventListener('click', saveAll);
$('saveRelays').addEventListener('click', saveRelays);
$('relayCount').addEventListener('change', onRelayCountChange);
function applyHashToken(){
  const m = location.hash.match(/[#&]t=([^&]+)/);
  if (!m) return false;
  const v = sanitizeTok(decodeURIComponent(m[1]));
  if (!v) return false;
  storeTok(v);
  try { history.replaceState(null, '', location.pathname); } catch(e){}
  log('Token applied from URL');
  return true;
}
buildFuncList();
buildGpioRows();
applyHashToken();
if (!tok()) {
  $('authBanner').classList.remove('hidden');
  $('authMsg').textContent = 'No API token in this browser yet - paste it above (or open /pc/settings#t=TOKEN); saving requires it.';
}
refreshDev();
load();
)PJS";

