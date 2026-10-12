#!/usr/bin/env python3
"""pc_manager - local-only configuration UI for the pc_agent Windows service.

Binds 127.0.0.1 ONLY (never LAN/Tailnet); stdlib only. Manages the same
config.json the agent uses: network settings, ESP32 allowlist, server-side
function flags, bearer-token generation, and service start/stop/restart.

Hard safety rules (mirrors the project plan):
  - dry_run / allow_real_execution are intentionally NOT editable here; they
    remain the human-edited config-file gates ("LOCKED" in the UI).
  - a generated token is shown exactly once, then only masked.
  - the management server stays local: bound to 127.0.0.1.

Usage:
    python pc_manager.py [--port 8766] [--config config.json]
"""

import argparse
import ipaddress
import json
import logging
import os
import secrets
import shutil
import socket
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

VERSION = "0.2.0"
DEFAULT_PORT = 8766
HERE = Path(__file__).resolve().parent
PID_FILE = HERE / "pc_agent.pid"
# Console children of a windowless (pythonw) process would each get a
# fresh visible console window - hide them all.
CREATE_NO_WINDOW = 0x08000000

log = logging.getLogger("pc_manager")


def load_cfg(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def save_cfg(path: Path, cfg: dict):
    tmp = path.with_suffix(".json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)


def harden_acl(path: Path) -> str:
    """Best-effort: restrict the config file to the current user."""
    user = os.environ.get("USERNAME", "")
    if not user:
        return "no USERNAME env - ACL not applied"
    try:
        r = subprocess.run(["icacls", str(path), "/inheritance:r",
                            "/grant:r", f"{user}:F"],
                           capture_output=True, text=True, timeout=10,
                           creationflags=CREATE_NO_WINDOW)
        return "ok" if r.returncode == 0 else f"icacls rc={r.returncode}"
    except (OSError, subprocess.SubprocessError) as exc:
        return f"icacls failed: {exc.__class__.__name__}"


def tailscale_exe():
    """Path to the tailscale CLI (PATH first, then the default install)."""
    exe = shutil.which("tailscale")
    if exe:
        return exe
    for cand in (r"C:\Program Files\Tailscale\tailscale.exe",
                 r"C:\Program Files (x86)\Tailscale\tailscale.exe"):
        if os.path.exists(cand):
            return cand
    return None


def tailscale_ipv4():
    """(ip_or_None, error_str)."""
    exe = tailscale_exe()
    if not exe:
        return None, "tailscale CLI not found"
    try:
        r = subprocess.run([exe, "ip", "-4"], capture_output=True,
                           text=True, timeout=5,
                           creationflags=CREATE_NO_WINDOW)
        if r.returncode == 0:
            for line in r.stdout.splitlines():
                line = line.strip()
                if line:
                    return line, ""
        return None, f"tailscale rc={r.returncode}"
    except (OSError, subprocess.SubprocessError) as exc:
        return None, f"tailscale unavailable: {exc.__class__.__name__}"


def read_pid():
    try:
        return int(PID_FILE.read_text().strip())
    except (OSError, ValueError):
        return None


def process_alive(pid):
    """True when pid is alive AND is a python process. The image-name check
    guards against a stale pidfile matching an unrelated reused PID."""
    if not pid:
        return False
    try:
        r = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"],
                           capture_output=True, text=True, timeout=10,
                           creationflags=CREATE_NO_WINDOW)
        return "python" in (r.stdout or "").lower()
    except (OSError, subprocess.SubprocessError):
        return False


def port_in_use(host, port):
    """True when something is already accepting connections there."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        return s.connect_ex((host, port)) == 0


def pid_is_agent(pid):
    """Verify a pid belongs to a python process running pc_agent.py."""
    try:
        cmd = ("Get-CimInstance Win32_Process -Filter \"ProcessId=%d\" | "
               "Select-Object -ExpandProperty CommandLine" % pid)
        r = subprocess.run(["powershell", "-NoProfile", "-Command", cmd],
                           capture_output=True, text=True, timeout=15,
                           creationflags=CREATE_NO_WINDOW)
        return "pc_agent" in (r.stdout or "")
    except (OSError, subprocess.SubprocessError):
        return False

def find_agent_pid():
    """Locate an externally-started pc_agent process (adoption support)."""
    try:
        cmd = ("Get-CimInstance Win32_Process | "
               "Where-Object { $_.Name -match '^python(w)?[.]exe$' -and $_.CommandLine -match 'pc_agent' } | "
               "Select-Object -First 1 -ExpandProperty ProcessId")
        r = subprocess.run(["powershell", "-NoProfile", "-Command", cmd],
                           capture_output=True, text=True, timeout=15,
                           creationflags=CREATE_NO_WINDOW)
        for line in (r.stdout or "").splitlines():
            line = line.strip()
            if line.isdigit():
                return int(line)
    except (OSError, subprocess.SubprocessError):
        pass
    return None


import urllib.error
import urllib.request


def probe_agent(cfg, ts_ip):
    """GET /api/v1/status from the running agent (token from config)."""
    port = int(cfg.get("port", 8765))
    host = cfg.get("bind_host", "auto")
    if host == "auto":
        host = ts_ip or "127.0.0.1"
    url = f"http://{host}:{port}/api/v1/status"
    req = urllib.request.Request(url, headers={
        "Authorization": "Bearer " + cfg.get("token", "")})
    try:
        with urllib.request.urlopen(req, timeout=3) as r:
            body = json.loads(r.read().decode("utf-8"))
            return {"reachable": True, "authenticated": bool(body.get("ok")),
                    "http_status": r.status, "version": body.get("version"),
                    "dry_run": body.get("dry_run"), "error": ""}
    except urllib.error.HTTPError as exc:
        return {"reachable": True, "authenticated": False,
                "http_status": exc.code, "error": f"HTTP {exc.code}"}
    except Exception as exc:  # report the class name only - never the URL token
        return {"reachable": False, "authenticated": False,
                "http_status": 0, "error": exc.__class__.__name__}


def start_service(cfg_path: Path):
    pid = read_pid()
    if pid and process_alive(pid) and pid_is_agent(pid):
        return {"ok": True, "running": True, "pid": pid,
                "detail": "already running"}
    if pid and process_alive(pid) and not pid_is_agent(pid):
        log.warning("pidfile pid %s is not a pc_agent process - ignoring "
                    "it and starting a fresh one", pid)
    adopted = find_agent_pid()
    if adopted:
        PID_FILE.write_text(str(adopted))
        return {"ok": True, "running": True, "pid": adopted,
                "detail": "adopted an already-running pc_agent process"}
    cmd = [sys.executable, str(HERE / "pc_agent.py"), "--config", str(cfg_path)]
    out = open(HERE / "console_out.log", "ab")
    err = open(HERE / "console_err.log", "ab")
    flags = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    p = subprocess.Popen(cmd, cwd=str(HERE), stdout=out, stderr=err,
                         creationflags=flags)
    PID_FILE.write_text(str(p.pid))
    time.sleep(1.0)
    ok = process_alive(p.pid)
    return {"ok": ok, "running": ok, "pid": p.pid if ok else None,
            "detail": "started" if ok else "process exited immediately - "
                      "check console_err.log"}


def stop_service():
    pid = read_pid()
    if not process_alive(pid):
        try:
            PID_FILE.unlink()
        except OSError:
            pass
        return {"ok": True, "running": False, "detail": "not running"}
    if not pid_is_agent(pid):
        return {"ok": False, "running": True,
                "detail": "pidfile pid is not a pc_agent process - refusing to kill"}
    r = subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"],
                       capture_output=True, text=True,
                       creationflags=CREATE_NO_WINDOW)
    time.sleep(0.5)
    still = process_alive(pid)
    if not still:
        try:
            PID_FILE.unlink()
        except OSError:
            pass
    return {"ok": not still, "running": still,
            "detail": "stopped" if not still else f"taskkill rc={r.returncode}"}


def validate_config_patch(cfg, patch):
    """Return (updated_cfg, error_or_None). dry_run / allow_real_execution
    are deliberately NOT patchable through the manager UI."""
    out = json.loads(json.dumps(cfg))
    if "port" in patch:
        try:
            port = int(patch["port"])
        except (TypeError, ValueError):
            return None, "port must be an integer"
        if not (1 <= port <= 65535):
            return None, "port must be 1..65535"
        out["port"] = port
    if "bind_host" in patch:
        h = str(patch["bind_host"])
        if h != "auto":
            try:
                ipaddress.ip_address(h)
            except ValueError:
                return None, "bind_host must be 'auto' or a valid IP address"
        out["bind_host"] = h
    if "allow_clients" in patch:
        entries = patch["allow_clients"]
        if not isinstance(entries, list) or not entries:
            return None, "allow_clients must be a non-empty list"
        cleaned = []
        for e in entries:
            try:
                ipaddress.ip_network(str(e), strict=False)
            except ValueError:
                return None, f"invalid CIDR/IP in allow_clients: {e}"
            cleaned.append(str(e))
        out["allow_clients"] = cleaned
    if "functions" in patch:
        fns = patch["functions"]
        if not isinstance(fns, dict):
            return None, "functions must be an object"
        merged = dict(out.get("functions", {"shutdown": True, "restart": True}))
        for k, v in fns.items():
            if k not in ("shutdown", "restart") or not isinstance(v, bool):
                return None, "functions only supports shutdown/restart booleans"
            merged[k] = v
        out["functions"] = merged
    return out, None

class ManagerHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "pc_manager/" + VERSION

    def log_message(self, fmt, *args):
        pass  # keep stdout clean; structured logging below

    def _send(self, code, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        log.info("%s %s -> %d", self.command, self.path, code)
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _read_json(self):
        try:
            n = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return None, "bad content length"
        raw = self.rfile.read(n) if n > 0 else b"{}"
        try:
            return json.loads(raw.decode("utf-8")), None
        except (UnicodeDecodeError, json.JSONDecodeError):
            return None, "invalid json"

    def do_GET(self):
        srv = self.server
        path = self.path.rstrip("/")
        if path == "":
            body = PAGES_HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/api/local/status":
            self._send(200, srv.status_payload())
            return
        self._send(404, {"ok": False, "error": "not_found"})

    def do_POST(self):
        srv = self.server
        srv.reload_cfg_if_changed()
        path = self.path.rstrip("/")
        obj, err = self._read_json()
        if err:
            self._send(400, {"ok": False, "error": "invalid_json", "detail": err})
            return

        if path == "/api/local/config":
            updated, verr = validate_config_patch(srv.cfg, obj or {})
            if verr:
                self._send(400, {"ok": False, "error": "invalid_config", "detail": verr})
                return
            srv.cfg = updated
            save_cfg(srv.cfg_path, updated)
            srv.note_saved()
            acl = harden_acl(srv.cfg_path)
            log.info("config saved by manager (acl: %s)", acl)
            self._send(200, {"ok": True, "restart_required": True, "acl": acl})
            return

        if path == "/api/local/token/rotate":
            token = secrets.token_hex(24)
            srv.cfg["token"] = token
            save_cfg(srv.cfg_path, srv.cfg)
            srv.note_saved()
            harden_acl(srv.cfg_path)
            log.info("token rotated by manager (value not logged)")
            self._send(200, {"ok": True, "token": token,
                             "note": "Copy this into the ESP32 Settings UI now - "
                                     "it is not shown again. Restart the service, "
                                     "then press Test."})
            return

        if path in ("/api/local/service/start", "/api/local/service/stop",
                    "/api/local/service/restart"):
            action = path.rsplit("/", 1)[1]
            if action == "start":
                res = start_service(srv.cfg_path)
            elif action == "stop":
                res = stop_service()
            else:
                stop_service()
                res = start_service(srv.cfg_path)
            self._send(200, res)
            return

        if path == "/api/local/test":
            ts_ip, terr = tailscale_ipv4()
            res = probe_agent(srv.cfg, ts_ip)
            res["tailscale_error"] = terr
            self._send(200, res)
            return

        self._send(404, {"ok": False, "error": "not_found"})


class ManagerServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr, cfg, cfg_path):
        super().__init__(addr, ManagerHandler)
        self.cfg = cfg
        self.cfg_path = cfg_path
        self.cfg_rev = 1
        self._cfg_mtime = self._stat_mtime()

    def _stat_mtime(self):
        try:
            return self.cfg_path.stat().st_mtime_ns
        except OSError:
            return None

    def reload_cfg_if_changed(self):
        """Pick up hand edits to config.json (e.g. the dry_run gates)
        without restarting the manager."""
        m = self._stat_mtime()
        if m is None or m == self._cfg_mtime:
            return
        self._cfg_mtime = m
        try:
            self.cfg = load_cfg(self.cfg_path)
            self.cfg_rev += 1
            log.info("config.json changed on disk - reloaded (rev %d)",
                     self.cfg_rev)
        except (OSError, json.JSONDecodeError) as exc:
            log.warning("config.json changed but could not be reloaded: %s",
                        exc)

    def note_saved(self):
        """Track a save done through this manager (no spurious reload)."""
        self.cfg_rev += 1
        self._cfg_mtime = self._stat_mtime()

    def status_payload(self):
        self.reload_cfg_if_changed()
        ts_ip, terr = tailscale_ipv4()
        cfg = self.cfg
        token = cfg.get("token", "")
        masked = (token[:4] + "..." + token[-4:]) if len(token) >= 8 else ("set" if token else "")
        pid = read_pid()
        if not process_alive(pid):
            ad = find_agent_pid()
            if ad:
                pid = ad
                PID_FILE.write_text(str(pid))
        running = process_alive(pid)
        if running:
            probe = probe_agent(cfg, ts_ip)
        else:
            probe = {"reachable": False, "authenticated": False,
                     "http_status": 0, "error": "service not running"}
        return {
            "ok": True,
            "config_rev": self.cfg_rev,
            "manager": {"version": VERSION, "bind": "127.0.0.1",
                        "config_path": str(self.cfg_path)},
            "tailscale": {"ipv4": ts_ip, "error": terr},
            "service": dict({"running": running, "pid": pid if running else None}, **probe),
            "config": {
                "port": int(cfg.get("port", 8765)),
                "bind_host": cfg.get("bind_host", "auto"),
                "allow_clients": cfg.get("allow_clients", []),
                "functions": cfg.get("functions", {}),
                "dry_run": bool(cfg.get("dry_run", True)),
                "allow_real_execution": bool(cfg.get("allow_real_execution", False)),
                "token_set": bool(token),
                "token_masked": masked,
            },
        }

# NOTE: keep the r prefix. The embedded JS contains literal \n escape
# sequences inside string literals; a non-raw string would make Python
# interpret them and serve broken JS (page loads, but every button is
# dead and the badges freeze at their placeholders).
PAGES_HTML = r"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Agent Manager</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:#0d1117;color:#e6edf3}
.hdr{background:#161b22;border-bottom:1px solid #30363d;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}
.hdr h1{font-size:1.2em;color:#58a6ff}
.sub{color:#8b949e;font-size:.78em}
.badge{padding:4px 10px;border-radius:12px;font-size:.72em;font-weight:700;border:1px solid #30363d;color:#8b949e}
.badge.ok{color:#3fb950;border-color:#3fb950}
.badge.bad{color:#f85149;border-color:#f85149}
.badge.lock{color:#d29922;border-color:#d29922}
.wrap{max-width:860px;margin:0 auto;padding:16px}
h2{font-size:.82em;color:#8b949e;margin:16px 0 8px;text-transform:uppercase;letter-spacing:.5px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:14px;margin-bottom:8px}
.k{font-size:.7em;color:#8b949e;text-transform:uppercase;letter-spacing:.5px;margin-bottom:6px}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.frm{display:flex;flex-direction:column;margin-bottom:10px;max-width:420px}
.frm label{font-size:.8em;color:#8b949e;margin-bottom:4px}
.chkrow{display:flex;gap:8px;align-items:center;padding:4px 0;font-size:.9em;cursor:pointer}
.txt{width:100%;padding:9px 12px;background:#0d1117;border:1px solid #30363d;color:#e6edf3;border-radius:6px;font-size:.9em}
textarea.txt{min-height:70px;font-family:monospace}
.btn{padding:9px 16px;border:none;border-radius:6px;cursor:pointer;font-size:.85em;font-weight:600;background:#238636;color:#fff}
.btn.ghost{background:#21262d;color:#c9d1d9;border:1px solid #30363d}
.btn.danger{background:#da3633}
.btn:disabled{opacity:.5;cursor:not-allowed}
.hint{font-size:.75em;color:#8b949e;margin:6px 0;line-height:1.5}
.mono{font-family:monospace;font-size:.85em;word-break:break-all;color:#e6edf3}
pre.tok{background:#0d1117;border:1px solid #d29922;border-radius:6px;padding:10px;margin-top:8px;white-space:pre-wrap;word-break:break-all}
#logList{list-style:none;font-size:.8em}
#logList li{padding:5px 10px;border-bottom:1px solid #21262d;color:#c9d1d9}
#logList li .t{color:#484f58;margin-right:8px}
footer{margin:20px 0 8px;font-size:.72em;color:#484f58;text-align:center}
</style></head><body>
<div class="hdr">
 <div><h1>PC Agent Manager</h1><div class="sub">local-only management &middot; 127.0.0.1</div></div>
 <div class="row">
  <span class="badge" id="bDry">MODE ?</span>
  <span class="badge" id="bSvc">SERVICE ?</span>
  <span class="badge" id="bTs">TAILSCALE ?</span>
 </div>
</div>
<div class="wrap">
 <div class="hint" id="hb" style="text-align:right"></div>
 <div class="card" id="offlineCard" style="display:none;border-color:#f85149">
  <div class="hint" style="color:#f85149"><b>MANAGER API UNREACHABLE.</b> This page cannot reach pc_manager on 127.0.0.1:8766, so every value below is STALE and the buttons will not work. If you rebooted the PC: re-run <b>start_pc_manager.cmd</b>. This page recovers automatically (checks every 3 s) once the API answers again - or press Ctrl+F5.</div>
 </div>
 <h2>Network</h2>
 <div class="card">
  <div class="row" style="justify-content:space-between"><span class="k">Detected Tailscale IPv4</span><span class="mono" id="tsIp">-</span></div>
  <div class="hint" id="tsErr"></div>
  <div class="frm"><label>Service port</label><input class="txt" id="cfgPort" type="number" min="1" max="65535"></div>
  <div class="frm"><label>Bind address</label><input class="txt" id="cfgBind" placeholder="auto = Tailscale IPv4, or an explicit IP"></div>
  <div class="frm"><label>Allowed ESP32 / client addresses (one CIDR or IP per line)</label><textarea class="txt" id="cfgAllow"></textarea></div>
  <div class="frm"><label>Service functions (server-side)</label>
   <label class="chkrow"><input type="checkbox" id="fnShutdown"> shutdown</label>
   <label class="chkrow"><input type="checkbox" id="fnRestart"> restart</label>
  </div>
  <div class="row"><button class="btn" id="saveCfg">Save configuration</button><span class="hint" id="saveMsg"></span></div>
  <div class="hint">Saving rewrites config.json (also ACL-hardened) and requires a service restart to take effect.</div>
 </div>

 <h2>Service</h2>
 <div class="card">
  <div class="row" style="justify-content:space-between">
   <span class="k">Status</span>
   <span class="mono" id="svcState">-</span>
  </div>
  <div class="hint" id="svcDetail"></div>
  <div class="row" style="margin-top:8px">
   <button class="btn" id="svcStart">Start</button>
   <button class="btn danger" id="svcStop">Stop</button>
   <button class="btn ghost" id="svcRestart">Restart</button>
   <button class="btn ghost" id="svcTest">Test readiness + auth</button>
  </div>
  <div class="hint" id="testMsg"></div>
 </div>

 <h2>Bearer token</h2>
 <div class="card">
  <div class="row" style="justify-content:space-between"><span class="k">Stored token</span><span class="mono" id="tokMasked">-</span></div>
  <div class="row" style="margin-top:8px"><button class="btn" id="tokGen">Generate new token (shown once)</button></div>
  <div id="tokNewWrap" style="display:none"><pre class="tok" id="tokNew"></pre>
   <div class="hint">Copy this to: ESP32 /pc/settings -&gt; Agent token -&gt; Set token, then Test here. Rotating breaks the old token immediately after the service restarts.</div></div>
 </div>

 <h2>Execution safety</h2>
 <div class="card">
  <div class="hint" id="safetyHint">-</div>
  <div class="hint">dry_run and allow_real_execution are deliberately NOT editable from this UI. They are human-edited config-file gates; the service still executes nothing real while either gate says no.</div>
 </div>

 <h2>Log</h2><ul id="logList"></ul>
 <footer>pc_manager binds 127.0.0.1 only; the agent service is what listens on the Tailscale address.</footer>
</div>
<script>
'use strict';
const $ = function(id){ return document.getElementById(id); };
let formFilled = false;
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
async function api(path, body){
  const opts = { method: body === undefined ? 'GET' : 'POST' };
  if (body !== undefined) {
    opts.headers = { 'Content-Type': 'application/json' };
    opts.body = JSON.stringify(body);
  }
  try {
    const r = await fetch(path, opts);
    let j = null;
    try { j = await r.json(); } catch(e){}
    return { status: r.status, json: j };
  } catch (e) {
    return { status: 0, json: null, error: String(e) };
  }
}
function setBadge(el, ok, text){
  el.textContent = text;
  el.className = 'badge ' + (ok === true ? 'ok' : ok === false ? 'bad' : '');
}
async function refresh(){
  const r = await api('/api/local/status');
  if (r.status !== 200 || !r.json || !r.json.ok) {
    $('offlineCard').style.display = 'block';
    $('hb').textContent = 'API unreachable (' + (r.error || ('HTTP ' + r.status)) + ') - page values are stale';
    setBadge($('bDry'), undefined, 'MODE ?');
    setBadge($('bSvc'), undefined, 'SERVICE ?');
    setBadge($('bTs'), undefined, 'TAILSCALE ?');
    return;
  }
  $('offlineCard').style.display = 'none';
  const st = r.json;
  $('hb').textContent = 'updated ' + new Date().toLocaleTimeString() + ' - config rev ' + (st.config_rev || 1);
  $('tsIp').textContent = st.tailscale.ipv4 || '(not detected)';
  $('tsErr').textContent = st.tailscale.error || '';
  setBadge($('bTs'), !!st.tailscale.ipv4, st.tailscale.ipv4 ? 'TAILSCALE ok' : 'TAILSCALE ?');
  const s = st.service;
  setBadge($('bSvc'), s.running, s.running ? 'SERVICE running' : 'SERVICE stopped');
  $('svcState').textContent = s.running
    ? ('running (pid ' + s.pid + ') - ' + (s.reachable
        ? (s.authenticated
            ? 'reachable, authenticated, runtime: ' + (s.dry_run ? 'DRY-RUN' : 'REAL')
            : 'reachable, AUTH FAILED (' + s.http_status + ')')
        : 'NOT reachable (' + (s.error || '?') + ')'))
    : 'stopped';
  const c = st.config;
  const effDry = !!(c.dry_run || !c.allow_real_execution);
  setBadge($('bDry'), null, effDry ? 'DRY-RUN on' : 'REAL mode');
  $('bDry').className = 'badge ' + (effDry ? 'lock' : 'bad');
  $('safetyHint').textContent = effDry
    ? ('Effective mode: DRY-RUN (dry_run=' + c.dry_run + ', allow_real_execution=' + c.allow_real_execution + '). No real Windows operation will run.')
    : ('WARNING: REAL execution enabled (dry_run=' + c.dry_run + ', allow_real_execution=' + c.allow_real_execution + '). Human-triggered shutdown/restart from the ESP32 will really execute.');
  $('tokMasked').textContent = c.token_set ? c.token_masked : '(none)';
  if (!formFilled) {
    $('cfgPort').value = c.port;
    $('cfgBind').value = c.bind_host;
    $('cfgAllow').value = (c.allow_clients || []).join('\n');
    $('fnShutdown').checked = !!c.functions.shutdown;
    $('fnRestart').checked = !!c.functions.restart;
    formFilled = true;
  }
}
$('saveCfg').addEventListener('click', async function(){
  const allow = $('cfgAllow').value.split('\n').map(function(s){ return s.trim(); }).filter(function(s){ return s; });
  const body = {
    port: parseInt($('cfgPort').value, 10),
    bind_host: $('cfgBind').value.trim() || 'auto',
    allow_clients: allow,
    functions: { shutdown: $('fnShutdown').checked, restart: $('fnRestart').checked }
  };
  const r = await api('/api/local/config', body);
  if (r.status === 200 && r.json && r.json.ok) {
    $('saveMsg').textContent = 'Saved (ACL: ' + r.json.acl + '). Restart the service to apply.';
    log('configuration saved');
    formFilled = false;
    refresh();
  } else {
    $('saveMsg').textContent = 'Save failed: ' + ((r.json && r.json.detail) || r.error || r.status);
  }
});
function act(name, r){ log(name + ' -> ' + (r.json ? JSON.stringify(r.json) : ('FAILED: ' + (r.error || ('HTTP ' + r.status))))); }
$('svcStart').addEventListener('click', async function(){ const r = await api('/api/local/service/start', {}); act('start', r); refresh(); });
$('svcStop').addEventListener('click', async function(){ const r = await api('/api/local/service/stop', {}); act('stop', r); refresh(); });
$('svcRestart').addEventListener('click', async function(){ const r = await api('/api/local/service/restart', {}); act('restart', r); refresh(); });
$('svcTest').addEventListener('click', async function(){
  $('testMsg').textContent = 'Testing...';
  const r = await api('/api/local/test', {});
  if (!r.json) { $('testMsg').textContent = 'manager API unreachable (' + (r.error || r.status) + ')'; return; }
  const j = r.json;
  $('testMsg').textContent = j.reachable
    ? (j.authenticated ? 'TEST OK: reachable and authenticated.' : 'Reachable but NOT authenticated (HTTP ' + j.http_status + ').')
    : ('NOT reachable (' + (j.error || '?') + ').');
  log('test: ' + $('testMsg').textContent);
});
$('tokGen').addEventListener('click', async function(){
  const r = await api('/api/local/token/rotate', {});
  if (r.status === 200 && r.json && r.json.token) {
    $('tokNew').textContent = r.json.token;
    $('tokNewWrap').style.display = 'block';
    log('token rotated - copy it into the ESP32 Settings UI now');
    refresh();
  } else {
    log('token rotate failed: ' + ((r.json && r.json.error) || r.error || r.status));
  }
});
refresh();
setInterval(refresh, 3000);
</script>
</body></html>
"""

def main() -> int:
    ap = argparse.ArgumentParser(description="pc_manager " + VERSION)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--config", default="config.json")
    args = ap.parse_args()

    cfg_path = Path(args.config)
    if not cfg_path.is_absolute():
        cfg_path = HERE / cfg_path

    handlers = [logging.StreamHandler()]
    try:
        handlers.append(logging.FileHandler(HERE / "manager.log",
                                            encoding="utf-8"))
    except OSError:
        pass
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s %(message)s",
                        handlers=handlers)
    try:
        cfg = load_cfg(cfg_path)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"cannot load {cfg_path}: {exc}", file=sys.stderr)
        return 2

    if port_in_use("127.0.0.1", args.port):
        print(f"cannot start: something is already listening on "
              f"127.0.0.1:{args.port} (another pc_manager?). Close it "
              f"first - start_pc_manager.cmd kills stale managers - "
              f"or pass a different --port.", file=sys.stderr)
        return 3

    try:
        srv = ManagerServer(("127.0.0.1", args.port), cfg, cfg_path)
    except OSError as exc:
        print(f"cannot bind 127.0.0.1:{args.port}: {exc}", file=sys.stderr)
        return 3
    log.info("pc_manager %s on http://127.0.0.1:%d (config: %s)",
             VERSION, args.port, cfg_path)
    log.info("management UI is LOCAL-ONLY; dry-run gates are not editable here")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        log.info("stopping on Ctrl+C")
    finally:
        srv.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())



