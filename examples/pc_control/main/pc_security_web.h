/**
 * @file pc_security_web.h
 * @brief Embedded ISUI Security page (/pc/security): admin login, API-token
 *        management (generate/set/clear) and login-credential change.
 *
 * Same ISUI theme/shell as the other /pc pages (pc_ui_theme.h + pc_api.c
 * send_ui_page()); include pc_ui_theme.h once (pc_api.c) before this header.
 *
 * Gated by a username/password login (default admin/admin) that yields a
 * RAM-only session cookie. Every mutating call on this page requires that
 * session; the API token itself is only ever returned once, right after it
 * is generated or set.
 */

#pragma once

static const char PCCTL_SECURITY_MARKUP[] = R"SECM(
<body>
<div class="ambient" aria-hidden="true"><div class="orb one"></div><div class="orb two"></div><div class="orb three"></div></div>
<div class="app">
  <header class="topbar">
    <div class="brand">
      <div class="logo" aria-hidden="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6"><rect x="4" y="4" width="16" height="16" rx="4"/><path d="M9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m12 0h4M2 15h4m12 0h4"/><path d="m9 12 2 2 4-5"/></svg></div>
      <div><h1>Control Deck <span class="acc">/ S3</span></h1><p>Security</p></div>
    </div>
    <div class="top-actions">
      <div class="connection" id="bSess"><span class="dot"></span><span id="sessTxt">checking session&hellip;</span></div>
      <a class="icon-btn" href="/pc" title="Control deck" aria-label="Control deck"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/></svg></a>
      <a class="icon-btn" href="/pc/settings" title="Device settings" aria-label="Device settings"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><circle cx="12" cy="12" r="3"/><path d="M12 2v3m0 14v3M2 12h3m14 0h3M4.9 4.9l2.1 2.1m10 10 2.1 2.1M19.1 4.9 17 7m-10 10-2.1 2.1"/></svg></a>
    </div>
  </header>
  <div class="layout">
    <aside class="sidebar" aria-label="Main navigation">
      <div class="nav-label">Workspace</div>
      <a class="nav" href="/pc#overview" title="Overview"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/></svg><span>Overview</span></a>
      <a class="nav" href="/pc/settings" title="Device settings"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><circle cx="12" cy="12" r="3"/><path d="M12 2v3m0 14v3M2 12h3m14 0h3M4.9 4.9l2.1 2.1m10 10 2.1 2.1M19.1 4.9 17 7m-10 10-2.1 2.1"/></svg><span>Settings</span></a>
      <a class="nav active" href="/pc/security" title="Security"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/></svg><span>Security</span></a>
      <div class="sidebar-foot"><strong>Session</strong><span id="hdrDev">&hellip;</span></div>
    </aside>
    <main id="overview">
      <div class="page-heading">
        <div><div class="eyebrow">ACCESS CONTROL</div><h2>Security</h2><p class="subtitle">Login, API token and credentials - all enforced by the firmware.</p></div>
      </div>

      <article class="glass lower-card" id="loginCard">
        <div class="card-top"><div><h3>Login</h3><div class="desc">Default credentials: <code>admin</code> / <code>admin</code>. Change them below after the first login.</div></div><span class="count">ADMIN</span></div>
        <div class="form-grid" style="max-width:560px">
          <div class="field"><label for="lu">Username</label><input id="lu" autocomplete="username" spellcheck="false"></div>
          <div class="field"><label for="lp">Password</label><input id="lp" type="password" autocomplete="current-password"></div>
        </div>
        <div class="row" style="margin-top:12px"><button class="primary-btn" id="loginBtn" type="button">Log in</button></div>
        <p class="hint" id="loginMsg"></p>
      </article>
      <div id="main" class="hidden">
        <div class="row" style="justify-content:space-between;margin-bottom:14px">
          <div class="small-copy" id="whoami">Signed in</div>
          <button class="soft-btn" id="logoutBtn" type="button">Log out</button>
        </div>

        <article class="glass lower-card">
          <div class="card-top"><div><h3>API token (browser &rarr; ESP32)</h3><div class="desc" id="tokState">&hellip;</div></div><span class="count">NVS</span></div>
          <div class="row">
            <button class="primary-btn" id="tokGen" type="button">Generate new token</button>
            <button class="soft-btn" id="tokClear" type="button">Clear (use firmware default)</button>
          </div>
          <div class="row" style="margin-top:10px">
            <input class="txt" id="tokCustom" placeholder="or paste a custom token (4-96 chars, no spaces)" spellcheck="false" style="max-width:420px">
            <button class="soft-btn" id="tokSet" type="button">Set</button>
          </div>
          <div id="tokOut" class="hidden" style="margin-top:12px">
            <div class="hint" style="color:var(--amber)">Copy this token now - it is shown only once. Paste it into the control/settings page ("API token (this browser)").</div>
            <div class="row" style="margin-top:6px"><input class="txt" id="tokVal" readonly style="max-width:420px"><button class="soft-btn" id="tokCopy" type="button">Copy</button></div>
          </div>
          <p class="hint" id="tokMsg"></p>
        </article>

        <article class="glass lower-card">
          <div class="card-top"><div><h3>Change login</h3><div class="desc">Stored salted + SHA-256 hashed - the plaintext is never kept.</div></div><span class="count">ADMIN</span></div>
          <div class="frm"><label>Current password</label><input class="txt" id="cp" type="password" autocomplete="current-password"></div>
          <div class="frm"><label>New username (leave blank to keep the current one)</label><input class="txt" id="nu" autocomplete="username" spellcheck="false"></div>
          <div class="frm"><label>New password (leave blank to keep the current one)</label><input class="txt" id="np" type="password" autocomplete="new-password"></div>
          <div class="row"><button class="primary-btn" id="credBtn" type="button">Update credentials</button></div>
          <p class="hint" id="credMsg"></p>
        </article>

        <section class="content-section">
          <div class="section-heading"><div><h3>Activity log</h3><p>This browser session only.</p></div></div>
          <article class="glass lower-card"><div class="event-list" id="logList"></div></article>
        </section>
      </div>

      <footer class="footer"><div class="safe"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 22s8-4 8-11V5l-8-3-8 3v6c0 7 8 11 8 11Z"/><path d="m9 12 2 2 4-4"/></svg>Session cookie only (RAM, expires on reboot). Traffic stays inside the WireGuard tunnel; the API token is stored in NVS and is never logged.</div><div>Control Deck &middot; MicroLink &middot; ISUI dark glass</div></footer>
    </main>
  </div>
</div>
</body>
)SECM";

/* ---- /pc/security : security page script ---- */

static const char PCCTL_SECURITY_SCRIPT[] = R"SECJS(
'use strict';
const $ = function(id){ return document.getElementById(id); };
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
    $('bSess').className = 'connection';
    $('sessTxt').textContent = 'SIGNED IN';
    $('hdrDev').textContent = 'signed in as ' + (j.username || '?');
    $('whoami').textContent = 'Signed in as ' + (j.username || '?');
    $('tokState').textContent = j.api_token_set
      ? ('API token: configured' + (j.api_token_custom ? ' (custom, stored in NVS).' : ' (firmware default).'))
      : 'API token: NOT configured - mutations are rejected until one is set.';
  } else {
    $('loginCard').classList.remove('hidden');
    $('main').classList.add('hidden');
    $('bSess').className = 'connection warn';
    $('sessTxt').textContent = 'NOT SIGNED IN';
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
    isuiToast('Signed in.');
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
    isuiToast('Credentials updated.');
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
)SECJS";
