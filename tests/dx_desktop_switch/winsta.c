/* dx_desktop_switch: window station creation rules and window-station isolation.
 *
 * SetProcessWindowStation is process-wide state, so every test that calls it runs in a CHILD
 * (exit code = failure count, 77 = prerequisite missing => parent t_skip).
 *
 * Analysis anchors:
 *  - user32!CommonCreateWindowStation: (dwFlags & 0xFFFFFFFC) != 0 -> ERROR_INVALID_PARAMETER before any
 *    syscall; CWF_CREATE_ONLY (bit 0) clears OBJ_OPENIF (0x80) in the OBJECT_ATTRIBUTES it hands to
 *    NtUserCreateWindowStation (the flags are derived from the caller flags), so a second create of an
 *    existing name collides (STATUS_OBJECT_NAME_COLLISION -> 183); an empty name makes the kernel
 *    (EditionCreateWindowStationEntryPoint) synthesize "...\Service-0x<luid hi>-<luid lo>$".
 *  - CreateWindowStation: once the window-station list is non-empty the new station is attached to
 *    the NOIO terminal, i.e. it is a non-interactive station.
 *  - NtUserSwitchDesktop: station flag 4 -> LastError 5. EditionOpenInputDesktopEntryPoint: station flag
 *    4 -> LastError 1 (ERROR_INVALID_FUNCTION).
 */
#include "dxd.h"

#define SKIPPED 77

#ifndef WINSTA_ALL_ACCESS
#define WINSTA_ALL_ACCESS 0x37F
#endif

/* ---- entering / leaving a private window station (child only) -------------------------------- */
struct ws_ctx {
    HWINSTA orig, w; HDESK origd, d;
    WCHAR wname[64], dname[64], orig_wname[64];
    int in, moved;
};
static int ws_enter(struct ws_ctx *c, const char *tag, int make_desktop) {
    memset(c, 0, sizeof *c);
    c->orig = U(GetProcessWindowStation)();
    c->origd = U(GetThreadDesktop)(GetCurrentThreadId());
    if (!c->orig || !c->origd) return 0;
    dxd_uoi_str(c->orig, DXD_UOI_NAME, c->orig_wname, 64);
    dxd_name(c->wname, tag);
    c->w = U(CreateWindowStationW)(c->wname, 0, WINSTA_ALL_ACCESS, NULL);
    if (!c->w) { printf("  NOTE CreateWindowStation failed, err %lu\n", GetLastError()); return 0; }
    if (!U(SetProcessWindowStation)(c->w)) { printf("  NOTE SetProcessWindowStation failed, err %lu\n", GetLastError()); U(CloseWindowStation)(c->w); c->w = NULL; return 0; }
    c->in = 1;
    if (make_desktop) {
        lstrcpyW(c->dname, L"dxd");
        c->d = U(CreateDesktopW)(c->dname, NULL, NULL, 0, DESKTOP_ALL_RIGHTS, NULL);
        if (!c->d) { printf("  NOTE CreateDesktop in new station failed, err %lu\n", GetLastError()); return 0; }
    }
    return 1;
}
static void ws_leave(struct ws_ctx *c) {
    if (c->in) {
        CK(U(SetProcessWindowStation)(c->orig));       /* station first, then the thread's desktop inside it */
        c->in = 0;
    }
    if (c->moved) { CK(U(SetThreadDesktop)(c->origd)); c->moved = 0; }
    if (c->d) { U(CloseDesktop)(c->d); c->d = NULL; }
    if (c->w) { U(CloseWindowStation)(c->w); c->w = NULL; }
}
#define CHILD_PREREQ() do { if (!k_has_interactive_desktop() || !U(CreateWindowStationW) || !U(SetProcessWindowStation)) { printf("  SKIP no interactive session\n"); return SKIPPED; } } while (0)

static void run_child(const char *name, DWORD to) {
    if (t_is_wine() && !strcmp(name, "dxd_ws_noio")) { t_skip("Wine does not implement the NOIO station switch refusal"); return; }
    struct k_child_result r = k_child_run(name, "", to);
    if (r.exit_code == SKIPPED) { t_skip("child prerequisite missing"); return; }
    CHECKF(r.exit_code == 0, "child %s exit code %lu", name, r.exit_code);
}

/* ================================ creation rules (no process-wide change) ======================== */

