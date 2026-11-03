#!/usr/bin/env python3
"""Emergency cleanup: kill wine process groups started from tp_prefix.* sandboxes (or any prefix whose
path contains a given substring) that have no test exe running. Safe against your real prefixes.
usage: reap_wine.py [prefix-substring=tp_prefix]"""
import os, sys, signal, collections
sub = sys.argv[1] if len(sys.argv) > 1 else "tp_prefix"
HELP = {"winedevice.exe","winhlp32.exe","svchost.exe","services.exe","plugplay.exe","rpcss.exe","explorer.exe","tabtip.exe","wineserver","conhost.exe","start.exe","rundll32.exe","wineboot.exe","winemenubuilder.exe","msiexec.exe"}
def rd(p, mode="rb"):
    try: return open(p, mode).read()
    except Exception: return b"" if mode == "rb" else ""
g = collections.defaultdict(list)
for p in filter(str.isdigit, os.listdir("/proc")):
    pre = next((kv[11:].decode() for kv in rd(f"/proc/{p}/environ").split(b"\0") if kv.startswith(b"WINEPREFIX=")), None)
    if pre and sub in pre: g[pre].append(int(p))
killed = 0
for pre, pids in g.items():
    active = any(rd(f"/proc/{x}/comm", "r").strip() not in HELP and not rd(f"/proc/{x}/comm", "r").startswith("wine") for x in pids)
    if not active:
        for x in pids:
            try: os.kill(x, signal.SIGKILL); killed += 1
            except Exception: pass
print(f"prefixes={len(g)} killed={killed}")
