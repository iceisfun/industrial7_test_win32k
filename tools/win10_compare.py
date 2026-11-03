#!/usr/bin/env python3
"""Compare several Windows 10 run logs (COM1 serial logs, see win10_results.py) for determinism.

usage: win10_compare.py <serial.log> <serial.log> [...] [--lines]

Per (mode, exe) it reports tests whose result differs between the logs and, with --lines, every printed output line
(NOTE, FAIL and SKIP detail, test results) that is not present the same number of times in all logs. Timing fields
(the rc seconds) are ignored. Exit code 0 when all logs agree, else 1."""
import collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import win10_results as wr

def outlines(text):
    d = collections.defaultdict(collections.Counter); mode = "?"
    for raw in text.splitlines():
        i = raw.find("W32K ")
        if i < 0: continue
        line = raw[i:].rstrip("\r\n")
        if line.startswith("W32K mode "): mode = line[10:].strip()
        elif line.startswith("W32K out "):
            exe, _, body = line[9:].partition(" | ")
            body = body.rstrip()
            if body.startswith("SUMMARY"):   # the number of checks varies with the system objects an area enumerates
                body = " ".join(w for w in body.split() if not w.startswith("checks_pass="))
            d[(mode, exe.strip())][body] += 1
    return d

def main(argv):
    lines = "--lines" in argv
    logs = [a for a in argv if not a.startswith("--")]
    texts = [open(p, errors="replace").read() for p in logs]
    parsed = [wr.parse(t) for t in texts]
    bad = 0
    tables = []
    for p in parsed:
        tab = {}
        for r in p["runs"]:
            tab[(r["mode"], r["exe"])] = (r["status"], dict((n, s) for s, n in r["results"]))
        tables.append(tab)
    keys = sorted(set().union(*[t.keys() for t in tables]))
    for k in keys:
        sts = [t.get(k, ("ABSENT", {}))[0] for t in tables]
        if len(set(sts)) > 1: print("%s %s status differs: %s" % (k[0], k[1], sts)); bad += 1
        names = set().union(*[t.get(k, ("", {}))[1].keys() for t in tables])
        for n in sorted(names):
            rs = [t.get(k, ("", {}))[1].get(n, "-") for t in tables]
            if len(set(rs)) > 1: print("%s %s %s: %s" % (k[0], k[1], n, rs)); bad += 1
    if lines:
        ol = [outlines(t) for t in texts]
        for k in keys:
            allk = set().union(*[o.get(k, {}).keys() for o in ol])
            for ln in sorted(allk):
                c = [o.get(k, {}).get(ln, 0) for o in ol]
                if len(set(c)) > 1: print("LINE %s %s %s: %s" % (k[0], k[1], ln[:200], c)); bad += 1
    print("# %d difference(s) over %d logs" % (bad, len(logs)))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
