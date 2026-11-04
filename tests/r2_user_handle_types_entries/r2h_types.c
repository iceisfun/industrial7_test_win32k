/* Per-type entry shape for object types the first-round area does not create one by one:
 * every thread-level hook id, every cursor/icon creator, raw-input device infos, timers, plus
 * whole-table ownership classification derived from the kernel's type-flag table. */
#include "r2h.h"

UDECL(HHOOK, NtUserSetWindowsHookEx, (HINSTANCE, PUNICODE_STRING, DWORD, int, HOOKPROC, DWORD));
UDECL(UINT, NtUserGetRawInputDeviceList, (PVOID, PUINT, UINT));

static LRESULT CALLBACK nullhook(int c, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, c, w, l); }

/* allocation of a TYPE_HOOK object (flags 0x101: thread owned, pool object): owner = creating thread id,
 * no heap part. freed on unhook. */
static void hook_id(int id) {
    R2H_NEED();
    DWORD before = r2h_gr(GR_USEROBJECTS);
    HHOOK h = U(SetWindowsHookExW)(id, nullhook, NULL, GetCurrentThreadId());
    if (!h) { t_skip("thread-level hook id %d refused (err %lu)", id, GetLastError()); return; }
    r2h_entry_t e;
    CHECK(r2h_read_h(h, &e));
    EXPECT_EQ(e.type, UT_HOOK);
    EXPECT_EQ(e.uniq, R2H_UNIQ(h));
    EXPECT_EQ(e.owner, GetCurrentThreadId());
    EXPECT_EQ(e.delta, 0); EXPECT_EQ(e.f10, 0);
    CHECK((e.flags & 1) == 0);
    EXPECT_EQ(r2h_gr(GR_USEROBJECTS), before + 1);
    BOOL r = U(UnhookWindowsHookEx)(h);
    CHECK(r);
    EXPECT_EQ(r2h_gr(GR_USEROBJECTS), before);
    BOOL r2; R2H_CALL_ERR(r2, U(UnhookWindowsHookEx)(h));
    CHECK(!r2);
    EXPECT_ERR(0x57c);
}
#define HK(tag, id) R2(hook_##tag##_entry) { hook_id(id); }
HK(msgfilter, WH_MSGFILTER)
HK(keyboard, WH_KEYBOARD)
HK(getmessage, WH_GETMESSAGE)
HK(callwndproc, WH_CALLWNDPROC)
HK(cbt, WH_CBT)
HK(mouse, WH_MOUSE)
HK(debug, WH_DEBUG)
HK(shell, WH_SHELL)
HK(foregroundidle, WH_FOREGROUNDIDLE)
HK(callwndprocret, WH_CALLWNDPROCRET)

/* PtiFromThreadId fails for a thread that does not exist: NtUserSetWindowsHookEx -> 0x57, no entry. */
R2(hook_nonexistent_thread_rejected) {
    R2H_NEED(); REQUIRE(UN(NtUserSetWindowsHookEx));
    DWORD before = r2h_gr(GR_USEROBJECTS);
    HHOOK h; R2H_CALL_ERR(h, UN(NtUserSetWindowsHookEx)(NULL, NULL, 0xFFFFFFF0u, WH_GETMESSAGE, nullhook, 0));
    CHECK(h == NULL);
    EXPECT_ERR(0x57);
    EXPECT_EQ(r2h_gr(GR_USEROBJECTS), before);
}
R2(hook_two_hooks_get_distinct_entries) {
    R2H_NEED();
    HHOOK a = U(SetWindowsHookExW)(WH_GETMESSAGE, nullhook, NULL, GetCurrentThreadId());
    HHOOK b = U(SetWindowsHookExW)(WH_GETMESSAGE, nullhook, NULL, GetCurrentThreadId());
    if (!a || !b) { if (a) U(UnhookWindowsHookEx)(a); if (b) U(UnhookWindowsHookEx)(b); t_skip("hooks not installed"); return; }
    CHECK(a != b);
    CHECK(R2H_IDX(a) != R2H_IDX(b));
    r2h_entry_t ea, eb;
    CHECK(r2h_read_h(a, &ea)); CHECK(r2h_read_h(b, &eb));
    EXPECT_EQ(ea.type, UT_HOOK); EXPECT_EQ(eb.type, UT_HOOK);
    /* destroying one leaves the other untouched */
    CHECK(U(UnhookWindowsHookEx)(a));
    CHECK(r2h_read_h(b, &eb));
    EXPECT_EQ(eb.type, UT_HOOK); EXPECT_EQ(eb.uniq, R2H_UNIQ(b)); CHECK((eb.flags & 1) == 0);
    CHECK(U(UnhookWindowsHookEx)(b));
}
R2(syscall_validatehandlesecure_is_not_exported_by_win32u) {
    /* ValidateHandleSecure is a the kernel kernel export, not a service: win32u has no such stub. */
    CHECK(GetProcAddress(t_module("win32u.dll"), "NtUserValidateHandleSecure") == NULL);
    CHECK(GetProcAddress(t_module("win32u.dll"), "ValidateHandleSecure") == NULL);
}