DXT(winsta_create_invalid_flag_bits_fail_87) {
    REQUIRE(U(CreateWindowStationW));
    if (t_is_wine()) { t_skip("Wine does not validate CreateWindowStation flag bits"); return; }
    static const DWORD bad[] = { 0x4, 0x8, 0x10, 0x100, 0x10000, 0x80000000, 0xFFFFFFFC, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        WCHAR n[64]; swprintf(n, 64, L"win32kt%lu_bf%zu", GetCurrentProcessId(), i);
        SetLastError(0);
        HWINSTA w = U(CreateWindowStationW)(n, bad[i], WINSTA_ALL_ACCESS, NULL);
        DWORD e = GetLastError();
        CHECKF(w == NULL, "flags 0x%lx created a station", bad[i]);
        if (!w) CHECKF(e == ERROR_INVALID_PARAMETER, "flags 0x%lx err %lu", bad[i], e);
        else U(CloseWindowStation)(w);
    }
}

DXT(winsta_create_only_collides_on_existing_name) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR n[64]; dxd_name(n, "co");
    HWINSTA a = U(CreateWindowStationW)(n, CWF_CREATE_ONLY, WINSTA_ALL_ACCESS, NULL);
    if (!a) { t_skip("CreateWindowStation failed, err %lu", GetLastError()); return; }
    SetLastError(0);
    HWINSTA b = U(CreateWindowStationW)(n, CWF_CREATE_ONLY, WINSTA_ALL_ACCESS, NULL);
    DWORD e = GetLastError();
    CHECK(b == NULL);
    if (!b) CHECKF(e == ERROR_ALREADY_EXISTS, "err %lu", e);     /* OBJ_OPENIF cleared => STATUS_OBJECT_NAME_COLLISION */
    else U(CloseWindowStation)(b);
    U(CloseWindowStation)(a);
}

DXT(winsta_create_without_create_only_opens_existing) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR n[64], n1[128], n2[128]; dxd_name(n, "oi");
    HWINSTA a = U(CreateWindowStationW)(n, 0, WINSTA_ALL_ACCESS, NULL);
    if (!a) { t_skip("CreateWindowStation failed, err %lu", GetLastError()); return; }
    HWINSTA b = U(CreateWindowStationW)(n, 0, WINSTA_ALL_ACCESS, NULL);      /* OBJ_OPENIF kept => opens the same station */
    CHECK(b != NULL);
    if (b) {
        CHECK(b != a);
        CHECK(dxd_uoi_str(a, DXD_UOI_NAME, n1, 128)); CHECK(dxd_uoi_str(b, DXD_UOI_NAME, n2, 128));
        EXPECT_WSTR(n1, n2);
        CHECK(dxd_same_object(a, b));
        U(CloseWindowStation)(b);
    }
    U(CloseWindowStation)(a);
}

DXT(winsta_new_station_is_not_visible) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR n[64]; dxd_name(n, "nv");
    HWINSTA w = U(CreateWindowStationW)(n, 0, WINSTA_ALL_ACCESS, NULL);
    if (!w) { t_skip("CreateWindowStation failed, err %lu", GetLastError()); return; }
    USEROBJECTFLAGS f; DWORD need = 0; memset(&f, 0, sizeof f);
    CHECK(U(GetUserObjectInformationW)(w, DXD_UOI_FLAGS, &f, sizeof f, &need));
    CHECKF(!(f.dwFlags & WSF_VISIBLE), "flags 0x%lx", f.dwFlags);     /* only the first (interactive) station carries WSF_VISIBLE */
    WCHAR nm[64], ty[32];
    CHECK(dxd_uoi_str(w, DXD_UOI_NAME, nm, 64)); EXPECT_WSTR(nm, n);
    CHECK(dxd_uoi_str(w, DXD_UOI_TYPE, ty, 32)); EXPECT_WSTR(ty, L"WindowStation");
    CHECK(dxd_obj_type(w, ty, 32)); EXPECT_WSTR(ty, L"WindowStation");
    U(CloseWindowStation)(w);
}

DXT(winsta_null_name_gets_service_style_name) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    SetLastError(0);
    HWINSTA w = U(CreateWindowStationW)(NULL, 0, WINSTA_ALL_ACCESS, NULL);
    if (!w) { t_note("CreateWindowStation(NULL name) failed, err %lu", GetLastError()); return; }
    WCHAR nm[128];
    CHECK(dxd_uoi_str(w, DXD_UOI_NAME, nm, 128));
    /* EditionCreateWindowStationEntryPoint formats "%ws\Service-0x%x-%x$" when no name and no root are given */
    CHECKF(!wcsncmp(nm, L"Service-0x", 10) && nm[wcslen(nm) - 1] == L'$', "name \"%ls\"", nm);
    U(CloseWindowStation)(w);
}

