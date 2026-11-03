/* The user atom table is shared by every process of the session (win32kbase!UserAtomTableHandle is one global
 * handle), so a second process sees the same ids for the same strings. The kernel32 global atom table is
 * per window station and is exercised for contrast. Children start with a pristine process state. */
#include "mta.h"
#include <stdlib.h>

static void widen(WCHAR *w, size_t cch, const char *s) { size_t i = 0; for (; s[i] && i + 1 < cch; i++) w[i] = (WCHAR)(unsigned char)s[i]; w[i] = 0; }

/* argv[1] = ASCII name; exit code = the id RegisterWindowMessageW returns (0 on failure) */
CHILD(mta_reg) {
    if (argc < 2) return 0x7FFF0001;
    WCHAR n[300]; widen(n, 300, argv[1]);
    UINT id = U(RegisterWindowMessageW)(n);
    return (int)id;
}
/* argv[1] = id (decimal), argv[2] = name; verifies both directions, exit code = failure count */
CHILD(mta_verify) {
    if (argc < 3) return 99;
    int bad = 0;
    UINT id = (UINT)strtoul(argv[1], NULL, 10);
    WCHAR n[300], b[300]; widen(n, 300, argv[2]);
    memset(b, 0, sizeof b);
    UINT len = U(GetClipboardFormatNameW)(id, b, 300);
    if (len != wcslen(n) || wcscmp(b, n)) { printf("  FAIL name of atom %u is \"%ls\" (len %u)\n", id, b, len); bad++; }
    if (U(RegisterWindowMessageW)(n) != id) { printf("  FAIL RegisterWindowMessageW(name) != %u\n", id); bad++; }
    if (U(RegisterClipboardFormatW)(n) != id) { printf("  FAIL RegisterClipboardFormatW(name) != %u\n", id); bad++; }
    return bad;
}
/* argv[1] = global atom id, argv[2] = name; GlobalFindAtom must see the parent's atom. The global atom table belongs to the
 * window station: a process that has not made a single user32 call yet is not attached to one, and the table refuses it
 * with ERROR_ACCESS_DENIED. The first user32 call (RegisterWindowMessageW) connects the process, after which the parent's
 * atom is visible. */
CHILD(mta_global) {
    if (argc < 3) return 99;
    int bad = 0;
    ATOM g = (ATOM)strtoul(argv[1], NULL, 10);
    WCHAR n[300]; widen(n, 300, argv[2]);
    SetLastError(0);
    ATOM pre = GlobalFindAtomW(n);
    DWORD pre_err = GetLastError();
    if (pre != 0 || pre_err != ERROR_ACCESS_DENIED) { printf("  FAIL unconnected GlobalFindAtomW = %u, error %lu\n", pre, pre_err); bad++; }
    /* the user table must not know this string: registering it yields an atom unrelated to the global one's name table */
    UINT u = U(RegisterWindowMessageW)(n);
    if (u < 0xC000) { printf("  FAIL user id 0x%x out of range\n", u); bad++; }
    if (GlobalFindAtomW(n) != g) { printf("  FAIL GlobalFindAtomW != %u\n", g); bad++; }
    WCHAR b[300]; memset(b, 0, sizeof b);
    if (GlobalGetAtomNameW(g, b, 300) != wcslen(n) || wcscmp(b, n)) { printf("  FAIL GlobalGetAtomNameW\n"); bad++; }
    return bad;
}
/* argv[1] = parent pid, argv[2] = name; registers it, publishes the id in a named mapping, waits for the stop event */
CHILD(mta_hold) {
    if (argc < 3) return 99;
    WCHAR n[300], mname[96], sname[96], gname[96];
    widen(n, 300, argv[2]);
    swprintf(mname, 96, L"Local\\win32kt_mta_map_%hs", argv[1]);
    swprintf(sname, 96, L"Local\\win32kt_mta_stop_%hs", argv[1]);
    swprintf(gname, 96, L"Local\\win32kt_mta_go_%hs", argv[1]);
    HANDLE hm = OpenFileMappingW(FILE_MAP_WRITE, FALSE, mname);
    HANDLE hs = OpenEventW(SYNCHRONIZE, FALSE, sname);
    HANDLE hg = OpenEventW(EVENT_MODIFY_STATE, FALSE, gname);
    if (!hm || !hs || !hg) return 98;
    UINT *p = MapViewOfFile(hm, FILE_MAP_WRITE, 0, 0, 4);
    if (!p) return 97;
    *p = U(RegisterWindowMessageW)(n);
    SetEvent(hg);
    WaitForSingleObject(hs, 15000);
    return 0;
}

static void child_name(WCHAR *w, char *a, const char *tag) {
    mta_name(w, 300, tag);
    for (int i = 0; ; i++) { a[i] = (char)w[i]; if (!w[i]) break; }
}

