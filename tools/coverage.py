#!/usr/bin/env python3
"""Syscall coverage for the win32k kernel-side corpus.

usage: coverage.py <corpus_dir>

Classification of every Nt* row of tools/win32u_syscalls.tsv:
  DIRECT  a test names the syscall in a real call context: U/UN/UIN(Name), a *_pfn variable,
          t_proc[_in]("Name"), dk_nt("Name"), NTU/W/sib_stub wrappers ... Occurrences that are
          only presence/layout evidence are ignored: comments, { "NtX", 4096 } number tables,
          STUB/STUBNUM/WINE_UNIMPL lines, blocks that use k_stub_*/stub-number helpers, the
          generated stub sweeps (sc_stub_layout, dcomp_stubs.c, d3dkmt_dd/tables.c),
          forbidden-name lists (dx_desktop_switch/guard.c) and the mitigation stub probe
          (win32k_mitigation/sc_syscalls.c).
  NONE    otherwise (direct-uncovered).
THIN: DIRECT syscalls with a single call site or <= 3 CHECK/EXPECT macros in all their sites,
listed fewest checks first.
Levels of evidence (see COVERAGE.md): direct (a test calls the syscall by name) and witnessed kernel entry
(the syscall was seen executing in a run; not measured by this script, see COVERAGE.md for why).
Writes COVERAGE.md. Static text scan: macros that build names at runtime are invisible.
"""
import re, sys, os, json, glob, collections

EXC = re.compile(r'sc_stub_layout/|dcomp_stubs\.c|d3dkmt_dd/tables\.c|'
                 r'win32k_mitigation/sc_syscalls\.c|dx_desktop_switch/guard\.c')
STUBBY = re.compile(r'k_stub_|STUBNUM|stub_check|stub_number|calls_stub|stub_layout')
IDENT = re.compile(r'\bNt[A-Za-z0-9_]+\b')
CHK = re.compile(r'\b(CHECK\w*|EXPECT\w*|EQ|NE|ASSERT\w*)\s*\(')
USERMODS = ('user32', 'gdi32', 'gdi32full', 'imm32', 'dwmapi', 'uxtheme', 'dcomp', 'd3d9', 'd3d11', 'dxgi', 'dciman32')

def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)