/* ---- cursor / icon creators: TYPE_CURSOR (flags 0x20e): process owned pool object ---- */
static BYTE s_and[32], s_xor[32];
static HANDLE mk_createicon(void) { memset(s_and, 0xff, sizeof s_and); memset(s_xor, 0, sizeof s_xor); return U(CreateIcon)(GetModuleHandleW(NULL), 16, 16, 1, 1, s_and, s_xor); }
static HANDLE mk_createcursor(void) { memset(s_and, 0xff, sizeof s_and); memset(s_xor, 0, sizeof s_xor); return U(CreateCursor)(GetModuleHandleW(NULL), 3, 5, 16, 16, s_and, s_xor); }
static HANDLE mk_indirect(void) {
    HBITMAP m = U(CreateBitmap)(16, 16, 1, 1, NULL);
    if (!m) return NULL;
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE; ii.hbmMask = m; ii.hbmColor = NULL;   /* monochrome icon: mask holds AND over XOR (32 rows) */
    HBITMAP m2 = U(CreateBitmap)(16, 32, 1, 1, NULL);
    ii.hbmMask = m2 ? m2 : m;
    HANDLE h = U(CreateIconIndirect)(&ii);
    U(DeleteObject)(m); if (m2) U(DeleteObject)(m2);
    return h;
}
static HANDLE mk_copyicon(void) { HICON s = mk_createicon(); if (!s) return NULL; HICON c = U(CopyIcon)(s); U(DestroyIcon)(s); return c; }
static HANDLE mk_copyimage(void) { HICON s = mk_createicon(); if (!s) return NULL; HANDLE c = U(CopyImage)(s, IMAGE_ICON, 16, 16, 0); U(DestroyIcon)(s); return c; }
static HANDLE mk_copycursor(void) { HANDLE s = mk_createcursor(); if (!s) return NULL; HANDLE c = U(CopyImage)(s, IMAGE_CURSOR, 16, 16, 0); U(DestroyIcon)(s); return c; }
static void cursor_creator(HANDLE (*mk)(void), const char *name) {
    R2H_NEED();
    DWORD before = r2h_gr(GR_USEROBJECTS);
    HANDLE h = mk();
    if (!h) { t_skip("%s failed (err %lu)", name, GetLastError()); return; }
    r2h_entry_t e;
    CHECK(r2h_read_h(h, &e));
    EXPECT_EQ(e.type, UT_CURSOR);
    EXPECT_EQ(e.uniq, R2H_UNIQ(h));
    EXPECT_EQ(e.owner, GetCurrentProcessId());   /* flag bit1: PsGetProcessId */
    EXPECT_EQ(e.delta, 0); EXPECT_EQ(e.f10, 0);   /* pool object, no desktop-heap part */
    CHECK((e.flags & 1) == 0);
    EXPECT_EQ(r2h_gr(GR_USEROBJECTS), before + 1);
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    if (U(GetIconInfo)(h, &ii)) {
        if (ii.hbmMask) U(DeleteObject)(ii.hbmMask);
        if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
    } else t_note("%s: GetIconInfo failed (err %lu)", name, GetLastError());
    CHECK(U(DestroyIcon)(h));
    EXPECT_EQ(r2h_gr(GR_USEROBJECTS), before);
    r2h_entry_t e2;
    CHECK(r2h_read_h(h, &e2));
    CHECK(e2.type == 0 || e2.uniq != e.uniq || (e2.flags & 1));
}
#define CC(tag, fn) R2(cursor_##tag##_entry) { cursor_creator(fn, #tag); }
CC(createicon, mk_createicon)
CC(createcursor, mk_createcursor)
CC(createiconindirect, mk_indirect)
CC(copyicon, mk_copyicon)
CC(copyimage, mk_copyimage)
CC(copyimage_cursor, mk_copycursor)

