#!/usr/bin/env python3
"""pc_agent - minimal authenticated HTTP control service for Windows.

Executes ONLY a fixed allowlist of Windows operations (graceful shutdown /
restart via shutdown.exe, and cancelling a pending shutdown). Ships in
DRY-RUN mode: with the default config (dry_run=true and
allow_real_execution=false) no Windows shutdown/restart subprocess is ever
spawned - requests return structured "simulated" responses instead.

Real execution is a deliberate HUMAN-ONLY configuration change (both flags
must be flipped in config.json after validation). Automated testing for
this project never enables it.

Stdlib only. Python 3.11+ (tested on 3.13).

Endpoints (all require "Authorization: Bearer <token>"):
  GET  /api/v1/status           health, dry-run state, pending operation
  POST /api/v1/shutdown         {"delay_s": N}  -> schedules graceful shutdown
  POST /api/v1/restart          {"delay_s": N}  -> schedules graceful restart
  POST /api/v1/cancel-shutdown  {}              -> cancels a pending operation

Usage:
  python pc_agent.py --config config.json
  python pc_agent.py --config config.json --self-test
"""

import argparse
import hmac
import ipaddress
import json
import logging
import logging.handlers
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import msvcrt
except ImportError:  # non-Windows dev box - the instance lock is skipped
    msvcrt = None

VERSION = "0.1.1"
MAX_BODY = 8 * 1024
DEFAULT_DELAY_S = 60
MIN_DELAY_S = 5
MAX_DELAY_S = 3600
# bind_host "auto": how long to poll for the Tailscale IPv4 before falling
# back to 127.0.0.1 (a logon can race Tailscale's own start-up).
BIND_WAIT_DEFAULT_S = 120
BIND_RETRY_INTERVAL_S = 5
# Single-instance lock file, created next to the config file.
INSTANCE_LOCK_NAME = "pc_agent.lock"
# Console children of a windowless (pythonw) process would each get a
# fresh visible console window - hide them all.
CREATE_NO_WINDOW = 0x08000000

log = logging.getLogger("pc_agent")