def famof(n):
    for p in ("NtUser", "NtGdi", "NtDComposition", "NtFlipObject", "NtDxgk", "NtModerncore",
              "NtCompositor", "NtDesktop", "NtToken"):
        if n.startswith(p): return p
    m = re.match(r'^(Nt[A-Z][a-z0-9]*)', n)
    return m.group(1) if m else "Nt?"

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    sysc = [l.split("\t")[1] for l in open(root + "/tools/win32u_syscalls.tsv").read().splitlines() if "\t" in l]
    sset = set(sysc)

    sites = collections.defaultdict(set)   # name -> {(file, block)}
    blocks_of = {}
    api = set()
    for f in glob.glob(root + "/tests/**/*.[ch]", recursive=True):
        rel = os.path.relpath(f, root)
        raw = open(f, errors="replace").read()
        for m in re.finditer(r'\bU(?:IN|N)?\s*\(\s*(?:"[^"]*"\s*,\s*)?([A-Za-z_]\w*)\s*\)', raw): api.add(m.group(1))
        for m in re.finditer(r'\bt_proc(?:_in)?\s*\(\s*(?:"[^"]*"\s*,\s*)?"([A-Za-z_]\w*)"', raw): api.add(m.group(1))
        if EXC.search(rel): continue
        blocks = re.split(r'\n\}', strip_comments(raw)); blocks_of[rel] = blocks
        for bi, b in enumerate(blocks):
            if STUBBY.search(b): continue
            for line in b.split("\n"):
                if re.search(r'\{\s*"Nt\w+"\s*,\s*-?\d+\s*\}', line): continue
                if 'UDECL' in line and not re.search(r'\(\s*\)\s*;', line): continue
                if re.search(r'\b(STUB|WINE_UNIMPL|STUBNUM|X)\s*\(', line) and not re.search(r'\bU[NI]*\s*\(', line): continue
                toks = set()
                for m in re.finditer(r'"(Nt[A-Za-z0-9_]+)"', line):
                    if (re.search(r'[A-Za-z_]\w*\s*\(\s*(?:"[^"]*"\s*,\s*|\w+\s*,\s*)?"%s"' % m.group(1), line)
                            and not re.search(r'(t_note|t_skip|printf|CHECKF|EQ)\s*\(', line)):
                        toks.add(m.group(1))
                ids = set(IDENT.findall(re.sub(r'"[^"]*"', '""', line))) | toks
                for n in ids:
                    base = n[:-4] if n.endswith("_pfn") else n
                    if base not in sset: continue
                    if re.match(r'\s*REQUIRE\s*\(\s*U', line) and line.count(base) == 1: continue
                    sites[base].add((rel, bi))

    stats, direct_unc = {}, []
    for n in sysc:
        if n in sites:
            stats[n] = (len(sites[n]), sum(len(CHK.findall(blocks_of[r][b])) for r, b in sites[n] if b < len(blocks_of[r])))
            continue
        direct_unc.append(n)
    direct_unc.sort()

    thin = []
    cand = [n for n, (s, c) in stats.items() if s <= 1 or c <= 3]
    for n in cand:
        thin.append((n, stats[n], 0))
    thin.sort(key=lambda x: (x[1][1], x[1][0], x[0]))

    tally = collections.defaultdict(lambda: [0, 0, 0, 0, 0])
    for n in sysc: tally[famof(n)][0] += 1
    for n in stats: tally[famof(n)][1] += 1
    for n in direct_unc: tally[famof(n)][3] += 1
    for n, _, _ in thin: tally[famof(n)][4] += 1
    tot = len(sysc)
    print(f"{tot} syscalls; direct={len(stats)} uncovered={len(direct_unc)} thin={len(thin)}")
    with open(root + "/COVERAGE.md", "w") as f:
        f.write(f"# Syscall coverage\n\n{tot} syscalls (win32u service table).\n\n"
                "Two levels of evidence, from strongest to weakest:\n\n"
                "1. **Direct**: a test names the `Nt*` stub (a `win32u.dll` export) in a real call context and invokes it.\n"
                "2. **Witnessed kernel entry**: the syscall was observed executing during a run on Windows 10. This level is **not\n"
                "   measured** in this tree (no run records which service numbers were entered), so no count is claimed for it. Caveat\n"
                "   when it is measured: most gdi32 drawing and attribute calls are batched in user mode in the GDI batch of the\n"
                "   thread (`GdiFlush` or a full batch sends them); a call through gdi32 can therefore return before any kernel entry\n"
                "   happens, so it counts as witnessed only after a `GdiFlush` (or a call that forces a flush) in the same test.\n\n"
                f"| Level | Syscalls | Share |\n|---|---|---|\n"
                f"| Direct | {len(stats)} | {len(stats)*100/tot:.1f}% |\n"
                f"| Not called directly | {len(direct_unc)} | {len(direct_unc)*100/tot:.1f}% |\n"
                f"| Witnessed | not measured | |\n\n"
                f"{len(thin)} of the direct ones are thin (one call site or at most three checks in all sites).\n"
                "Generated by `tools/coverage.py` (see its docstring for the counting rules).\n\n"
                "Per family:\n\n"
                "| Family | Total | Direct | Uncovered | Thin |\n|---|---|---|---|---|\n")
        for k, v in sorted(tally.items(), key=lambda kv: -kv[1][0]):
            f.write(f"| {k} | " + " | ".join(str(x) for i, x in enumerate(v) if i != 2) + " |\n")
        f.write("\n## Direct-uncovered\n\n")
        for n in direct_unc: f.write(f"- {n}\n")
        f.write("\n## Thin (call sites, checks)\n\n")
        for n, (s, c), sz in thin: f.write(f"- {n} sites={s} checks={c}\n")
    json.dump({"direct_uncovered": list(direct_unc), "thin": thin, "direct": len(stats), "total": tot,
               "tally": {k: v for k, v in tally.items()}}, open(os.environ.get("COV_JSON", "/dev/null"), "w"))
    return 0

if __name__ == "__main__":
    sys.exit(main())
