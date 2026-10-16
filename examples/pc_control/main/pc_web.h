/**
 * @file pc_web.h
 * @brief Embedded web UI for the PC controller (single page, vanilla
 *        HTML/CSS/JS - no external assets, no frameworks).
 *
 * Behavior contract (mirrors the firmware's honesty rules):
 *  - PC state shows the firmware's state + source + note; the device has no
 *    power-sensing input, so it is usually reported as unknown;
 *  - every command drives the real motherboard front-panel GPIO;
 *  - commands are disabled only while another command is active (the hardware
 *    cooldown runs in the background and is reported, not enforced by locking
 *    the buttons);
 *  - POWER_ON and WAKE_UP ask for confirmation; PWR_RESET requires typing the
 *    command name; FORCE_POWER_OFF requires typing the command name (inline
 *    modal - no JS confirm/alert);
 *  - status polling backs off on failure and pauses while the tab is hidden;
 *  - the USB HID login panel is explicit-submit only, clears the password
 *    field immediately, never persists it, and reports only that keystrokes
 *    were sent (it cannot know whether Windows accepted them).
 */

#pragma once

static const char PCCTL_PAGE_HTML[] = R"PCCTL(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Control - MicroLink</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:#0d1117;color:#e6edf3}
.hdr{background:#161b22;border-bottom:1px solid #30363d;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}
.hdr h1{font-size:1.3em;color:#58a6ff}
.sub{color:#8b949e;font-size:.8em}
.badges{display:flex;gap:8px;flex-wrap:wrap}
.badge{padding:4px 10px;border-radius:12px;font-size:.72em;font-weight:700;border:1px solid #30363d;background:#0d1117;color:#8b949e}
.badge.mode{color:#d29922;border-color:#d29922}
.badge.ok{color:#3fb950;border-color:#3fb950}
.badge.bad{color:#f85149;border-color:#f85149}
.badge.mid{color:#d29922;border-color:#d29922}
.wrap{max-width:960px;margin:0 auto;padding:16px}
.banner{padding:10px 14px;border-radius:8px;margin-bottom:12px;font-size:.85em;border:1px solid;display:flex;gap:10px;align-items:center;justify-content:space-between;flex-wrap:wrap}
.banner.warn{background:#d2992220;border-color:#d29922;color:#d29922}
.hidden{display:none !important}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px;margin-bottom:12px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px}
.card .k{font-size:.7em;color:#8b949e;text-transform:uppercase;letter-spacing:.5px}
.card .v{font-size:1.05em;font-weight:700;margin:6px 0 4px}
.card .d{font-size:.75em;color:#8b949e;line-height:1.4;word-break:break-word}
.v-ok{color:#3fb950}.v-bad{color:#f85149}.v-mid{color:#d29922}
.active{background:#1f6feb20;border:1px solid #1f6feb;border-radius:8px;padding:12px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;font-size:.85em}
h2{font-size:.85em;color:#8b949e;margin:18px 0 8px;text-transform:uppercase;letter-spacing:.5px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
@media(max-width:640px){.grid{grid-template-columns:1fr 1fr}}
.cmdbtn{background:#21262d;border:1px solid #30363d;border-radius:8px;padding:14px 10px;color:#e6edf3;font-size:.9em;font-weight:600;cursor:pointer;display:flex;flex-direction:column;align-items:center;gap:6px;min-height:88px;justify-content:center;text-align:center}
.cmdbtn .ic{font-size:1.5em;line-height:1}
.cmdbtn small{font-weight:400;color:#8b949e;font-size:.72em}
.cmdbtn:disabled{opacity:.45;cursor:not-allowed}
.cmdbtn.danger{border-color:#da3633}
.cmdbtn.danger .ic{color:#f85149}
.cmdbtn:active:not(:disabled){background:#30363d}
.hint{font-size:.75em;color:#8b949e;margin-top:10px;line-height:1.5}
#logList{list-style:none;font-size:.8em;margin-top:4px}
#logList li{padding:6px 10px;border-bottom:1px solid #21262d;color:#c9d1d9}
#logList li .t{color:#484f58;margin-right:8px}
.btn{padding:9px 16px;border:none;border-radius:6px;cursor:pointer;font-size:.85em;font-weight:600;background:#238636;color:#fff}
.btn.ghost{background:#21262d;color:#c9d1d9;border:1px solid #30363d}
.btn.danger{background:#da3633;color:#fff}
.btn:disabled{opacity:.5;cursor:not-allowed}
.modal{position:fixed;top:0;left:0;right:0;bottom:0;inset:0;background:#010409cc;display:flex;align-items:center;justify-content:center;padding:16px;z-index:50}
.modal-box{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:18px;max-width:430px;width:100%}
.modal-box h3{margin-bottom:10px;color:#e6edf3;font-size:1.05em}
.modal-box p{font-size:.85em;color:#8b949e;line-height:1.5;margin-bottom:12px}
.modal-box code{color:#e6edf3;background:#0d1117;padding:1px 5px;border-radius:4px;font-size:.9em}
.row{display:flex;gap:10px;justify-content:flex-end;margin-top:12px}
.txt{width:100%;padding:10px 12px;background:#0d1117;border:1px solid #30363d;color:#e6edf3;border-radius:6px;font-size:.9em;margin-bottom:4px}
.spin{display:inline-block;width:10px;height:10px;border:2px solid #58a6ff;border-top-color:transparent;border-radius:50%;animation:sp 1s linear infinite;margin-right:6px;vertical-align:middle}
@keyframes sp{to{transform:rotate(360deg)}}
footer{margin:22px 0 8px;font-size:.72em;color:#484f58;text-align:center;line-height:1.5}
</style></head><body>
<div class="hdr">
 <div><h1>PC Control</h1><div class="sub" id="hdrDev">&hellip;</div></div>
 <div class="badges">
  <span class="badge" id="bNet">TAILNET ?</span>
  <a class="badge" href="/pc/settings" style="text-decoration:none">&#9881; Settings</a>
  <a class="badge" href="/pc/security" style="text-decoration:none">&#128274; Security</a>
 </div>
</div>
<div class="wrap">
 <div id="authBanner" class="banner warn hidden"><span id="authMsg"></span><button class="btn ghost" id="btnTokOpen">Set token</button></div>
 <div id="staleBanner" class="banner warn hidden"><span>&#9888; Status is not updating (last refresh failed). Values may be stale.</span></div>
 <div class="cards">
  <div class="card"><div class="k">PC state</div><div class="v" id="vPc">&hellip;</div><div class="d" id="dPc"></div></div>
  <div class="card"><div class="k">ESP32 / tailnet</div><div class="v" id="vEsp">&hellip;</div><div class="d" id="dEsp"></div></div>
  <div class="card"><div class="k">Last command</div><div class="v" id="vLast">&ndash;</div><div class="d" id="dLast"></div></div>
 </div>
 <div class="active hidden" id="activeBox">
  <div><span id="activeSpin"></span><span id="activeText"></span></div>
 </div>
 <h2>Commands</h2>
 <div class="grid" id="cmdGrid"></div>
 <p class="hint" id="cdHint"></p>
 <h2>USB HID login</h2>
 <div class="card">
  <div class="d" id="hidState">HID: loading...</div>
  <div class="row" style="margin-top:8px;justify-content:flex-start">
   <input class="txt" id="hidPw" type="password" autocomplete="new-password" spellcheck="false" placeholder="Windows password (RAM only, never stored)" style="max-width:420px" disabled>
   <button class="btn" id="hidGo" disabled>Type password</button>
   <button class="btn ghost" id="hidCancel" disabled>Cancel</button>
  </div>
  <label class="hint" style="display:flex;gap:8px;align-items:center;margin-top:6px;cursor:pointer"><input type="checkbox" id="hidEnter" checked> Press Enter after typing</label>
  <div class="hint" id="hidMsg" style="min-height:1em"></div>
  <div class="hint">Types through the ESP32 USB keyboard port into whichever window has focus on the PC. This reports that keystrokes were sent - it cannot know whether Windows accepted the password (screen state, keyboard layout and other factors are invisible from here).</div>
 </div>
 <h2>Log</h2>
 <ul id="logList"></ul>
 <footer>MicroLink PC Control &middot; every command drives the motherboard front-panel inputs.<br>
 The device has no power-sensing input, so PC state is usually unknown.</footer>
</div>
<div class="modal hidden" id="modal"><div class="modal-box">
 <h3 id="mTitle"></h3><p id="mBody"></p>
 <input class="txt hidden" id="mInput" autocomplete="off" spellcheck="false" placeholder="">
 <div class="row"><button class="btn ghost" id="mNo">Cancel</button><button class="btn danger" id="mYes" disabled>Confirm</button></div>
</div></div>
<div class="modal hidden" id="tokModal"><div class="modal-box">
 <h3>API token</h3>
 <p>Paste the token from <code>sdkconfig.credentials</code> - surrounding quotes and the <code>CONFIG_PCCTL_API_TOKEN=</code> prefix are stripped automatically. It is stored only in this browser (localStorage) and sent as the <code>X-PC-Token</code> header.<br>Browser storage blocked (private mode)? Open <code>/pc#t=TOKEN</code> instead.</p>
 <input class="txt" id="tokInput" autocomplete="off" spellcheck="false" placeholder="token">
 <div id="tokMsg" class="hint" style="margin:6px 0 0;min-height:1em"></div>
 <div class="row"><button class="btn ghost" id="tokNo">Cancel</button><button class="btn" id="tokYes">Save</button></div>
</div></div>
<script>
'use strict';
const $ = function(id){ return document.getElementById(id); };
const CMDS = [
 {id:'POWER_ON', label:'Power On', ic:'\u23FB', desc:'Start the PC (PWR_SW pulse)', confirm:'soft'},
 {id:'WAKE_UP', label:'Wake Up', ic:'\u2600', desc:'Short PWR_SW press (wake)', confirm:'soft'},
 {id:'PWR_RESET', label:'PWR Reset', ic:'\u26A1', desc:'Hardware RESET_SW pulse', confirm:'hard'},
 {id:'FORCE_POWER_OFF', label:'Force Power Off', ic:'\u26A0', desc:'Hold PWR_SW (frozen PC)', confirm:'strong'}
];
let st = null, fails = 0, pollTimer = null, busy = false;
let memTok = '';
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
function log(msg){
  const li = document.createElement('li');
  const d = new Date();
  const t = String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0')+':'+String(d.getSeconds()).padStart(2,'0');
  const ts = document.createElement('span'); ts.className = 't'; ts.textContent = t;
  li.appendChild(ts); li.appendChild(document.createTextNode(msg));
  const list = $('logList');
  list.insertBefore(li, list.firstChild);
  while (list.children.length > 30) list.removeChild(list.lastChild);
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
  const g = $('cmdGrid'); g.innerHTML = '';
  CMDS.forEach(function(c){
    const b = document.createElement('button');
    b.className = 'cmdbtn' + (c.confirm === 'strong' ? ' danger' : '');
    b.id = 'btn-' + c.id;
    const ic = document.createElement('span'); ic.className = 'ic'; ic.textContent = c.ic;
    const lb = document.createElement('span'); lb.textContent = c.label;
    const ds = document.createElement('small'); ds.textContent = c.desc;
    b.appendChild(ic); b.appendChild(lb); b.appendChild(ds);
    b.addEventListener('click', function(){ onCommand(c); });
    g.appendChild(b);
  });
}
function commandDisabled(c){
  if (busy) return 'another request is in flight';
  if (st && st.active_command) return 'a command is already running';
  return null;
}
function render(s){
  st = s;
  const esp = s.esp32 || {};
  $('hdrDev').textContent = (esp.hostname || '') + (esp.vpn_ip ? '  (' + esp.vpn_ip + ')' : '');
  const bn = $('bNet');
  const espOk = esp.state === 'CONNECTED';
  bn.textContent = espOk ? 'TAILNET ok' : ('TAILNET ' + (esp.state || '?'));
  bn.className = 'badge ' + (espOk ? 'ok' : 'mid');
  const pc = s.pc || {};
  const vPc = $('vPc');
  vPc.textContent = pc.state || 'unknown';
  vPc.className = 'v ' + (pc.state === 'online' ? 'v-ok' : (pc.state === 'unknown' ? 'v-mid' : 'v-bad'));
  $('dPc').textContent = 'source: ' + (pc.source || 'none') + ' - ' + (pc.note || '');

  const vEsp = $('vEsp');
  vEsp.textContent = esp.state || '?';
  vEsp.className = 'v ' + (espOk ? 'v-ok' : 'v-mid');
  $('dEsp').textContent = 'wifi: ' + (esp.wifi_ip || '-') + '  |  vpn: ' + (esp.vpn_ip || '-') +
                          '  |  heap: ' + (esp.free_heap || 0);

  const lc = s.last_command;
  if (lc) {
    $('vLast').textContent = lc.command + ': ' + lc.state;
    $('dLast').textContent = (lc.detail || lc.error || '') + '  (' + fmtAge(lc.age_s) + ')';
  } else {
    $('vLast').textContent = 'none';
    $('dLast').textContent = '';
  }

  const ac = s.active_command;
  const box = $('activeBox');
  if (ac) {
    box.classList.remove('hidden');
    $('activeSpin').innerHTML = (ac.state === 'accepted' || ac.state === 'running')
      ? '<span class="spin"></span>' : '';
    $('activeText').textContent = ac.command + ' - ' + ac.state +
      ' - ' + (ac.detail || '') +
      '  [' + ac.request_id + ']';
  } else {
    box.classList.add('hidden');
  }

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

  const cdms = (s.hw_cooldown_ms && s.hw_cooldown_ms > 0) ? s.hw_cooldown_ms : 0;
  $('cdHint').textContent = cdms > 0
    ? ('Cooldown: ' + Math.ceil(cdms / 1000) + ' s remaining - a press now is refused until it expires.')
    : '';

  renderHid(s.hid || {});

  CMDS.forEach(function(c){
    const b = $('btn-' + c.id);
    if (!b) return;
    const why = commandDisabled(c);
    b.disabled = !!why;
    b.title = why || '';
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
  $('modal').classList.remove('hidden');
}
function closeModal(){ $('modal').classList.add('hidden'); modalCb = null; }
$('mNo').addEventListener('click', closeModal);
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
    var msg = (c.id === 'POWER_ON')
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
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    const j = r.json || {};
    if (r.status === 202) {
      log('[' + body.request_id + '] ' + c.id + ' accepted (' + (j.state || 'accepted') + ')');
    } else if (r.status === 200 && j.replay) {
      log(c.id + ' already known - state: ' + (j.command ? j.command.state : '?'));
    } else if (r.status === 401) {
      log(c.id + ' rejected: ' + (j.error || r.status) + ' - set the API token');
      openTok();
    } else if (r.status === 409 && j.error === 'cooldown') {
      const left = Math.ceil((j.retry_after_ms || 0) / 1000);
      log(c.id + ' not sent - cooldown active, wait ' + left + ' s before pressing again');
    } else {
      let m = c.id + ' rejected (' + r.status + '): ' + (j.error || r.error || '?');
      if (j.detail) m += ' - ' + j.detail;
      if (j.retry_after_ms) m += ' (retry in ' + j.retry_after_ms + ' ms)';
      if (j.active_command) m += ' [active: ' + j.active_command + ']';
      log(m);
    }
  } catch (e) {
    log(c.id + ' request failed: ' + e);
  }
  busy = false;
  refresh();
}

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
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  payload.password = '';
  const j = r.json || {};
  if (r.status === 202) {
    $('hidMsg').textContent = 'Queued - typing starts shortly; Cancel works until the first keystroke.';
    log('HID login typing queued');
  } else if (r.status === 401) {
    $('hidMsg').textContent = 'Rejected: API token missing or invalid.';
    openTok();
  } else if (j.error === 'unsupported_char') {
    $('hidMsg').textContent = 'Unsupported character at position ' + j.position + ' - printable ASCII only.';
  } else {
    var m = 'Rejected (' + r.status + '): ' + (j.error || r.error || '?');
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
  /* While a command is running or the hardware cooldown counts down, poll
   * every second: the buttons must unlock exactly when the firmware would
   * accept them again (at the normal cadence a finished 0.5 s pulse felt
   * like a 10 s lockout). */
  const busyNow = !!(st && (st.active_command || (st.hw_cooldown_ms > 0) ||
                            (st.hid && (st.hid.state === 'queued' || st.hid.state === 'typing'))));
  if (!document.hidden && busyNow && fails === 0) delay = 1000;
  pollTimer = setTimeout(refresh, delay);
}
document.addEventListener('visibilitychange', function(){ if (!document.hidden) refresh(); });
function openTok(){
  $('tokInput').value = tok();
  $('tokModal').classList.remove('hidden');
}
$('btnTokOpen').addEventListener('click', openTok);
$('tokNo').addEventListener('click', function(){ $('tokModal').classList.add('hidden'); });
$('tokYes').addEventListener('click', async function(){
  const msg = $('tokMsg');
  const v = sanitizeTok($('tokInput').value);
  if (!v) { msg.textContent = 'Token is empty.'; return; }
  $('tokInput').value = v;
  const stored = storeTok(v);
  msg.textContent = 'Checking with the device...';
  const r = await fetchJson('/api/v1/commands', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command: 'PING' })
  });
  if (r.status === 200 && r.json && r.json.pong) {
    msg.textContent = stored
      ? 'Token accepted and saved. This browser is ready.'
      : 'Token accepted. Browser storage is blocked - valid for this session only (use the /pc#t=... link to restore it).';
    log('API token ' + (stored ? 'saved' : 'set for this session') + ' - device accepted it');
    setTimeout(function(){ $('tokModal').classList.add('hidden'); msg.textContent = ''; refresh(); }, 900);
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
applyHashToken();
window.addEventListener('hashchange', function(){ if (applyHashToken()) refresh(); });
$('hidGo').addEventListener('click', hidSubmit);
$('hidCancel').addEventListener('click', hidCancel);
buildButtons();
refresh();

</script>
</body></html>
)PCCTL";

/**
 * @file (continued in pc_web.h)
 * @brief Device Settings page (/pc/settings): per-function permissions, GPIO
 *        mapping (pin + active level) and press timings. All changes are saved
 *        through POST /api/v1/config (NVS-backed) and enforced firmware-side.
 *        The admin login and the API token are managed on /pc/security.
 */

static const char PCCTL_SETTINGS_HTML[] = R"PCSET(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Control - Settings</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:#0d1117;color:#e6edf3}
.hidden{display:none !important}
.hdr{background:#161b22;border-bottom:1px solid #30363d;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}
.hdr h1{font-size:1.3em;color:#58a6ff}
.sub{color:#8b949e;font-size:.8em}
.badges{display:flex;gap:8px;flex-wrap:wrap}
.badge{padding:4px 10px;border-radius:12px;font-size:.72em;font-weight:700;border:1px solid #30363d;background:#0d1117;color:#58a6ff;text-decoration:none}
.wrap{max-width:960px;margin:0 auto;padding:16px}
.banner{padding:10px 14px;border-radius:8px;margin-bottom:12px;font-size:.85em;border:1px solid}
.banner.warn{background:#d2992220;border-color:#d29922;color:#d29922}
h2{font-size:.85em;color:#8b949e;margin:18px 0 8px;text-transform:uppercase;letter-spacing:.5px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:14px;margin-bottom:8px}
.card .k{font-size:.7em;color:#8b949e;text-transform:uppercase;letter-spacing:.5px;margin-bottom:8px}
.d{color:#8b949e;font-size:.8em}
.frm{display:flex;flex-direction:column;margin-bottom:10px;max-width:420px}
.frm label{font-size:.8em;color:#8b949e;margin-bottom:4px}
.chkrow{display:flex;gap:10px;align-items:center;padding:6px 0;font-size:.9em;cursor:pointer}
.hint{font-size:.75em;color:#8b949e;margin:6px 0;line-height:1.5}
.txt{width:100%;padding:9px 12px;background:#0d1117;border:1px solid #30363d;color:#e6edf3;border-radius:6px;font-size:.9em}
.btn{padding:9px 16px;border:none;border-radius:6px;cursor:pointer;font-size:.85em;font-weight:600;background:#238636;color:#fff}
.btn.ghost{background:#21262d;color:#c9d1d9;border:1px solid #30363d}
.btn:disabled{opacity:.5;cursor:not-allowed}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
#logList{list-style:none;font-size:.8em}
#logList li{padding:6px 10px;border-bottom:1px solid #21262d;color:#c9d1d9}
#logList li .t{color:#484f58;margin-right:8px}
footer{margin:22px 0 8px;font-size:.72em;color:#484f58;text-align:center;line-height:1.5}
</style></head><body>
<div class="hdr">
 <div><h1>PC Control - Settings</h1><div class="sub" id="hdrDev">&hellip;</div></div>
 <div class="badges"><a class="badge" href="/pc">&larr; Control page</a><a class="badge" href="/pc/security">&#128274; Security</a></div>
</div>
<div class="wrap">
 <div id="authBanner" class="banner warn hidden"><span id="authMsg"></span></div>

 <div class="card">
  <div class="k">API token (this browser)</div>
  <div class="row"><input class="txt" id="apiTok" placeholder="paste API token (saving settings requires it)" style="max-width:420px"><button class="btn" id="apiTokSave">Save</button><span class="hint" id="apiTokMsg"></span></div>
 </div>

 <h2>Function permissions (independent)</h2>
 <div class="card" id="funcList"></div>

 <h2>GPIO mapping (hardware functions)</h2>
 <div class="card" id="gpioList">
 </div>
 <div class="hint">POWER_ON / WAKE_UP / FORCE_POWER_OFF may share one PWR_SW output; PWR_RESET must use a different pin. Functions sharing a pin must use the same active level. Active level = the level driven to "press": HIGH for the typical transistor/optocoupler stage into an active-low front-panel input; LOW for low-trigger circuits (opto modules, PNP stages). NC = not configured - hardware functions are rejected until a pin is selected. Only board-verified pins are listed.</div>

 <h2>Press timings</h2>
 <div class="card">
  <div class="row" style="gap:20px">
   <div class="frm"><label>Short press <span id="shortRange"></span></label><input class="txt" id="shortMs" type="number"></div>
   <div class="frm"><label>Long press (FORCE_POWER_OFF hold) <span id="longRange"></span></label><input class="txt" id="longMs" type="number"></div>
  </div>
 </div>

 <div class="row" style="margin-top:14px">
  <button class="btn" id="saveAll">Save settings</button>
 </div>
 <p class="hint" id="saveMsg"></p>
 <h2>Device</h2>
 <div class="card">
  <div class="d">Restart the ESP32 firmware. WiFi, the Tailnet identity, the GPIO mapping, the login and the API token are all kept - this page stops responding for a few seconds.</div>
  <div class="row" style="margin-top:10px"><button class="btn ghost" id="rebootBtn">Reset Device</button><span class="hint" id="rebootMsg" style="margin:0"></span></div>
 </div>



 <h2>Log</h2><ul id="logList"></ul>
 <footer>Changes are validated and enforced in firmware (NVS-backed); commands drive the real GPIO outputs.<br>
 The device has no power-sensing input, so PC state is usually unknown.</footer>
</div>
<script>
'use strict';
const $ = function(id){ return document.getElementById(id); };
const FUNCS = ['POWER_ON','WAKE_UP','PWR_RESET','FORCE_POWER_OFF'];
const FUNC_DESC = {
  POWER_ON: 'short PWR_SW press (start the PC)',
  WAKE_UP: 'short PWR_SW press (wake)',
  PWR_RESET: 'RESET_SW press (hardware reset)',
  FORCE_POWER_OFF: 'long PWR_SW hold (frozen PC)'
};
const HW_FUNCS = ['POWER_ON','WAKE_UP','PWR_RESET','FORCE_POWER_OFF'];
let cfg = null;
let memTok = '';
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
    return {status:r.status, json:j};
  } catch (e) {
    return {status:0, json:null, error:String(e)};
  }
}
function log(msg){
  const li = document.createElement('li');
  const d = new Date();
  const t = String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0')+':'+String(d.getSeconds()).padStart(2,'0');
  const ts = document.createElement('span'); ts.className = 't'; ts.textContent = t;
  li.appendChild(ts); li.appendChild(document.createTextNode(msg));
  const list = $('logList');
  list.insertBefore(li, list.firstChild);
  while (list.children.length > 20) list.removeChild(list.lastChild);
}
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
function buildGpioRows(){
  const gl = $('gpioList');
  HW_FUNCS.forEach(function(name){
    const row = document.createElement('div'); row.className = 'frm';
    const lbl = document.createElement('label'); lbl.textContent = name;
    const wrap = document.createElement('div'); wrap.className = 'row';
    const sel = document.createElement('select'); sel.className = 'txt'; sel.id = 'g-' + name;
    sel.style.flex = '1 1 130px';
    const lvl = document.createElement('select'); lvl.className = 'txt'; lvl.id = 'l-' + name;
    lvl.style.flex = '1 1 120px';
    lvl.title = 'level driven to "press" the front-panel input';
    const oh = document.createElement('option'); oh.value = 'high'; oh.textContent = 'Active HIGH';
    const ol = document.createElement('option'); ol.value = 'low';  ol.textContent = 'Active LOW';
    lvl.appendChild(oh); lvl.appendChild(ol);
    wrap.appendChild(sel); wrap.appendChild(lvl);
    row.appendChild(lbl); row.appendChild(wrap); gl.appendChild(row);
  });
}
function render(){
  $('hdrDev').textContent = 'device settings';
  $('shortRange').textContent = '(' + cfg.short_press_min_ms + '-' + cfg.short_press_max_ms + ' ms)';
  $('longRange').textContent = '(' + cfg.long_press_min_ms + '-' + cfg.long_press_max_ms + ' ms)';
  $('shortMs').value = cfg.short_press_ms;
  $('longMs').value = cfg.long_press_ms;

  const fl = $('funcList'); fl.innerHTML = '';
  FUNCS.forEach(function(name){
    const row = document.createElement('label'); row.className = 'chkrow';
    const cb = document.createElement('input'); cb.type = 'checkbox'; cb.id = 'f-' + name;
    cb.checked = !!cfg.functions[name];
    const t = document.createElement('span');
    t.innerHTML = '<b>' + name + '</b> <span class="d">' + FUNC_DESC[name] + '</span>';
    row.appendChild(cb); row.appendChild(t); fl.appendChild(row);
  });

  HW_FUNCS.forEach(function(name){
    const sel = $('g-' + name);
    if (!sel) return;
    sel.innerHTML = '';
    const none = document.createElement('option'); none.value = ''; none.textContent = 'NC (not configured)';
    sel.appendChild(none);
    cfg.allowed_gpios.forEach(function(p){
      const o = document.createElement('option'); o.value = p; o.textContent = 'GPIO' + p; sel.appendChild(o);
    });
    sel.value = (cfg.gpios[name] === null || cfg.gpios[name] === undefined) ? '' : String(cfg.gpios[name]);
    const lvl = $('l-' + name);
    if (lvl) lvl.value = (cfg.gpio_levels && cfg.gpio_levels[name] === 'low') ? 'low' : 'high';
  });
}
async function load(){
  const rc = await fetchJson('/api/v1/config');
  if (rc.status !== 200 || !rc.json || !rc.json.ok) {
    $('saveMsg').textContent = 'Cannot load config (HTTP ' + rc.status + (rc.error ? ': ' + rc.error : '') + ').';
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
    $('saveMsg').textContent = 'Fill every field with a valid value first.';
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
    log('settings saved');
  } else if (r.status === 401) {
    $('saveMsg').textContent = 'Rejected: API token missing or invalid - set it in the top card.';
  } else {
    $('saveMsg').textContent = 'Save failed (' + r.status + '): ' +
      ((r.json && (r.json.detail || r.json.error)) || r.error || '?');
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
    log('API token saved');
  } else if (r.status === 401) {
    $('apiTokMsg').textContent = 'Device REJECTED it (401).';
  } else {
    $('apiTokMsg').textContent = 'Could not verify (HTTP ' + r.status + ').';
  }
});
/* Device reset (restart the firmware). No confirm()/alert() - captive-portal
 * webviews block JS dialogs - so the button uses a two-step inline confirm:
 * click once to arm ("Confirm reset?"), click again within 4 s to fire. */
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
applyHashToken();
if (!tok()) {
  $('authBanner').classList.remove('hidden');
  $('authMsg').textContent = 'No API token in this browser yet - paste it above (or open /pc/settings#t=TOKEN); saving requires it.';
}
buildGpioRows();
load();
</script>
</body></html>
)PCSET";