/* CreateCursor keeps the hot spot; CreateIcon is flagged as an icon (GetIconInfo.fIcon). */
R2(cursor_hotspot_and_ficon_roundtrip) {
    R2H_NEED();
    HANDLE c = mk_createcursor(), i = mk_createicon();
    if (!c || !i) { if (c) U(DestroyIcon)(c); if (i) U(DestroyIcon)(i); t_skip("creators failed"); return; }
    ICONINFO ic, ii; memset(&ic, 0, sizeof ic); memset(&ii, 0, sizeof ii);
    CHECK(U(GetIconInfo)(c, &ic));
    CHECK(U(GetIconInfo)(i, &ii));
    CHECK(!ic.fIcon); EXPECT_EQ(ic.xHotspot, 3); EXPECT_EQ(ic.yHotspot, 5);
    CHECK(ii.fIcon);
    if (ic.hbmMask) { U(DeleteObject)(ic.hbmMask); } if (ic.hbmColor) { U(DeleteObject)(ic.hbmColor); }
    if (ii.hbmMask) { U(DeleteObject)(ii.hbmMask); } if (ii.hbmColor) { U(DeleteObject)(ii.hbmColor); }
    r2h_entry_t a, b;
    CHECK(r2h_read_h(c, &a)); CHECK(r2h_read_h(i, &b));
    EXPECT_EQ(a.type, UT_CURSOR); EXPECT_EQ(b.type, UT_CURSOR);   /* cursor and icon are the same object type */
    U(DestroyIcon)(c); U(DestroyIcon)(i);
}
/* A copy is a separate object: destroying the source leaves the copy alive. */
R2(cursor_copy_survives_source_destroy) {
    R2H_NEED();
    HICON s = mk_createicon(); REQUIRE(s);
    HICON c = U(CopyIcon)(s);
    if (!c) { U(DestroyIcon)(s); t_skip("CopyIcon failed"); return; }
    CHECK(c != s);
    CHECK(U(DestroyIcon)(s));
    r2h_entry_t e;
    CHECK(r2h_read_h(c, &e));
    EXPECT_EQ(e.type, UT_CURSOR); EXPECT_EQ(e.uniq, R2H_UNIQ(c)); CHECK((e.flags & 1) == 0);
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    CHECK(U(GetIconInfo)(c, &ii));
    if (ii.hbmMask) { U(DeleteObject)(ii.hbmMask); } if (ii.hbmColor) { U(DeleteObject)(ii.hbmColor); }
    CHECK(U(DestroyIcon)(c));
}

