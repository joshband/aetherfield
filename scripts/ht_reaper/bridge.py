#!/usr/bin/env python3
"""Drive a running REAPER MCP bridge directly through its file mailbox.

The bridge (Scripts/reaper_mcp_bridge.lua) polls
Scripts/mcp_bridge_data/request_<slot>.json and answers in response_<slot>.json.
Talking to it here means the harness needs only REAPER plus the bridge script --
no Python MCP server process, and no MCP client.

    python3 bridge.py call GetAppVersion
    python3 bridge.py run /abs/path/to/script.lua
"""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

BRIDGE_DIR = Path.home() / "Library/Application Support/REAPER/Scripts/mcp_bridge_data"
SLOT = os.environ.get("HT_BRIDGE_SLOT", "41")  # kept clear of the MCP server's low slots
TIMEOUT_S = 180.0


class BridgeError(RuntimeError):
    pass


def call(func: str, *args, timeout: float = TIMEOUT_S):
    """Invoke a ReaScript API function in REAPER and return its result."""
    req = BRIDGE_DIR / f"request_{SLOT}.json"
    res = BRIDGE_DIR / f"response_{SLOT}.json"
    if not BRIDGE_DIR.is_dir():
        raise BridgeError(f"bridge directory missing: {BRIDGE_DIR}")

    res.unlink(missing_ok=True)
    req.write_text(json.dumps({"func": func, "args": list(args)}))

    deadline = time.time() + timeout
    while time.time() < deadline:
        if res.exists():
            try:
                payload = json.loads(res.read_text())
            except json.JSONDecodeError:
                time.sleep(0.02)  # bridge mid-write
                continue
            res.unlink(missing_ok=True)
            if not payload.get("ok", False):
                raise BridgeError(f"{func}: {payload.get('error', payload)}")
            return payload.get("ret")
        time.sleep(0.05)

    req.unlink(missing_ok=True)
    raise BridgeError(
        f"{func}: no response in {timeout:.0f}s -- is REAPER running with "
        "reaper_mcp_bridge.lua active? (a modal dialog in REAPER also stalls it)"
    )


def ping() -> str:
    return call("GetAppVersion", timeout=10)


def run_script(path: str | Path, timeout: float = TIMEOUT_S) -> None:
    """Register a .lua file as a REAPER action and run it."""
    path = Path(path).expanduser().resolve()
    if not path.is_file():
        raise BridgeError(f"no such script: {path}")

    cmd = call("AddRemoveReaScript", True, 0, str(path), True)
    if isinstance(cmd, list):
        cmd = cmd[0]
    if not cmd or int(cmd) == 0:
        raise BridgeError(f"REAPER refused to register {path.name} as an action")
    call("Main_OnCommand", int(cmd), 0, timeout=timeout)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    mode = sys.argv[1]
    try:
        if mode == "call":
            args = [json.loads(a) if a[:1] in "[{-0123456789tfn\"" else a
                    for a in sys.argv[3:]]
            print(json.dumps(call(sys.argv[2], *args), indent=2))
        elif mode == "run":
            run_script(sys.argv[2])
            print(f"ran {sys.argv[2]}")
        elif mode == "ping":
            print(ping())
        else:
            print(f"unknown mode {mode!r}")
            return 2
    except BridgeError as e:
        print(f"bridge error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