def load_config(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    cfg.setdefault("bind_host", "auto")   # "auto" = Tailscale IPv4, else 127.0.0.1
    cfg.setdefault("bind_wait_s", BIND_WAIT_DEFAULT_S)  # tailnet wait at start
    cfg.setdefault("port", 8765)
    cfg.setdefault("token", "")
    cfg.setdefault("dry_run", True)               # hard default ON
    cfg.setdefault("allow_real_execution", False) # second gate for real ops
    cfg.setdefault("allow_no_token", False)
    cfg.setdefault("allow_clients", ["100.64.0.0/10", "127.0.0.1"])
    cfg.setdefault("default_delay_s", DEFAULT_DELAY_S)
    cfg.setdefault("functions", {"shutdown": True, "restart": True})
    cfg.setdefault("log_file", "pc_agent.log")
    return cfg


def tailscale_ipv4():
    """Return the host's Tailscale IPv4 address, or None."""
    try:
        out = subprocess.run(["tailscale", "ip", "-4"], capture_output=True,
                             text=True, timeout=5,
                             creationflags=CREATE_NO_WINDOW)
        if out.returncode == 0:
            for line in out.stdout.splitlines():
                line = line.strip()
                if line:
                    return line
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def resolve_bind_host(cfg: dict) -> str:
    """Resolve "auto" to the Tailscale IPv4, retrying at start-up.

    At logon the agent can start before Tailscale has finished bringing
    its interface up; falling back to 127.0.0.1 immediately would strand
    the agent off the tailnet (the ESP32 then shows "svc: offline" until
    a manual restart). Poll for up to bind_wait_s seconds instead.
    """
    host = str(cfg.get("bind_host", "auto"))
    if host != "auto":
        return host
    try:
        wait_s = max(0, int(cfg.get("bind_wait_s", BIND_WAIT_DEFAULT_S)))
    except (TypeError, ValueError):
        wait_s = BIND_WAIT_DEFAULT_S
    deadline = time.monotonic() + wait_s
    attempt = 0
    while True:
        attempt += 1
        ts_ip = tailscale_ipv4()
        if ts_ip:
            log.info("bind host auto-detected from Tailscale: %s (attempt %d)",
                     ts_ip, attempt)
            return ts_ip
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        log.info("Tailscale IP not ready yet (attempt %d) - retrying in %.0fs",
                 attempt, min(BIND_RETRY_INTERVAL_S, remaining))
        time.sleep(min(BIND_RETRY_INTERVAL_S, remaining))
    log.warning("Tailscale IP not found after %ds - binding to 127.0.0.1 only",
                wait_s)
    return "127.0.0.1"


def port_in_use(host: str, port: int) -> bool:
    """True when something already accepts connections on host:port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        return s.connect_ex((host, port)) == 0


def acquire_instance_lock(path: str):
    """Take the single-instance lock file for this process's lifetime.

    Returns an open file object on success (keep it referenced!), True
    when the platform has no msvcrt (lock skipped), or None when another
    instance already holds the lock. The OS releases the lock when the
    process exits, so a crash never leaves it stuck."""
    if msvcrt is None:  # non-Windows dev box
        return True
    try:
        handle = open(path, "a+")
    except OSError:
        return True  # cannot create the lock file - keep permissive
    try:
        handle.seek(0)
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        handle.close()
        return None
    handle.seek(0)
    handle.truncate()
    handle.write("%d\n" % os.getpid())
    handle.flush()
    return handle


class Pending:
    """The single pending operation state (thread-safe via agent lock)."""

    def __init__(self):
        self.operation = None      # "shutdown" / "restart" / None
        self.deadline = 0.0        # monotonic time when the task fires
        self.delay_s = 0
        self.dry_run = True

    def snapshot(self):
        """Return the pending operation, or None. Expired entries clear
        themselves - the dry-run window simply ends; a real scheduled
        operation would have fired by that deadline."""
        if self.operation is None:
            return None
        if time.monotonic() >= self.deadline:
            self.clear()
            return None
        remaining = max(0, int(self.deadline - time.monotonic()))
        return {"operation": self.operation, "delay_s": self.delay_s,
                "remaining_s": remaining}

    def start(self, operation: str, delay_s: int, dry_run: bool):
        self.operation = operation
        self.delay_s = delay_s
        self.deadline = time.monotonic() + delay_s
        self.dry_run = dry_run

    def clear(self):
        self.operation = None
        self.deadline = 0.0


def client_allowed(addr: str, allow_list) -> bool:
    try:
        ip = ipaddress.ip_address(addr)
    except ValueError:
        return False
    for entry in allow_list:
        try:
            if ip in ipaddress.ip_network(entry, strict=False):
                return True
        except ValueError:
            continue
    return False


def run_command(cmd, timeout=10):
    """Run an allowlisted command list. Returns (ok, detail-without-secrets)."""
    try:
        proc = subprocess.run(cmd, shell=False, capture_output=True,
                              timeout=timeout,
                              creationflags=CREATE_NO_WINDOW)
    except (OSError, subprocess.SubprocessError) as exc:
        return False, f"subprocess failed: {exc.__class__.__name__}"
    if proc.returncode != 0:
        return False, f"exit code {proc.returncode}"
    return True, "ok"

class AgentHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "pc_agent/" + VERSION

    # silence the default stderr logging; structured logging happens below
    def log_message(self, fmt, *args):
        pass

    # ---------- helpers ----------

    def _client_ok(self):
        addr = self.client_address[0]
        if not client_allowed(addr, self.server.cfg["allow_clients"]):
            log.warning("REJECT %s %s client=%s (not in allow list)",
                        self.command, self.path, addr)
            self._send(403, {"ok": False, "error": "client_not_allowed"})
            return False
        return True

    def _authorized(self):
        cfg = self.server.cfg
        token = cfg.get("token", "")
        if not token:
            if cfg.get("allow_no_token", False):
                return True
            self._send(401, {"ok": False, "error": "token_not_configured"})
            return False
        header = self.headers.get("Authorization", "")
        presented = header[7:] if header.startswith("Bearer ") else ""
        if not presented or not hmac.compare_digest(presented, token):
            log.warning("AUTH FAIL %s %s client=%s", self.command, self.path,
                        self.client_address[0])
            self._send(401, {"ok": False, "error": "unauthorized"})
            return False
        return True

    def _read_json(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except (TypeError, ValueError):
            return None, "bad_content_length"
        if length > MAX_BODY:
            return None, "body_too_large"
        raw = self.rfile.read(length) if length > 0 else b""
        if not raw:
            return {}, None
        try:
            obj = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return None, "invalid_json"
        if not isinstance(obj, dict):
            return None, "invalid_json"
        return obj, None

    def _send(self, code, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        log.info("%s %s client=%s -> %d", self.command, self.path,
                 self.client_address[0], code)
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _delay_from_body(self, obj, default):
        delay = obj.get("delay_s", default)
        if isinstance(delay, bool) or not isinstance(delay, (int, float)):
            return None
        delay = int(delay)
        if delay < MIN_DELAY_S or delay > MAX_DELAY_S:
            return None
        return delay

    # ---------- routes ----------

    def do_GET(self):
        if not self._client_ok():
            return
        path = self.path.rstrip("/")
        if path == "/api/v1/status":
            if not self._authorized():
                return
            self._status()
        elif path == "/api/v1/config/status":
            if not self._authorized():
                return
            self._config_status()
        else:
            self._send(404, {"ok": False, "error": "not_found"})

    def do_POST(self):
        if not self._client_ok():
            return
        path = self.path.rstrip("/")
        if path not in ("/api/v1/shutdown", "/api/v1/restart",
                        "/api/v1/cancel-shutdown"):
            self._send(404, {"ok": False, "error": "not_found"})
            return
        if not self._authorized():
            return
        obj, err = self._read_json()
        if err == "body_too_large":
            self._send(413, {"ok": False, "error": err})
            return
        if err:
            self._send(400, {"ok": False, "error": err})
            return
        if path == "/api/v1/cancel-shutdown":
            self._cancel()
        else:
            self._schedule("shutdown" if path == "/api/v1/shutdown" else "restart",
                           obj)

    # ---------- operations ----------

    def _status(self):
        srv = self.server
        with srv.lock:
            pending = srv.pending.snapshot()
        effective_dry = bool(srv.cfg.get("dry_run")
                             or not srv.cfg.get("allow_real_execution"))
        self._send(200, {
            "ok": True,
            "app": "pc_agent",
            "version": VERSION,
            "dry_run": effective_dry,
            "allow_real_execution": bool(srv.cfg.get("allow_real_execution")),
            "host": socket.gethostname(),
            "os": "windows",
            "uptime_s": int(time.monotonic() - srv.started),
            "pending": pending,
        })

    def _config_status(self):
        srv = self.server
        cfg = srv.cfg
        effective_dry = bool(cfg.get("dry_run")
                             or not cfg.get("allow_real_execution"))
        with srv.lock:
            pending = srv.pending.snapshot()
        self._send(200, {
            "ok": True,
            "app": "pc_agent",
            "version": VERSION,
            "port": int(cfg.get("port", 8765)),
            "bind_host": srv.bound_host,
            "allow_clients": list(cfg.get("allow_clients", [])),
            "functions": dict(cfg.get("functions", {})),
            "dry_run": effective_dry,
            "allow_real_execution": bool(cfg.get("allow_real_execution")),
            "token_set": bool(cfg.get("token")),
            "uptime_s": int(time.monotonic() - srv.started),
            "pending": pending,
        })

    def _schedule(self, operation, obj):
        srv = self.server
        default = int(srv.cfg.get("default_delay_s", DEFAULT_DELAY_S))
        delay = self._delay_from_body(obj, default)
        if delay is None:
            self._send(400, {"ok": False, "error": "invalid_delay",
                             "detail": f"delay_s must be {MIN_DELAY_S}..{MAX_DELAY_S}"})
            return

        if not srv.cfg.get("functions", {}).get(operation, True):
            log.warning("BLOCKED %s (disabled server-side)", operation)
            self._send(403, {"ok": False, "error": "function_disabled",
                             "detail": f"{operation} is disabled in the agent configuration"})
            return

        effective_dry = bool(srv.cfg.get("dry_run")
                             or not srv.cfg.get("allow_real_execution"))
        already = None
        conflict = None
        with srv.lock:
            current = srv.pending.snapshot()
            if current is None:
                srv.pending.start(operation, delay, effective_dry)
            elif current["operation"] == operation:
                already = current
            else:
                conflict = current

        if already is not None:
            log.info("duplicate %s ignored (already pending)", operation)
            self._send(200, {"ok": True, "already_pending": True, "pending": already})
            return
        if conflict is not None:
            self._send(409, {"ok": False, "error": "busy", "pending": conflict})
            return

        if effective_dry:
            log.info("DRY-RUN %s scheduled (delay=%ss, nothing executed)",
                     operation, delay)
            self._send(202, {"ok": True, "scheduled": True,
                             "operation": operation, "delay_s": delay,
                             "dry_run": True})
            return

        verb = "/s" if operation == "shutdown" else "/r"
        ok, detail = run_command(["shutdown", verb, "/t", str(delay)])
        log.info("REAL %s requested (delay=%ss) -> %s", operation, delay, detail)
        if not ok:
            with srv.lock:
                srv.pending.clear()
            self._send(500, {"ok": False, "error": "exec_failed", "detail": detail})
            return
        self._send(202, {"ok": True, "scheduled": True, "operation": operation,
                         "delay_s": delay, "dry_run": False})

    def _cancel(self):
        srv = self.server
        with srv.lock:
            current = srv.pending.snapshot()
            was_dry = srv.pending.dry_run
            if current is not None:
                srv.pending.clear()

        if current is None:
            self._send(200, {"ok": True, "cancelled": False,
                             "reason": "none_pending"})
            return
        if was_dry:
            log.info("DRY-RUN cancel acknowledged (%s)", current["operation"])
            self._send(200, {"ok": True, "cancelled": True, "dry_run": True})
            return
        ok, detail = run_command(["shutdown", "/a"])
        log.info("REAL cancel via 'shutdown /a' -> %s", detail)
        # note: 'shutdown /a' aborts any pending system shutdown/restart,
        # including one started outside this agent (documented limitation)
        self._send(200, {"ok": True, "cancelled": ok, "reason": detail})

class AgentServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr, cfg):
        super().__init__(addr, AgentHandler)
        self.cfg = cfg
        self.bound_host = addr[0]
        self.lock = threading.Lock()
        self.pending = Pending()
        self.started = time.monotonic()


def self_test(cfg) -> int:
    """Validation/auth/subprocess plumbing without any real shutdown op."""
    failures = 0

    def check(name, cond):
        nonlocal failures
        print(("PASS " if cond else "FAIL ") + name)
        if not cond:
            failures += 1

    check("token compare accepts correct", hmac.compare_digest("abc", "abc"))
    check("token compare rejects wrong", not hmac.compare_digest("abc", "abd"))
    check("client allow 100.x", client_allowed("100.64.0.10", cfg["allow_clients"]))
    check("client allow localhost", client_allowed("127.0.0.1", cfg["allow_clients"]))
    check("client reject public", not client_allowed("8.8.8.8", cfg["allow_clients"]))
    ok, _ = run_command(["cmd.exe", "/c", "exit", "0"])
    check("subprocess runner success path", ok)
    ok, detail = run_command(["cmd.exe", "/c", "exit", "1"])
    check("subprocess runner failure path", (not ok) and ("exit code 1" in detail))
    ok, _ = run_command(["definitely-not-a-real-command-xyz"])
    check("subprocess runner OSError path", not ok)
    if msvcrt is not None:
        lock_path = os.path.join(tempfile.gettempdir(), "pc_agent_selftest.lock")
        holder = acquire_instance_lock(lock_path)
        rival = acquire_instance_lock(lock_path)
        check("instance lock blocks a second holder",
              holder is not None and rival is None)
        for handle in (holder, rival):
            if hasattr(handle, "close"):
                handle.close()
    print(f"self-test complete: {failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description="pc_agent " + VERSION)
    ap.add_argument("--config", default="config.json")
    ap.add_argument("--self-test", action="store_true",
                    help="exercise validation/auth/subprocess plumbing only; "
                         "never touches a real shutdown/restart operation")
    args = ap.parse_args()

    try:
        cfg = load_config(args.config)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"cannot load config {args.config}: {exc}", file=sys.stderr)
        return 2

    if args.self_test:
        return self_test(cfg)

    if not cfg.get("token") and not cfg.get("allow_no_token", False):
        print("refusing to start: no token configured (set \"token\" or "
              "explicitly allow_no_token)", file=sys.stderr)
        return 2

    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s %(message)s")
    log_file = cfg.get("log_file")
    if log_file:
        fh = logging.handlers.RotatingFileHandler(
            log_file, maxBytes=1_000_000, backupCount=3, encoding="utf-8")
        fh.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
        logging.getLogger().addHandler(fh)

    # One agent per machine: a duplicate start (e.g. the logon Task
    # Scheduler task racing the manager's own start path) must exit
    # instead of double-binding the port.
    lock_path = os.path.join(os.path.dirname(os.path.abspath(args.config)),
                             INSTANCE_LOCK_NAME)
    instance_lock = acquire_instance_lock(lock_path)  # held until exit
    if instance_lock is None:
        log.warning("another pc_agent instance is already running "
                    "(lock %s held) - exiting", lock_path)
        return 0  # benign: the other instance is serving

    host = resolve_bind_host(cfg)
    port = int(cfg.get("port", 8765))
    for probe_host in dict.fromkeys((host, "127.0.0.1")):
        if port_in_use(probe_host, port):
            log.warning("port %d on %s already accepts connections - "
                        "another instance is probably running; exiting",
                        port, probe_host)
            return 3
    effective_dry = bool(cfg.get("dry_run")
                         or not cfg.get("allow_real_execution"))
    srv = AgentServer((host, port), cfg)
    log.info("pc_agent %s listening on %s:%d (dry_run=%s, clients=%s)",
             VERSION, host, port, effective_dry, ",".join(cfg["allow_clients"]))
    if effective_dry:
        log.info("DRY-RUN MODE: no real Windows shutdown/restart will be executed")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        log.info("stopping on Ctrl+C")
    finally:
        srv.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