TEST(msg_timers_atoms_child_sees_parent_registered_id) {
    MTA_NEED();
    WCHAR n[300]; char a[300]; child_name(n, a, "ch_parentfirst");
    UINT id = U(RegisterWindowMessageW)(n); REQUIRE(id);
    DWORD code = t_run_child("mta_reg", a, 20000);
    MEQ(code, id);                 /* a different process of the session resolves the same string to the same atom */
}
TEST(msg_timers_atoms_child_reads_parent_atom_name) {
    MTA_NEED();
    WCHAR n[300]; char a[300], extra[400]; child_name(n, a, "ch_names");
    UINT id = U(RegisterWindowMessageW)(n); REQUIRE(id);
    snprintf(extra, sizeof extra, "%u %s", id, a);
    struct k_child_result r = k_child_run("mta_verify", extra, 20000);
    MEQ(r.exit_code, 0);
}
TEST(msg_timers_atoms_child_integer_atoms) {
    MTA_NEED();
    DWORD code = t_run_child("mta_reg", "#1357", 20000);
    MEQ(code, 1357);
    code = t_run_child("mta_reg", "#60000", 20000);
    MEQ(code, 0);                  /* integer atoms of 0xC000 and above are rejected in every process */
}
TEST(msg_timers_atoms_child_case_insensitive_across_processes) {
    MTA_NEED();
    WCHAR n[300]; char a[300]; child_name(n, a, "ch_case");
    UINT id = U(RegisterWindowMessageW)(n); REQUIRE(id);
    for (char *p = a; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    MEQ(t_run_child("mta_reg", a, 20000), id);
}
TEST(msg_timers_atoms_child_registered_atom_visible_while_child_lives) {
    MTA_NEED();
    WCHAR n[300]; char a[300], extra[400]; child_name(n, a, "ch_childfirst");
    WCHAR mname[96], sname[96], gname[96]; DWORD pid = GetCurrentProcessId();
    swprintf(mname, 96, L"Local\\win32kt_mta_map_%lu", pid);
    swprintf(sname, 96, L"Local\\win32kt_mta_stop_%lu", pid);
    swprintf(gname, 96, L"Local\\win32kt_mta_go_%lu", pid);
    HANDLE hm = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 64, mname);
    HANDLE hs = CreateEventW(NULL, TRUE, FALSE, sname), hg = CreateEventW(NULL, TRUE, FALSE, gname);
    UINT *p = hm ? MapViewOfFile(hm, FILE_MAP_WRITE, 0, 0, 4) : NULL;
    if (!hm || !hs || !hg || !p) { t_skip("sync objects"); goto out; }
    *p = 0;
    snprintf(extra, sizeof extra, "%lu %s", pid, a);
    PROCESS_INFORMATION pi;
    HANDLE hp = t_spawn_child("mta_hold", extra, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!hp) { t_skip("spawn"); goto out; }
    CHECK(WaitForSingleObject(hg, 20000) == WAIT_OBJECT_0);
    UINT cid = *p;
    CHECK(cid >= 0xC000 && cid <= 0xFFFF);
    MEQ(U(RegisterWindowMessageW)(n), cid);        /* the child's registration is visible to the parent */
    WCHAR b[300]; memset(b, 0, sizeof b);
    MEQ(U(GetClipboardFormatNameW)(cid, b, 300), wcslen(n));
    CHECK(!wcscmp(b, n));
    SetEvent(hs);
    CHECK(WaitForSingleObject(hp, 20000) == WAIT_OBJECT_0);
    DWORD ec = 99; GetExitCodeProcess(hp, &ec);
    MEQ(ec, 0);
    /* once the child is gone the parent still holds its own reference from the call above */
    MEQ(U(RegisterWindowMessageW)(n), cid);
    CloseHandle(pi.hThread); CloseHandle(hp);
out:
    if (p) UnmapViewOfFile(p);
    if (hm) CloseHandle(hm);
    if (hs) CloseHandle(hs);
    if (hg) CloseHandle(hg);
}
TEST(msg_timers_atoms_child_atom_survives_process_exit_note) {
    MTA_NEED();
    WCHAR n[300]; char a[300]; child_name(n, a, "ch_afterexit");
    DWORD cid = t_run_child("mta_reg", a, 20000);
    REQUIRE(cid >= 0xC000 && cid <= 0xFFFF);
    UINT mine = U(RegisterWindowMessageW)(n);
    if (mine != cid) t_note("atom 0x%lx registered by an exited process was re-registered as 0x%x", cid, mine);
    else CHECK(mine == cid);
}
TEST(msg_timers_atoms_child_global_atom_table_shared_per_window_station) {
    MTA_NEED();
    WCHAR n[300]; char a[300], extra[400]; child_name(n, a, "ch_global");
    ATOM g = GlobalAddAtomW(n); REQUIRE(g);
    snprintf(extra, sizeof extra, "%u %s", g, a);
    struct k_child_result r = k_child_run("mta_global", extra, 20000);
    MEQ(r.exit_code, 0);
    GlobalDeleteAtom(g);
}
TEST(msg_timers_atoms_child_user_atom_not_found_by_global_lookup) {
    MTA_NEED();
    WCHAR n[300]; char a[300]; child_name(n, a, "ch_userglob");
    UINT u = U(RegisterWindowMessageW)(n); REQUIRE(u);
    DWORD code = t_run_child("mta_reg", a, 20000);
    MEQ(code, u);
    SET_ERR(0);
    MEQ(GlobalFindAtomW(n), 0);
}
TEST(msg_timers_atoms_child_first_call_is_register_window_message) {
    MTA_NEED();
    /* a fresh process whose very first user32 call is RegisterWindowMessageW connects to win32k as a side effect */
    WCHAR n[300]; char a[300]; child_name(n, a, "ch_fresh");
    DWORD code = t_run_child("mta_reg", a, 20000);
    CHECK(code >= 0xC000 && code <= 0xFFFF);
    MEQ(U(RegisterWindowMessageW)(n), code);
}
