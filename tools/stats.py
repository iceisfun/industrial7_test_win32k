#!/usr/bin/env python3
"""Regenerate results/STATS.md: areas, tests, categories, syscall coverage and Windows 10 results of the corpus.

usage: stats.py [--no-wine] [--json]

Inputs (all repo-relative):
  tests/<area>/            the areas; a category is the area name up to its first underscore
  build/<area>.exe         test counts come from `<exe> --list` run under Wine (headless, throw-away prefix; the
                           listing only prints the registered test names, no Windows API is called). With --no-wine, or
                           when Wine is missing, the count of an area is taken from its Windows 10 summary and an area
                           with no Windows 10 result is reported as unknown. Build first with `make -j8`.
  tools/coverage.py        syscall coverage (run here; it also rewrites COVERAGE.md)
  results/win10/**/<mode>/summary.tsv   per-area results on Windows 10, mode = adm or std (see tools/win10_summarize.py)
  results/win10/determinism.tsv         one row per group of runs that were compared (hand-kept, see DETERMINISM.md)
Pass/fail/skip per mode: passed = tests - failed - skipped. Areas named dx_* change system state and are not part of the
Windows 10 runs. --json prints the numbers as one JSON object instead of writing results/STATS.md."""
import collections, csv, glob, json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

CATEGORY = {
    "class": "window class registration", "clipboard": "clipboard and DDE", "core": "harness self-test",
    "cursor": "cursors, icons, menus", "dcomp": "composition, flip objects, DxgKrnl negatives",
    "display": "monitors and DPI", "dx": "destructive or state-changing (guarded, not part of the Windows 10 runs)",
    "focus": "focus, capture, attach", "gap": "gap-fill areas for syscall families", "gdi": "GDI objects and drawing",
    "gui": "GUI thread conversion", "hooks": "hooks and win events", "ime": "IME, hotkeys, misc",
    "kbd": "keyboard layouts and state", "kcb": "kernel callbacks", "kprobe": "user-pointer probing",
    "msg": "messages, timers, atoms", "paint": "painting, DC cache, regions", "process": "process and session cleanup",
    "quota": "quotas and limits", "r2": "second-round matrices (internals, tables, quotas, display)",
    "raw": "raw input, touch, pointer", "sc": "syscall layer (stubs, raw calls, dispatch)",
    "scroll": "scroll bars and structures", "sysparams": "system parameters, colors, metrics",
    "teb": "TEB/PEB win32 fields", "user": "USER handle table, shared info, desktop heap",
    "win32k": "mitigation policy, module and system information", "win": "window creation, position, properties",
    "winsta": "window stations and desktops", "x": "extended negative and matrix areas",
}

def areas():
    return sorted(d for d in os.listdir(os.path.join(ROOT, "tests")) if os.path.isdir(os.path.join(ROOT, "tests", d)))

def category(a):
    return a.split("_", 1)[0]

def wine_counts(names):
    wine = shutil.which("wine")
    if not wine: return None
    env = dict(os.environ, WINEDEBUG="-all")
    env.pop("DISPLAY", None); env.pop("WAYLAND_DISPLAY", None)
    pfx = tempfile.mkdtemp(prefix="w32kstats_")
    env["WINEPREFIX"] = pfx
    out = {}
    try:
        for n in names:
            exe = os.path.join(ROOT, "build", n + ".exe")
            if not os.path.isfile(exe): return None
            r = subprocess.run([wine, exe, "--list"], env=env, capture_output=True, text=True, timeout=120)
            out[n] = len([l for l in r.stdout.splitlines() if l.strip()])
    except Exception:
        return None
    finally:
        subprocess.run([wine.replace("wine", "wineserver"), "-k"], env=env, capture_output=True)
        shutil.rmtree(pfx, ignore_errors=True)
    return out

def load_win10():
    """mode -> area -> row dict."""
    res = {"adm": {}, "std": {}}
    for mode in res:
        for p in sorted(glob.glob(os.path.join(ROOT, "results", "win10", "**", mode, "summary.tsv"), recursive=True)):
            for l in open(p):
                if l.startswith(("#", "exe\t")) or not l.strip(): continue
                f = l.rstrip("\n").split("\t")
                if f[7:8] and f[7] != "OK": continue
                res[mode][f[0]] = dict(tests=int(f[1]), failed=int(f[2]), skipped=int(f[3]), checks=int(f[4]), checks_fail=int(f[5]))
    return res

def coverage():
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as t: path = t.name
    env = dict(os.environ, COV_JSON=path)
    subprocess.run([sys.executable, os.path.join(HERE, "coverage.py"), ROOT], env=env, check=True, capture_output=True)
    d = json.load(open(path)); os.unlink(path)
    return d