DXT(winsta_create_same_name_twice_with_distinct_flags_note) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR n[64]; dxd_name(n, "fl2");
    HWINSTA a = U(CreateWindowStationW)(n, 2, WINSTA_ALL_ACCESS, NULL);      /* bit 1 passes the 0xFFFFFFFC test */
    if (!a) { t_note("CreateWindowStation(flags 2) failed, err %lu", GetLastError()); return; }
    CHECK(a != NULL);
    U(CloseWindowStation)(a);
}

DXT(winsta_open_existing_by_name_and_type) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR n[64]; dxd_name(n, "op");
    HWINSTA a = U(CreateWindowStationW)(n, 0, WINSTA_ALL_ACCESS, NULL);
    if (!a) { t_skip("CreateWindowStation failed"); return; }
    HWINSTA b = U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES);
    CHECK(b != NULL);
    if (b) {
        ULONG g = 0;
        if (dxd_granted(b, &g)) CHECKF(g & WINSTA_READATTRIBUTES, "granted 0x%lx", g);
        CHECK(dxd_same_object(a, b));
        U(CloseWindowStation)(b);
    }
    U(CloseWindowStation)(a);
}

DXT(winsta_close_invalid_handle_fails) {
    REQUIRE(U(CloseWindowStation));
    SetLastError(0);
    CHECK(!U(CloseWindowStation)(NULL));
    CHECK(GetLastError() != 0);
    SetLastError(0);
    CHECK(!U(CloseWindowStation)((HWINSTA)(ULONG_PTR)0x12344));
    CHECK(GetLastError() != 0);
}

/* ================================ children: process-wide window-station changes ================= */

CHILD(dxd_ws_noio) {
    dxd_cf = 0;
    CHILD_PREREQ();
    struct ws_ctx c;
    if (!ws_enter(&c, "noio", 1)) { ws_leave(&c); return SKIPPED; }
    WCHAR cur[64];
    CK(dxd_winsta_name(cur, 64)); CK(!lstrcmpW(cur, c.wname));          /* process station is the new one */
    CK(c.d != NULL);
    /* NtUserSwitchDesktop: the desktop's window station has the NOIO flag -> LastError 5 */
    SetLastError(0);
    CK(!U(SwitchDesktop)(c.d));
    DWORD e = GetLastError();
    CKF(e == ERROR_ACCESS_DENIED, "SwitchDesktop on NOIO station err %lu", e);
    /* EditionOpenInputDesktopEntryPoint: NOIO process station -> ERROR_INVALID_FUNCTION */
    SetLastError(0);
    HDESK in = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    e = GetLastError();
    CKF(in == NULL, "OpenInputDesktop from a NOIO station returned %p", in);
    if (!in) CKF(e == ERROR_INVALID_FUNCTION, "OpenInputDesktop err %lu", e);
    else U(CloseDesktop)(in);
    USEROBJECTFLAGS f; DWORD need = 0; memset(&f, 0, sizeof f);
    CK(U(GetUserObjectInformationW)(c.w, DXD_UOI_FLAGS, &f, sizeof f, &need));
    CK(!(f.dwFlags & WSF_VISIBLE));
    ws_leave(&c);
    /* back on the interactive station the input desktop is reachable again and was not disturbed */
    WCHAR inname[128];
    CK(dxd_winsta_name(cur, 64)); CK(!lstrcmpW(cur, c.orig_wname));
    CK(dxd_input_name(inname, 128));
    return dxd_cf;
}
DXT(winsta_noio_station_refuses_switch_and_input_open) { run_child("dxd_ws_noio", 40000); }