/* ---- raw input device infos: TYPE_DEVICEINFO (19), unowned pool objects ---- */
R2(deviceinfo_entries_for_rawinput_devices) {
    R2H_NEED();
    UINT n = 0;
    SetLastError(R2H_SENTINEL);
    UINT rc = U(GetRawInputDeviceList)(NULL, &n, sizeof(RAWINPUTDEVICELIST));
    CHECK(rc == 0);   /* NtUserGetRawInputDeviceList: list == NULL stores the count and succeeds */
    if (!n) { t_skip("no raw input devices"); return; }
    RAWINPUTDEVICELIST *l = calloc(n + 8, sizeof *l);
    REQUIRE(l);
    UINT cnt = n + 8;
    UINT got = U(GetRawInputDeviceList)(l, &cnt, sizeof *l);
    if (got == (UINT)-1) { free(l); t_skip("device list changed (err %lu)", GetLastError()); return; }
    for (UINT i = 0; i < got; i++) {
        r2h_entry_t e;
        HANDLE h = l[i].hDevice;
        CHECKF(r2h_read_h(h, &e), "device %u", i);
        EXPECT_EQ(e.type, UT_DEVICEINFO);
        EXPECT_EQ(e.uniq, R2H_UNIQ(h));
        EXPECT_EQ(e.owner, 0);   /* type flags 0: neither thread nor process owned */
        EXPECT_EQ(e.delta, 0); EXPECT_EQ(e.f10, 0);
        CHECK((e.flags & 1) == 0);
        CHECK(l[i].dwType <= RIM_TYPEHID);
    }
    free(l);
}
R2(deviceinfo_list_small_buffer_reports_count) {
    R2H_NEED(); REQUIRE(UN(NtUserGetRawInputDeviceList));
    UINT n = 0;
    CHECK(UN(NtUserGetRawInputDeviceList)(NULL, &n, 16) == 0);
    if (!n) { t_skip("no raw input devices"); return; }
    RAWINPUTDEVICELIST one[1]; memset(one, 0, sizeof one);
    UINT cnt = 0;
    UINT rc; R2H_CALL_ERR(rc, UN(NtUserGetRawInputDeviceList)(one, &cnt, 16)); /* *pnum < count: store count, ERROR_INSUFFICIENT_BUFFER */
    EXPECT_EQ(rc, 0xffffffffu);
    EXPECT_EQ(cnt, n);
    EXPECT_ERR(0x7a);
}
R2(deviceinfo_list_bad_cbsize_rejected) {
    R2H_NEED(); REQUIRE(UN(NtUserGetRawInputDeviceList));
    UINT n = 77;
    UINT rc; R2H_CALL_ERR(rc, UN(NtUserGetRawInputDeviceList)(NULL, &n, 8));  /* cbSize must be sizeof(RAWINPUTDEVICELIST) == 16 */
    EXPECT_EQ(rc, 0xffffffffu);
    EXPECT_ERR(0x57);
    EXPECT_EQ(n, 77);
}

/* ---- timers: TYPE_TIMER (flags 0x2), process owned, no handle exposed to the caller ---- */
static struct { HWND w; unsigned idx, uniq; int fired, rd_before, rd_during; r2h_entry_t before, during; } tm;
static VOID CALLBACK tm_cb(HWND w, UINT m, UINT_PTR id, DWORD t) {
    if (tm.fired) return;
    tm.fired = 1;
    tm.rd_before = r2h_read(tm.idx, &tm.before);
    U(KillTimer)(w, id);
    tm.rd_during = r2h_read(tm.idx, &tm.during);
}
R2(timer_killed_inside_callback_ends_up_free) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    unsigned nb; r2h_entry_t *bef = r2h_snap(&nb); REQUIRE(bef);
    UINT_PTR id = U(SetTimer)(w, 5, 20, tm_cb);
    if (!id) { free(bef); U(DestroyWindow)(w); t_skip("SetTimer failed"); return; }
    unsigned na; r2h_entry_t *aft = r2h_snap(&na); REQUIRE(aft);
    int found = 0;
    memset(&tm, 0, sizeof tm);
    for (unsigned i = 0; i < na && i < nb; i++) {
        if (aft[i].type != UT_TIMER || aft[i].owner != GetCurrentProcessId()) continue;
        if (bef[i].type == UT_TIMER && bef[i].uniq == aft[i].uniq) continue;
        found++; tm.idx = i; tm.uniq = aft[i].uniq;
    }
    free(bef); free(aft);
    EXPECT_EQ(found, 1);
    tm.w = w;
    MSG m; DWORD t0 = GetTickCount();
    while (!tm.fired && GetTickCount() - t0 < 3000) {
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
        if (!tm.fired) Sleep(10);
    }
    if (!tm.fired) { U(KillTimer)(w, id); U(DestroyWindow)(w); t_skip("timer did not fire in 3 s"); return; }
    if (found == 1) {
        CHECK(tm.rd_before); CHECK(tm.rd_during);
        EXPECT_EQ(tm.before.type, UT_TIMER);
        EXPECT_EQ(tm.before.uniq, tm.uniq);
        CHECK((tm.before.flags & 1) == 0);
        if (tm.during.type == UT_TIMER && tm.during.uniq == tm.uniq) { CHECK(tm.during.flags & 1); t_note("timer killed in its callback is kept as a destroyed entry"); }
        else t_note("timer entry released inside its callback");
        r2h_entry_t e; unsigned long t1 = GetTickCount();
        for (;;) {
            CHECK(r2h_read(tm.idx, &e));
            if (!(e.type == UT_TIMER && e.uniq == tm.uniq)) break;
            if (GetTickCount() - t1 > 2000) { CHECK(0); break; }
            Sleep(10);
        }
    }
    CHECK(!U(KillTimer)(w, id));
    U(DestroyWindow)(w);
}

