#!/usr/bin/env python3
"""e2e_check - verify a RUNNING pc_manager (127.0.0.1 only, stdlib only).

Checks:
  1. GET / serves the page (MODE ? placeholder, offline card present);
  2. the served <script> parses - via `node --check` when node is on PATH
     (this catches Python-escape bugs that silently kill all UI JS);
  3. GET /api/local/status answers with ok=true;
  4. POST /api/local/test reports the agent state;
  5. with --start: calls the same service/start endpoint the Start button
     uses, then re-probes until the agent answers (or times out).

Usage:
    python e2e_check.py            # passive checks only
    python e2e_check.py --start    # also start the pc_agent service
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


def get_json(base, path):
    with urllib.request.urlopen(base + path, timeout=15) as r:
        return r.status, json.loads(r.read().decode("utf-8"))


def post_json(base, path, obj):
    req = urllib.request.Request(
        base + path, data=json.dumps(obj).encode("utf-8"),
        headers={"Content-Type": "application/json"}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode("utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser(description="pc_manager end-to-end check")
    ap.add_argument("--base", default="http://127.0.0.1:8766")
    ap.add_argument("--start", action="store_true",
                    help="call the service/start endpoint (starts pc_agent)")
    args = ap.parse_args()
    base = args.base
    fails = 0

    def note(label, ok, extra=""):
        nonlocal fails
        if not ok:
            fails += 1
        suffix = ("  " + str(extra)) if extra else ""
        print(("PASS " if ok else "FAIL ") + label + suffix)

    html = urllib.request.urlopen(base + "/", timeout=15).read().decode("utf-8")
    note("page has MODE ? badge placeholder", "MODE ?" in html)
    note("page has offline card", 'id="offlineCard"' in html)

    js = html.split("<script>", 1)[1].split("</script>", 1)[0]
    node = shutil.which("node")
    if node:
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False,
                                         encoding="utf-8", newline="\n") as f:
            f.write(js)
            tmp_js = f.name
        try:
            r = subprocess.run([node, "--check", tmp_js],
                               capture_output=True, text=True)
            note("served JS parses (node --check)", r.returncode == 0,
                 r.stderr.strip().splitlines()[-1] if r.returncode else "")
        finally:
            os.unlink(tmp_js)
    else:
        print("SKIP served-JS syntax check (node not found on PATH)")

    st, body = get_json(base, "/api/local/status")
    note("status api ok", st == 200 and body.get("ok") is True)
    cfg = body.get("config", {})
    print("  tailscale: %s" % (body.get("tailscale"),))
    print("  config: port=%s funcs=%s dry_run=%s allow_real=%s rev=%s" % (
        cfg.get("port"), cfg.get("functions"), cfg.get("dry_run"),
        cfg.get("allow_real_execution"), body.get("config_rev")))
    print("  service: %s" % (body.get("service"),))

    st, probe = post_json(base, "/api/local/test", {})
    if probe.get("reachable") and probe.get("authenticated"):
        note("agent reachable + authenticated", True)
        note("agent in REAL mode", probe.get("dry_run") is False)
        print("E2E:", "ALL PASS" if fails == 0 else "%d FAIL(s)" % fails)
        return 0 if fails == 0 else 1

    if args.start:
        st, res = post_json(base, "/api/local/service/start", {})
        note("service/start ok", st == 200 and res.get("ok") is True, res)
        for _ in range(6):
            time.sleep(2)
            st, probe = post_json(base, "/api/local/test", {})
            if probe.get("reachable"):
                break
        note("agent reachable after start", bool(probe.get("reachable")),
             probe)
        note("agent authenticated", bool(probe.get("authenticated")))
        note("agent in REAL mode", probe.get("dry_run") is False)
    else:
        print("NOTE: agent not reachable; pass --start to start it here")

    print("E2E:", "ALL PASS" if fails == 0 else "%d FAIL(s)" % fails)
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
