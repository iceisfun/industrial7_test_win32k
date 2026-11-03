#!/usr/bin/env python3
"""Parse the COM1 log of a Windows 10 run (see win10_pack.py) into per-exe and per-test results.

usage: win10_results.py <serial.log> [--format tsv|json] [--tests] [--failures] [-o FILE]

Grammar of the framed lines (everything else in the log, such as boot noise, is ignored):
  W32K begin ...            W32K os ...             W32K mode <adm|std|user>
  W32K userkind <real-user|unavailable>   (user mode: whether the throwaway standard account was usable; if unavailable
                            no exe of that mode runs and the mode has no rows)
  W32K start <exe>          W32K out <exe> | <line>   (the exe's own stdout, RUN lines optional)
  W32K rc <exe> <code|TIMEOUT|MISSING> <secs>        W32K done
Per (mode, exe) the table has: tests failed skipped checks_pass checks_fail rc secs status, where status is
  OK       SUMMARY seen and the exe returned 0 or 1 (1 = some test failed; counts say which)
  TIMEOUT  killed by the runner      NOSUMMARY  exited without a SUMMARY line (crash)
  HANG     no rc line (the log stops while the exe runs)   MISSING  exe not in the payload
  (exes with no tests, such as the identity probe, report status OUT)
--tests prints one row per test (mode, exe, test, OK|FAIL|SKIP); --failures prints the FAIL/SKIP detail
lines. The exit code is 0 when the run finished ("W32K done") and every exe has status OK, else 1."""
import argparse, json, re, sys

SUM = re.compile(r"SUMMARY tests=(\d+) failed=(\d+) skipped=(\d+) checks_pass=(\d+) checks_fail=(\d+)")
FR = re.compile(r"W32K (begin|os|mode|userkind|start|out|rc|done)\b ?(.*)")

def parse(text):
    mode = "?"; done = False; info = {}
    runs = {}   # (mode, exe) -> dict
    order = []
    cur = None
    for raw in text.splitlines():
        line = raw.rstrip("\r\n").strip("\x00")
        i = line.find("W32K ")
        if i < 0: continue
        m = FR.match(line[i:])
        if not m: continue
        kind, rest = m.groups()
        if kind in ("begin", "os", "userkind"): info[kind] = rest.strip()
        elif kind == "mode": mode = rest.strip()
        elif kind == "done": done = True
        elif kind == "start":
            exe = rest.strip(); cur = (mode, exe)
            runs[cur] = dict(mode=mode, exe=exe, tests=0, failed=0, skipped=0, checks_pass=0, checks_fail=0,
                             rc=None, secs=None, status="HANG", results=[], details=[], summary=False)
            order.append(cur)
        elif kind == "out":
            exe, _, body = rest.partition(" | ")
            r = runs.get((mode, exe.strip()))
            if r is None: continue
            s = SUM.search(body)
            if s and body.startswith("SUMMARY"):
                (r["tests"], r["failed"], r["skipped"], r["checks_pass"], r["checks_fail"]) = map(int, s.groups())
                r["summary"] = True
            elif re.match(r"(OK  |FAIL|SKIP) \S", body):
                r["results"].append((body[:4].strip(), body[5:].strip()))
            elif body.lstrip().startswith(("FAIL", "SKIP", "NOTE")):
                r["details"].append(body.strip())
        elif kind == "rc":
            f = rest.split()
            r = runs.get((mode, f[0])) if f else None
            if r is None: continue
            r["rc"] = f[1]; r["secs"] = float(f[2]) if len(f) > 2 else None
            if f[1] in ("TIMEOUT", "MISSING"): r["status"] = f[1]
            elif r["summary"]: r["status"] = "OK" if f[1] in ("0", "1") else "NOSUMMARY"
            elif r["results"] or r["details"]: r["status"] = "NOSUMMARY"
            else: r["status"] = "OUT"
    rows = [runs[k] for k in order]
    return dict(done=done, info=info, runs=rows)

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("log"); ap.add_argument("--format", choices=["tsv", "json"], default="tsv")
    ap.add_argument("--tests", action="store_true"); ap.add_argument("--failures", action="store_true")
    ap.add_argument("-o", dest="out")
    a = ap.parse_args(argv)
    res = parse(open(a.log, errors="replace").read())
    out = []
    if a.format == "json":
        for r in res["runs"]: r["results"] = [list(x) for x in r["results"]]
        out.append(json.dumps(res, indent=1))
    elif a.tests:
        out.append("mode\texe\ttest\tresult")
        for r in res["runs"]:
            for st, name in r["results"]: out.append("\t".join((r["mode"], r["exe"], name, st)))
    elif a.failures:
        for r in res["runs"]:
            for d in r["details"]:
                if d.startswith(("FAIL", "SKIP")): out.append("%s\t%s\t%s" % (r["mode"], r["exe"], d))
    else:
        out.append("mode\texe\ttests\tfailed\tskipped\tchecks_pass\tchecks_fail\trc\tsecs\tstatus")
        for r in res["runs"]:
            out.append("\t".join(str(x) for x in (r["mode"], r["exe"], r["tests"], r["failed"], r["skipped"],
                                                  r["checks_pass"], r["checks_fail"], r["rc"], r["secs"], r["status"])))
        out.append("# done=%s %s" % (res["done"], res["info"].get("os", "")))
        if "userkind" in res["info"]: out.append("# userkind=%s" % res["info"]["userkind"])
    text = "\n".join(out) + "\n"
    if a.out: open(a.out, "w").write(text)
    else: sys.stdout.write(text)
    runs = [r for r in res["runs"] if r["status"] != "OUT"]
    if res["info"].get("userkind", "real-user") != "real-user": return 1   # requested user mode was not run
    return 0 if res["done"] and all(r["status"] == "OK" for r in runs) else 1

if __name__ == "__main__":
    sys.exit(main())