/* ---- whole-table ownership classification ---- */
static int my_ids(DWORD *ids, int max) {
    int n = 0;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) { ids[n++] = GetCurrentThreadId(); return n; }
    THREADENTRY32 te; te.dwSize = sizeof te;
    if (Thread32First(s, &te)) do {
        if (te.th32OwnerProcessID == GetCurrentProcessId() && n < max) ids[n++] = te.th32ThreadID;
    } while (Thread32Next(s, &te));
    CloseHandle(s);
    return n;
}
/* Create a spread of types first so the scan has something of each ownership class to classify. */
R2(ownership_class_of_my_entries_matches_type_flags) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    HMENU m = U(CreateMenu)();
    HANDLE c = mk_createicon();
    HHOOK hk = U(SetWindowsHookExW)(WH_GETMESSAGE, nullhook, NULL, GetCurrentThreadId());
    HDWP dwp = U(BeginDeferWindowPos)(1);
    HANDLE acc = r2h_d_accel.create();
    UINT_PTR tid_timer = U(SetTimer)(w, 9, 60000, NULL);
    unsigned n; r2h_entry_t *t = r2h_snap(&n);
    DWORD ids[512]; int ni = my_ids(ids, 512);
    unsigned thread_seen = 0, proc_seen = 0, bad = 0;
    for (unsigned i = 0; t && i < n; i++) {
        if (!t[i].type || t[i].type > UT_MAX) continue;
        int mine_thread = 0;
        for (int k = 0; k < ni; k++) if (t[i].owner == ids[k]) { mine_thread = 1; break; }
        int mine_proc = t[i].owner == GetCurrentProcessId();
        if (mine_thread) { thread_seen++; if (!r2h_is_thread_type(t[i].type)) { bad++; t_note("entry %u type %u owned by my thread id but not a thread-owned type", i, t[i].type); } }
        if (mine_proc)   { proc_seen++;   if (!r2h_is_process_type(t[i].type)) { bad++; t_note("entry %u type %u owned by my pid but not a process-owned type", i, t[i].type); } }
    }
    CHECK(t != NULL);
    EXPECT_EQ(bad, 0);
    CHECK(thread_seen >= 3);  /* window, hook, hdwp at least */
    CHECK(proc_seen >= 3);    /* menu, cursor, accelerator (and the timer) at least */
    free(t);
    if (tid_timer) U(KillTimer)(w, tid_timer);
    if (acc) r2h_d_accel.destroy(acc);
    if (dwp) U(EndDeferWindowPos)(dwp);
    if (hk) U(UnhookWindowsHookEx)(hk);
    if (c) U(DestroyIcon)(c);
    if (m) U(DestroyMenu)(m);
    U(DestroyWindow)(w);
}
/* only pOwner for flag bits 0/1; types without them (clip data, monitor,
 * keyboard layout/file, device info, SPC) keep the zero the table was cleared with. */
R2(unowned_types_have_zero_owner) {
    R2H_NEED();
    unsigned n; r2h_entry_t *t = r2h_snap(&n); REQUIRE(t);
    unsigned seen = 0, bad = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!t[i].type || !r2h_is_unowned_type(t[i].type)) continue;
        seen++;
        if (t[i].owner) { bad++; t_note("entry %u type %u has owner %llu", i, t[i].type, t[i].owner); }
    }
    free(t);
    EXPECT_EQ(bad, 0);
    if (!seen) t_note("no unowned-type entries present");
}
/* Shared-heap objects (flag 0x40: monitors) have a heap offset but no desktop-heap guard word. */
R2(shared_heap_types_have_delta_without_guard) {
    R2H_NEED();
    unsigned n; r2h_entry_t *t = r2h_snap(&n); REQUIRE(t);
    unsigned seen = 0, bad = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!t[i].type || t[i].type > UT_MAX || !(r2h_typeflags[t[i].type] & R2H_TF_SHARED)) continue;
        seen++;
        if (!t[i].delta || t[i].f10) bad++;
    }
    free(t);
    EXPECT_EQ(bad, 0);
    if (!seen) t_note("no shared-heap entries present");
}

