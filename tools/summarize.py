#!/usr/bin/env python3
"""Aggregate results/*.txt written by run_all.bat (or wine_run.sh output files).
usage: summarize.py <results_dir> [--failures]"""
import glob, os, re, sys
d = sys.argv[1]; show = "--failures" in sys.argv
tt = tf = ts = cf = 0
rows = []
for f in sorted(glob.glob(os.path.join(d, "*.txt"))):
    txt = open(f, errors="replace").read()
    m = re.findall(r"SUMMARY tests=(\d+) failed=(\d+) skipped=(\d+) checks_pass=(\d+) checks_fail=(\d+)", txt)
    if not m: rows.append((os.path.basename(f), "NO SUMMARY (crash/timeout?)")); continue
    t, fl, sk, cp, cfl = map(int, m[-1]); tt += t; tf += fl; ts += sk; cf += cfl
    rows.append((os.path.basename(f), f"tests={t} failed={fl} skipped={sk} checks_failed={cfl}"))
    if show and fl:
        cur = None
        for line in txt.splitlines():
            if line.startswith("RUN "): cur = line[5:]
            elif line.startswith("  FAIL"): print(f"{os.path.basename(f)}: {cur}: {line.strip()}")
for r in rows: print(f"{r[0]:40} {r[1]}")
print(f"TOTAL tests={tt} failed={tf} skipped={ts} checks_failed={cf}")
