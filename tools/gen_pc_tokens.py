#!/usr/bin/env python3
"""Generate PC-control tokens into examples/pc_control/sdkconfig.credentials.

Creates (or refreshes) the gitignored credentials file used by the
pc_control example's first sdkconfig generation:

  CONFIG_PCCTL_API_TOKEN        browser UI -> ESP32 mutations
  CONFIG_PCCTL_WIN_AGENT_TOKEN  ESP32 -> tools/pc_agent bearer token

Other lines in the file (WiFi credentials, auth key) are preserved when the
script is re-run. Re-running rotates the tokens; the change reaches the
firmware on the next sdkconfig regeneration (delete
examples/pc_control/sdkconfig, or build fresh).

Usage:
    python tools/gen_pc_tokens.py
"""

import pathlib
import secrets

REPO = pathlib.Path(__file__).resolve().parents[1]
CRED = REPO / "examples" / "pc_control" / "sdkconfig.credentials"

DEFAULTS = {
    "CONFIG_ML_WIFI_SSID": '""',
    "CONFIG_ML_WIFI_PASSWORD": '""',
    "CONFIG_ML_TAILSCALE_AUTH_KEY": '""',
}


def main() -> int:
    api = secrets.token_hex(24)
    agent = secrets.token_hex(24)

    lines = []
    seen = set()
    if CRED.exists():
        for raw in CRED.read_text(encoding="utf-8").splitlines():
            key = raw.split("=", 1)[0].strip()
            if key == "CONFIG_PCCTL_API_TOKEN":
                raw = f'CONFIG_PCCTL_API_TOKEN="{api}"'
            elif key == "CONFIG_PCCTL_WIN_AGENT_TOKEN":
                raw = f'CONFIG_PCCTL_WIN_AGENT_TOKEN="{agent}"'
            if key:
                seen.add(key)
            lines.append(raw)

    for key, val in DEFAULTS.items():
        if key not in seen:
            lines.append(f"{key}={val}")
    if "CONFIG_PCCTL_API_TOKEN" not in seen:
        lines.append(f'CONFIG_PCCTL_API_TOKEN="{api}"')
    if "CONFIG_PCCTL_WIN_AGENT_TOKEN" not in seen:
        lines.append(f'CONFIG_PCCTL_WIN_AGENT_TOKEN="{agent}"')

    CRED.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {CRED}")
    print(f"API_TOKEN={api}")
    print(f"AGENT_TOKEN={agent}")
    print("note: applies on the next sdkconfig generation "
          "(delete examples/pc_control/sdkconfig or build fresh)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