/* ---- free-list discipline ---- */
/* Non-window types take indices from the odd free list while it is not empty (
 * is skipped for them). */
R2(nonwindow_allocs_prefer_odd_free_entries) {
    R2H_NEED();
    unsigned n; r2h_entry_t *t = r2h_snap(&n); REQUIRE(t);
    unsigned odd_free = 0;
    for (unsigned i = 1; i < n; i += 2) if (!t[i].type) odd_free++;
    free(t);
    if (odd_free < 24) { t_skip("only %u free odd entries", odd_free); return; }
    HMENU m[8]; unsigned bad = 0;
    for (int i = 0; i < 8; i++) { m[i] = U(CreateMenu)(); if (m[i] && !(R2H_IDX(m[i]) & 1)) bad++; }
    for (int i = 0; i < 8; i++) { CHECK(m[i] != NULL); if (m[i]) U(DestroyMenu)(m[i]); }
    EXPECT_EQ(bad, 0);
}
/* Freed indices are pushed on the free list head, so the next allocations come back in reverse
 * order of release (tolerating a concurrent allocator in another process). */
R2(menu_free_list_is_lifo) {
    R2H_NEED();
    HMENU m[8]; unsigned idx[8];
    for (int i = 0; i < 8; i++) { m[i] = U(CreateMenu)(); if (!m[i]) { for (int k = 0; k < i; k++) U(DestroyMenu)(m[k]); t_skip("CreateMenu failed"); return; } idx[i] = R2H_IDX(m[i]); }
    for (int i = 0; i < 8; i++) U(DestroyMenu)(m[i]);
    HMENU n2[8]; int match = 0;
    for (int i = 0; i < 8; i++) { n2[i] = U(CreateMenu)(); if (n2[i] && R2H_IDX(n2[i]) == idx[7 - i]) match++; }
    for (int i = 0; i < 8; i++) if (n2[i]) U(DestroyMenu)(n2[i]);
    CHECKF(match >= 6, "only %d of 8 reallocations hit the LIFO order", match);
}
/* uniq' = uniq + 1, and 0xFFFF is skipped (stored as 1), so a slot cycled enough
 * times wraps 0xFFFE -> 1 and never exposes 0 or 0xFFFF. Another allocator in the system may
 * take and release the same slot between two of our cycles, which advances the counter by
 * more than one; so the new value must be a short forward walk of the successor function
 * from the previous one, and most pairs must be exact single steps. */
R2(uniq_successor_wraps_skipping_ffff) {
    R2H_NEED();
    HMENU prev = NULL; unsigned long iter; int wrapped = 0, pairs = 0, exact = 0;
    for (iter = 0; iter < 70000 && !wrapped; iter++) {
        HMENU h = U(CreateMenu)();
        if (!h) { t_skip("CreateMenu failed at %lu", iter); return; }
        unsigned u = R2H_UNIQ(h);
        if (u == 0 || u == 0xffff) { CHECKF(0, "handle %p carries reserved uniq 0x%x", h, u); U(DestroyMenu)(h); break; }
        if (prev && R2H_IDX(prev) == R2H_IDX(h)) {
            pairs++;
            unsigned cur = R2H_UNIQ(prev); int steps = 0, passed_wrap = 0;
            while (steps < 8) {
                if (cur == 0xfffe) passed_wrap = 1;
                cur = r2h_next_uniq((unsigned short)cur); steps++;
                if (cur == u) break;
            }
            if (cur != u) { CHECKF(0, "slot %u: uniq %u -> %u is not a short forward walk of the successor", R2H_IDX(h), R2H_UNIQ(prev), u); U(DestroyMenu)(h); break; }
            if (steps == 1) exact++;
            if (passed_wrap) { CHECK(u < 8); wrapped = 1; }
        }
        U(DestroyMenu)(h);
        prev = h;
    }
    CHECKF(pairs == 0 || exact * 10 >= pairs * 9, "only %d of %d same-slot pairs were single steps", exact, pairs);
    CHECK(pairs > 1000);
    if (exact != pairs) t_info("%d of %d same-slot pairs skipped extra steps (concurrent allocator)", pairs - exact, pairs);
    if (!wrapped) t_info("uniq wrap not reached after %lu cycles (%d same-slot pairs)", iter, pairs);
}