CHILD(dxd_ws_invalid_set) {
    dxd_cf = 0;
    CHILD_PREREQ();
    WCHAR before[64], after[64];
    CK(dxd_winsta_name(before, 64));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    HDESK td = U(GetThreadDesktop)(GetCurrentThreadId());
    struct { HANDLE h; const char *what; int exact6; } v[] = {
        { NULL, "NULL", 1 }, { (HANDLE)(ULONG_PTR)0x12344, "bogus", 1 }, { ev, "event", 0 }, { td, "desktop handle", 0 },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        BOOL rc = U(SetProcessWindowStation)((HWINSTA)v[i].h);
        DWORD e = GetLastError();
        CKF(!rc, "SetProcessWindowStation(%s) succeeded", v[i].what);
        if (!rc && v[i].exact6) CKF(e == ERROR_INVALID_HANDLE, "%s err %lu", v[i].what, e);
        else if (!rc) printf("  NOTE SetProcessWindowStation(%s) err %lu\n", v[i].what, e);
        CK(dxd_winsta_name(after, 64)); CK(!lstrcmpW(before, after));
    }
    if (ev) CloseHandle(ev);
    return dxd_cf;
}
DXT(winsta_set_process_invalid_handles_fail_and_keep_station) { run_child("dxd_ws_invalid_set", 30000); }

/* Atom tables: win32kbase!UserAddAtom* all go through the single session-wide UserAtomTableHandle
 * (RtlAddAtomToAtomTable), not through a per-tagWINDOWSTATION table: strings registered with
 * RegisterClipboardFormat / RegisterWindowMessage keep their ids when the process moves to another window
 * station of the same session, and a private-station registration is visible by id from the interactive one. */
CHILD(dxd_ws_atoms) {
    dxd_cf = 0;
    CHILD_PREREQ();
    WCHAR na[96], nb[96], nw[96];
    swprintf(na, 96, L"win32kt%lu_atom_a", GetCurrentProcessId());
    swprintf(nb, 96, L"win32kt%lu_atom_b", GetCurrentProcessId());
    swprintf(nw, 96, L"win32kt%lu_atom_only_in_new", GetCurrentProcessId());
    UINT a1 = U(RegisterClipboardFormatW)(na), a1b = U(RegisterClipboardFormatW)(na), b1 = U(RegisterClipboardFormatW)(nb);
    CK(a1 >= 0xC000 && a1 <= 0xFFFF); CK(b1 >= 0xC000 && b1 <= 0xFFFF);
    CK(a1 == a1b); CK(a1 != b1);
    UINT m1 = U(RegisterWindowMessageW)(na);
    printf("  NOTE RegisterWindowMessage(a)=0x%x RegisterClipboardFormat(a)=0x%x\n", m1, a1);
    struct ws_ctx c;
    if (!ws_enter(&c, "atoms", 1)) { ws_leave(&c); return SKIPPED; }
    CK(U(SetThreadDesktop)(c.d)); c.moved = 1;
    UINT a2 = U(RegisterClipboardFormatW)(na), w2 = U(RegisterClipboardFormatW)(nw), w2b = U(RegisterClipboardFormatW)(nw);
    CKF(a2 == a1, "id of a differs between stations: 0x%x vs 0x%x", a1, a2);
    CK(w2 >= 0xC000 && w2 <= 0xFFFF); CK(w2 == w2b);
    WCHAR got[128]; got[0] = 0;
    int r = U(GetClipboardFormatNameW)(b1, got, 128);
    CKF(r > 0 && !lstrcmpW(got, nb), "id 0x%x resolves to \"%ls\" in the new station", b1, got);
    ws_leave(&c);
    UINT a3 = U(RegisterClipboardFormatW)(na);
    CKF(a3 == a1, "id of a changed 0x%x -> 0x%x after the round trip", a1, a3);
    got[0] = 0;
    r = U(GetClipboardFormatNameW)(w2, got, 128);
    CKF(r > 0 && !lstrcmpW(got, nw), "interactive station does not resolve id 0x%x to the string registered in the other station", w2);
    return dxd_cf;
}
DXT(winsta_registered_atoms_are_session_wide) { run_child("dxd_ws_atoms", 40000); }