def main():
    names = areas()
    counts = None if "--no-wine" in sys.argv else wine_counts(names)
    w10 = load_win10()
    if counts is None:
        counts = {n: w10["adm"][n]["tests"] for n in names if n in w10["adm"]}
    known = [n for n in names if n in counts]
    total_tests = sum(counts[n] for n in known)
    cats = collections.OrderedDict()
    for n in names:
        c = cats.setdefault(category(n), [0, 0, 0])
        c[0] += 1
        if n in counts: c[1] += counts[n]; c[2] += 1
    cov = coverage()
    tot = cov["total"]; direct = cov["direct"]; unc = len(cov["direct_uncovered"])
    modes = {}
    for m, rows in w10.items():
        a = len(rows); t = sum(r["tests"] for r in rows.values()); f = sum(r["failed"] for r in rows.values())
        s = sum(r["skipped"] for r in rows.values()); c = sum(r["checks"] for r in rows.values())
        modes[m] = dict(areas=a, tests=t, passed=t - f - s, failed=f, skipped=s, checks=c)
    det = []
    p = os.path.join(ROOT, "results", "win10", "determinism.tsv")
    if os.path.isfile(p):
        det = [l.rstrip("\n").split("\t") for l in open(p) if l.strip() and not l.startswith("group\t")]
    summary = dict(areas=len(names), tests=total_tests, tests_known_areas=len(known),
                   dx_areas=sum(1 for n in names if n.startswith("dx_")),
                   categories={k: dict(areas=v[0], tests=v[1]) for k, v in cats.items()},
                   syscalls=dict(total=tot, direct=direct, uncovered=unc,
                                 thin=len(cov["thin"]), witnessed="not measured"),
                   win10=modes)
    if "--json" in sys.argv:
        print(json.dumps(summary, indent=1)); return 0
    L = ["# Corpus statistics", "",
         "Generated by `tools/stats.py` from the tests, a fresh build, `tools/coverage.py` and the sanitized Windows 10",
         "summaries under `results/win10/`. Windows 10 means 22H2 x64 (build 19045), one interactive session, no network.", "",
         "## Size", "",
         f"- Areas: {len(names)} ({len(names) - summary['dx_areas']} run on Windows 10, {summary['dx_areas']} `dx_` areas that change system state and are not run there)",
         f"- Tests (`TEST()` registrations plus generated ones, counted from the built exes): {total_tests}", "",
         "## Tests by category (area name prefix)", "",
         "| Prefix | Areas | Tests | What |", "|---|---|---|---|"]
    for k, v in sorted(cats.items(), key=lambda kv: (-kv[1][1], kv[0])):
        L.append(f"| {k}_ | {v[0]} | {v[1]} | {CATEGORY.get(k, '')} |")
    L += ["", "## Syscall coverage", "",
          f"Win32u service table: {tot} syscalls. Levels of evidence are explained in `COVERAGE.md`.", "",
          "| Level | Syscalls | Share |", "|---|---|---|",
          f"| Direct (a test calls the `Nt*` stub) | {direct} | {direct * 100 / tot:.1f}% |",
          f"| Not called directly | {unc} | {unc * 100 / tot:.1f}% |",
          "| Witnessed kernel entry | not measured | |", "",
          f"{len(cov['thin'])} of the direct syscalls are thin (one call site or at most three checks).", "",
          "## Windows 10 results", "",
          "Pass = tests - failed - skipped. Skips are conditional and carry a reason (`docs/WIN10_NOTES.md`).", "",
          "| Mode | Areas | Tests | Pass | Fail | Skip | Checks passed |", "|---|---|---|---|---|---|---|"]
    for m, label in (("adm", "adm (elevated Administrator token)"), ("std", "std (unprivileged token)")):
        r = modes[m]
        L.append(f"| {label} | {r['areas']} | {r['tests']} | {r['passed']} | {r['failed']} | {r['skipped']} | {r['checks']} |")
    missing = [n for n in names if not n.startswith("dx_") and n not in w10["adm"]]
    if missing: L += ["", "Areas without a Windows 10 result: " + ", ".join(missing)]
    L += ["", "## Determinism", ""]
    if det:
        L += ["| Group | Runs compared | Areas | Tests | Differences |", "|---|---|---|---|---|"]
        for r in det: L.append("| " + " | ".join(r[:5]) + " |")
        L += [""]
    L += ["See `results/win10/DETERMINISM.md` for the method and what was fixed to get there.", "",
          "## Regenerate", "",
          "    make -j8",
          "    python3 tools/stats.py          # rewrites results/STATS.md and COVERAGE.md",
          "    python3 tools/stats.py --json   # the same numbers as JSON, nothing written", "",
          "Test counts need Wine only to list names (`build/<area>.exe --list`); `--no-wine` falls back to the Windows 10",
          "summaries. Windows 10 numbers are re-summarized from run logs with `tools/win10_summarize.py` (see `HANDOFF.md`).", ""]
    open(os.path.join(ROOT, "results", "STATS.md"), "w").write("\n".join(L))
    print(json.dumps(summary))
    return 0

if __name__ == "__main__":
    sys.exit(main())
