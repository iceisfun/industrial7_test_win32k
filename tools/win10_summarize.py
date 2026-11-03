#!/usr/bin/env python3
"""Write the sanitized per-mode summaries of Windows 10 runs (COM1 logs, see win10_results.py) into a results folder.

usage: win10_summarize.py <out_dir> <serial.log> [<serial.log> ...]

When several logs are given, a later log replaces the (mode, exe) rows of the earlier ones, so a full run can be
combined with re-runs of single areas. For each mode it writes <out_dir>/<mode>/:
  summary.tsv        tests failed skipped checks_pass checks_fail rc status per area (no timing: seconds vary per run)
  failing_tests.tsv  exe, test, result (FAIL|SKIP), reason - the detail line(s) printed before the result line
  failures.tsv       exe, detail - every FAIL/SKIP detail line
Raw logs are never copied. Lines are cut at 400 characters and checked for host-identifying text."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import win10_results as wr

BAD = re.compile(r"[A-Za-z]:\\|\\\\|/home/|@[A-Za-z0-9.-]+\.[a-z]{2,}|\bWIN-[A-Z0-9]{6,}|\\Users\\", re.I)

def reasons(text):
    """(mode, exe) -> list of (result, test, [detail lines before it])."""
    out = {}; mode = "?"; pend = {}
    for raw in text.splitlines():
        i = raw.find("W32K ")
        if i < 0: continue
        line = raw[i:].rstrip("\r\n")
        if line.startswith("W32K mode "): mode = line[10:].strip(); continue
        if line.startswith("W32K start "): pend[(mode, line[11:].strip())] = []; out[(mode, line[11:].strip())] = []; continue
        if not line.startswith("W32K out "): continue
        exe, _, body = line[9:].partition(" | ")
        k = (mode, exe.strip())
        if k not in out: continue
        m = re.match(r"(OK  |FAIL|SKIP) (\S.*)$", body)
        if m:
            st, name = m.group(1).strip(), m.group(2).strip()
            if st != "OK": out[k].append((st, name, [d for d in pend[k] if d.startswith(("FAIL", "SKIP"))]))
            pend[k] = []
        elif body.lstrip().startswith(("FAIL", "SKIP", "NOTE")):
            pend[k].append(body.strip())
    return out

def main(argv):
    if len(argv) < 2: print(__doc__); return 2
    out_dir, logs = argv[0], argv[1:]
    rows = {}; why = {}
    for p in logs:
        text = open(p, errors="replace").read()
        res = wr.parse(text); rs = reasons(text)
        for r in res["runs"]:
            k = (r["mode"], r["exe"]); rows[k] = r; why[k] = rs.get(k, [])
    for mode in sorted({k[0] for k in rows}):
        d = os.path.join(out_dir, mode); os.makedirs(d, exist_ok=True)
        keys = sorted(k for k in rows if k[0] == mode and rows[k]["status"] != "OUT")
        with open(os.path.join(d, "summary.tsv"), "w") as f:
            f.write("exe\ttests\tfailed\tskipped\tchecks_pass\tchecks_fail\trc\tstatus\n")
            for k in keys:
                r = rows[k]; f.write("\t".join(str(x) for x in (k[1], r["tests"], r["failed"], r["skipped"], r["checks_pass"], r["checks_fail"], r["rc"], r["status"])) + "\n")
        with open(os.path.join(d, "failing_tests.tsv"), "w") as f:
            f.write("exe\ttest\tresult\treason\n")
            for k in keys:
                for st, name, ds in why[k]:
                    f.write("\t".join((k[1], name, st, " | ".join(x[:400] for x in ds))) + "\n")
        with open(os.path.join(d, "failures.tsv"), "w") as f:
            f.write("exe\tdetail\n")
            for k in keys:
                for dline in rows[k]["details"]:
                    if dline.startswith(("FAIL", "SKIP")): f.write("%s\t%s\n" % (k[1], dline[:400]))
        for fn in ("summary.tsv", "failing_tests.tsv", "failures.tsv"):
            for n, ln in enumerate(open(os.path.join(d, fn)), 1):
                if BAD.search(ln): print("WARNING: %s/%s:%d looks host-identifying: %s" % (mode, fn, n, ln.strip()[:120]))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