CHILD(dxd_ws_clipboard) {
    dxd_cf = 0;
    CHILD_PREREQ();
    UINT seq0 = U(GetClipboardSequenceNumber)();
    struct ws_ctx c;
    if (!ws_enter(&c, "clip", 1)) { ws_leave(&c); return SKIPPED; }
    CK(U(SetThreadDesktop)(c.d)); c.moved = 1;
    DWORD seqw0 = U(GetClipboardSequenceNumber)();
    /* a brand-new station starts with an empty clipboard */
    CK(!U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    CK(U(CountClipboardFormats)() == 0);
    int wrote = 0;
    if (U(OpenClipboard)(NULL)) {
        U(EmptyClipboard)();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 16);
        if (g) {
            WCHAR *p = (WCHAR *)GlobalLock(g); lstrcpyW(p, L"dxd"); GlobalUnlock(g);
            if (U(SetClipboardData)(CF_UNICODETEXT, g)) wrote = 1; else GlobalFree(g);
        }
        U(CloseClipboard)();
    } else printf("  NOTE OpenClipboard in the new station failed, err %lu\n", GetLastError());
    if (wrote) {
        CK(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
        DWORD seqw1 = U(GetClipboardSequenceNumber)();
        CKF(seqw1 != seqw0, "sequence did not advance (%lu)", seqw1);
    }
    ws_leave(&c);
    UINT seq1 = U(GetClipboardSequenceNumber)();
    /* the interactive station's clipboard never saw our data (sequence number is per window station) */
    if (seq1 != seq0) printf("  NOTE interactive clipboard sequence %u -> %u (another process may have copied)\n", seq0, seq1);
    else CK(seq1 == seq0);
    return dxd_cf;
}
DXT(winsta_clipboard_is_per_station) { run_child("dxd_ws_clipboard", 40000); }

/* broadcast must stay inside the sender's desktop; a window on another station never sees it */
static void bcast_action(struct dxd_thr *t) {
    DWORD_PTR res = 0;
    t->r[0] = (LONG_PTR)U(SendMessageTimeoutW)(HWND_BROADCAST, DXD_WM_COUNT, 0, 0, SMTO_ABORTIFHUNG, 1000, &res);
    t->r[1] = (LONG_PTR)res;
}
CHILD(dxd_ws_broadcast) {
    dxd_cf = 0;
    CHILD_PREREQ();
    struct dxd_thr a; memset(&a, 0, sizeof a);
    a.want_window = 1;
    if (!dxd_thr_start(&a, "bca") || !a.hw) { dxd_thr_stop(&a); return SKIPPED; }
    struct ws_ctx c;
    if (!ws_enter(&c, "bc", 1)) { dxd_thr_stop(&a); ws_leave(&c); return SKIPPED; }
    struct dxd_thr b; memset(&b, 0, sizeof b);
    b.desk = c.d; b.want_window = 1; b.action = bcast_action;
    int ok = dxd_thr_start(&b, "bcb");
    CKF(ok && b.set_ok && b.hw, "worker on the new station's desktop (set_ok=%d err=%lu)", b.set_ok, b.set_err);
    if (ok && b.hw) {
        CKF(a.hits == 0, "window on the other station received the broadcast (%ld)", a.hits);
        CKF(b.hits == 1, "sender-side window got %ld broadcast messages", b.hits);
        /* positive control: a broadcast from the interactive-station thread reaches A but not B */
        DWORD_PTR res = 0;
        U(SendMessageTimeoutW)(HWND_BROADCAST, DXD_WM_COUNT, 0, 0, SMTO_ABORTIFHUNG, 1000, &res);
        Sleep(50);
        CKF(a.hits == 1, "control broadcast reached %ld windows on the sender's own desktop", a.hits);
        CKF(b.hits == 1, "control broadcast leaked into the new station (%ld)", b.hits);
    }
    dxd_thr_stop(&b);
    ws_leave(&c);
    dxd_thr_stop(&a);
    return dxd_cf;
}
DXT(winsta_broadcast_does_not_cross_stations) { run_child("dxd_ws_broadcast", 40000); }

CHILD(dxd_ws_close_current) {
    dxd_cf = 0;
    CHILD_PREREQ();
    struct ws_ctx c;
    if (!ws_enter(&c, "cc", 0)) { ws_leave(&c); return SKIPPED; }
    SetLastError(0);
    BOOL rc = U(CloseWindowStation)(c.w);
    printf("  NOTE CloseWindowStation(process station) -> %d err %lu\n", rc, GetLastError());
    if (rc) c.w = NULL;                                  /* handle gone: nothing for ws_leave to close */
    CK(U(SetProcessWindowStation)(c.orig));
    c.in = 0;
    ws_leave(&c);
    WCHAR cur[64];
    CK(dxd_winsta_name(cur, 64)); CK(!lstrcmpW(cur, c.orig_wname));
    return dxd_cf;
}
DXT(winsta_close_process_station_then_restore) { run_child("dxd_ws_close_current", 30000); }
