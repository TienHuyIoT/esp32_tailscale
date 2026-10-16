/**
 * @file pc_security_web.h
 * @brief Embedded Security page (/pc/security): admin login, API-token
 *        management (generate/set/clear) and login-credential change.
 *
 * Gated by a username/password login (default admin/admin) that yields a
 * RAM-only session cookie. Every mutating call on this page requires that
 * session; the API token itself is only ever returned once, right after it
 * is generated or set.
 */

#pragma once

static const char PCCTL_SECURITY_HTML[] = R"PCSEC(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Control - Security</title>
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
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:14px;margin-bottom:10px}
h2{font-size:.85em;color:#8b949e;margin:18px 0 8px;text-transform:uppercase;letter-spacing:.5px}
.d{color:#8b949e;font-size:.85em;line-height:1.5}
.frm{display:flex;flex-direction:column;margin-bottom:10px;max-width:420px}
.frm label{font-size:.8em;color:#8b949e;margin-bottom:4px}
.txt{width:100%;padding:9px 12px;background:#0d1117;border:1px solid #30363d;color:#e6edf3;border-radius:6px;font-size:.9em}
.btn{padding:9px 16px;border:none;border-radius:6px;cursor:pointer;font-size:.85em;font-weight:600;background:#238636;color:#fff}
.btn.ghost{background:#21262d;color:#c9d1d9;border:1px solid #30363d}
.btn:disabled{opacity:.5;cursor:not-allowed}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.hint{font-size:.75em;color:#8b949e;margin:8px 0 0;line-height:1.5}
#logList{list-style:none;font-size:.8em;margin-top:4px}
#logList li{padding:6px 10px;border-bottom:1px solid #21262d;color:#c9d1d9}
#logList li .t{color:#484f58;margin-right:8px}
footer{margin:22px 0 8px;font-size:.72em;color:#484f58;text-align:center;line-height:1.5}
</style></head><body>
<div class="hdr">
 <div><h1>PC Control - Security</h1><div class="sub" id="hdrDev">&hellip;</div></div>
 <div class="badges"><a class="badge" href="/pc">&larr; Control page</a><a class="badge" href="/pc/settings">Settings</a></div>
</div>
<div class="wrap">

 <div id="loginCard" class="card">
  <h2 style="margin-top:0">Login</h2>
  <div class="frm"><label>Username</label><input class="txt" id="lu" autocomplete="username" spellcheck="false"></div>
  <div class="frm"><label>Password</label><input class="txt" id="lp" type="password" autocomplete="current-password"></div>
  <div class="row"><button class="btn" id="loginBtn">Log in</button></div>
  <p class="hint" id="loginMsg"></p>
  <p class="hint">Default credentials: <code>admin</code> / <code>admin</code>. Change them below after the first login.</p>
 </div>

 <div id="main" class="hidden">
  <div class="row" style="justify-content:space-between">
   <h2 style="margin:0" id="whoami">Signed in</h2>
   <button class="btn ghost" id="logoutBtn">Log out</button>
  </div>

  <h2>API token (browser &rarr; ESP32)</h2>
  <div class="card">
   <div class="d" id="tokState">&hellip;</div>
   <div class="row" style="margin-top:10px">
    <button class="btn" id="tokGen">Generate new token</button>
    <button class="btn ghost" id="tokClear">Clear (use firmware default)</button>
   </div>
   <div class="row" style="margin-top:10px">
    <input class="txt" id="tokCustom" placeholder="or paste a custom token (4-96 chars, no spaces)" style="max-width:420px">
    <button class="btn ghost" id="tokSet">Set</button>
   </div>
   <div id="tokOut" class="hidden" style="margin-top:12px">
    <div class="hint" style="color:#d29922">Copy this token now - it is shown only once. Paste it into the control/settings page ("API token (this browser)").</div>
    <div class="row" style="margin-top:6px"><input class="txt" id="tokVal" readonly style="max-width:420px"><button class="btn" id="tokCopy">Copy</button></div>
   </div>
   <p class="hint" id="tokMsg"></p>
  </div>

  <h2>Change login</h2>
  <div class="card">
   <div class="frm"><label>Current password</label><input class="txt" id="cp" type="password" autocomplete="current-password"></div>
   <div class="frm"><label>New username (leave blank to keep the current one)</label><input class="txt" id="nu" autocomplete="username" spellcheck="false"></div>
   <div class="frm"><label>New password (leave blank to keep the current one)</label><input class="txt" id="np" type="password" autocomplete="new-password"></div>
   <div class="row"><button class="btn" id="credBtn">Update credentials</button></div>
   <p class="hint" id="credMsg"></p>
  </div>
 </div>

 <h2>Log</h2><ul id="logList"></ul>
 <footer>Session cookie only (RAM, expires on reboot). Traffic stays inside the WireGuard tunnel.<br>
 The API token is stored in NVS and is never logged.</footer>
</div>
<script>
'use strict';
const $ = function(id){ return document.getElementById(id); };
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
async function api(path, opts){
  opts = opts || {};
  try {
    const r = await fetch(path, opts);
    let j = null;
    try { j = await r.json(); } catch(e){}
    return { status: r.status, json: j };
  } catch (e) {
    return { status: 0, json: null, error: String(e) };
  }
}
function post(path, body){
  return api(path, { method:'POST', headers:{'Content-Type':'application/json'},
                     body: JSON.stringify(body || {}) });
}
function showTok(t){
  if (!t) { $('tokOut').classList.add('hidden'); return; }
  $('tokVal').value = t;
  $('tokOut').classList.remove('hidden');
}
async function refresh(){
  const r = await api('/api/v1/auth/status');
  const j = r.json || {};
  if (j.logged_in) {
    $('loginCard').classList.add('hidden');
    $('main').classList.remove('hidden');
    $('hdrDev').textContent = 'signed in as ' + (j.username || '?');
    $('whoami').textContent = 'Signed in as ' + (j.username || '?');
    $('tokState').textContent = j.api_token_set
      ? ('API token: configured' + (j.api_token_custom ? ' (custom, stored in NVS).' : ' (firmware default).'))
      : 'API token: NOT configured - mutations are rejected until one is set.';
  } else {
    $('loginCard').classList.remove('hidden');
    $('main').classList.add('hidden');
    $('hdrDev').textContent = 'not signed in';
  }
}
$('loginBtn').addEventListener('click', async function(){
  $('loginMsg').textContent = 'Signing in...';
  const r = await post('/api/v1/auth/login', { username: $('lu').value.trim(), password: $('lp').value });
  $('lp').value = '';
  if (r.status === 200 && r.json && r.json.ok) {
    $('loginMsg').textContent = '';
    log('signed in');
    refresh();
  } else if (r.status === 401) {
    $('loginMsg').textContent = 'Wrong username or password.';
  } else {
    $('loginMsg').textContent = 'Login failed (HTTP ' + r.status + ').';
  }
});
$('lp').addEventListener('keydown', function(e){ if (e.key === 'Enter') $('loginBtn').click(); });
$('logoutBtn').addEventListener('click', async function(){
  await post('/api/v1/auth/logout', {});
  showTok(null);
  log('signed out');
  refresh();
});

$('tokGen').addEventListener('click', async function(){
  $('tokMsg').textContent = 'Generating...';
  const r = await post('/api/v1/auth/api-token', { generate: true });
  if (r.status === 200 && r.json && r.json.token) {
    showTok(r.json.token);
    $('tokMsg').textContent = 'New token generated.';
    log('API token generated');
  } else if (r.status === 401) {
    $('tokMsg').textContent = 'Session expired - log in again.';
    refresh();
  } else {
    $('tokMsg').textContent = 'Failed (' + r.status + ').';
  }
});
$('tokSet').addEventListener('click', async function(){
  const v = $('tokCustom').value.trim();
  if (!v) { $('tokMsg').textContent = 'Enter a token first.'; return; }
  const r = await post('/api/v1/auth/api-token', { token: v });
  if (r.status === 200) {
    $('tokCustom').value = '';
    showTok(r.json && r.json.token);
    $('tokMsg').textContent = 'Token set.';
    log('API token set');
  } else if (r.status === 400) {
    $('tokMsg').textContent = 'Rejected: 4-96 printable chars, no spaces.';
  } else if (r.status === 401) {
    $('tokMsg').textContent = 'Session expired - log in again.';
    refresh();
  } else {
    $('tokMsg').textContent = 'Failed (' + r.status + ').';
  }
});
$('tokClear').addEventListener('click', async function(){
  const r = await post('/api/v1/auth/api-token', { clear: true });
  if (r.status === 200) {
    showTok(null);
    $('tokMsg').textContent = 'Cleared - the firmware default applies.';
    log('API token cleared');
    refresh();
  } else {
    $('tokMsg').textContent = 'Failed (' + r.status + ').';
  }
});
$('tokCopy').addEventListener('click', function(){
  $('tokVal').select();
  try { document.execCommand('copy'); $('tokMsg').textContent = 'Copied to clipboard.'; }
  catch (e) { $('tokMsg').textContent = 'Copy failed - select and copy manually.'; }
});
$('credBtn').addEventListener('click', async function(){
  $('credMsg').textContent = 'Updating...';
  const r = await post('/api/v1/auth/credentials', {
    current_password: $('cp').value,
    username: $('nu').value.trim(),
    new_password: $('np').value
  });
  $('cp').value = ''; $('np').value = '';
  if (r.status === 200) {
    $('credMsg').textContent = 'Credentials updated.';
    $('nu').value = '';
    log('login credentials updated');
  } else if (r.status === 403) {
    $('credMsg').textContent = 'Current password is incorrect.';
  } else if (r.status === 400) {
    $('credMsg').textContent = 'Invalid values (username 1-32, no spaces; password 1-64).';
  } else if (r.status === 401) {
    $('credMsg').textContent = 'Session expired - log in again.';
    refresh();
  } else {
    $('credMsg').textContent = 'Failed (' + r.status + ').';
  }
});
refresh();
</script>
</body></html>
)PCSEC";

