/* x_menu_accel_hotkey_lifetime / accel: NtUserCreateAcceleratorTable, NtUserCopyAcceleratorTable, NtUserDestroyAcceleratorTable
 * seen through who may touch a table and how long it lives.
 *
 * A table is a USER object with a handle: any thread of any process in the session can read it (copy) and destroy it, there is
 * no owner check, only a type check; a stale or foreign-type handle is ERROR_INVALID_ACCEL_HANDLE (1403) and the table is
 * never touched. A table made on a thread outlives that thread; a table made by a process is destroyed when that process ends,
 * however it ends. The handle is validated like every USER handle: the low word picks the entry, the high word must match the
 * entry's uniqueness or be 0xFFFF (a wildcard), a zero high word never matches, and only the low 32 bits of the value count.
 * Buffers: the kernel probes count*6 bytes at 4-byte alignment.
 *
 * Determinism: handles are never printed; only counts, relations and symbolic results.
 */
#include "x.h"

#define COUNT(h) UN(NtUserCopyAcceleratorTable)((h), NULL, 0)

/* ---- creation and copy by size ---- */
static void roundtrip(int n) {
    NEED_ACC();
    HANDLE h = xm_acc_make(n);
    CHECK(h != NULL); if (!h) return;
    EXPECT_EQ(COUNT(h), n);
    xm_acc6 *o = VirtualAlloc(NULL, (size_t)n * 6 + 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    int got = UN(NtUserCopyAcceleratorTable)(h, o, n);
    EXPECT_EQ(got, n);
    int bad = 0;
    for (int i = 0; i < n && o; i++)
        if ((o[i].fVirt & 0x7f) != FVIRTKEY || o[i].key != 'A' + i % 26 || o[i].cmd != 1000 + i % 0x10000 || (o[i].fVirt & 0x80)) bad++;
    EXPECT_EQ(bad, 0);   /* the end-of-table flag the kernel keeps in the last entry is masked out on the way back */
    XOK(UN(NtUserDestroyAcceleratorTable)(h), 1);
    XFAIL(COUNT(h), XE_INVALID_ACCEL_HANDLE);
    if (o) VirtualFree(o, 0, MEM_RELEASE);
}
#define RT(n) XT(roundtrip_##n) { roundtrip(n); }
RT(1) RT(2) RT(3) RT(7) RT(64) RT(1000) RT(32767)

XT(create_beyond_limit_fails_with_invalid_parameter) {
    NEED_ACC();
    xm_acc6 a[1] AL4; xm_acc_fill(a, 1);
    XFAIL(UN(NtUserCreateAcceleratorTable)(a, 0x8000), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserCreateAcceleratorTable)(a, 0), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserCreateAcceleratorTable)(a, -1), XE_INVALID_PARAMETER);
}
XT(count_query_is_not_limited_by_capacity) {
    NEED_ACC();
    HANDLE h = xm_acc_make(5);
    xm_acc6 o[8] AL4;
    XOK(UN(NtUserCopyAcceleratorTable)(h, NULL, 1), 5);
    XOK(UN(NtUserCopyAcceleratorTable)(h, NULL, 100), 5);
    XOK(UN(NtUserCopyAcceleratorTable)(h, o, 8), 5);   /* capacity larger than the table: all entries */
    XOK(UN(NtUserCopyAcceleratorTable)(h, o, 2), 2);   /* smaller: truncated */
    UN(NtUserDestroyAcceleratorTable)(h);
}

/* ---- buffers ---- */
XT(create_with_null_table_fails_without_setting_an_error) {
    NEED_ACC();
    SET_ERR(XE_SENT);
    CHECK(UN(NtUserCreateAcceleratorTable)(NULL, 2) == NULL);
    EXPECT_ERR(XE_SENT);   /* the failure comes from the probe of the null pointer, which sets nothing */
}
XT(create_with_unreadable_table_is_noaccess) {
    NEED_ACC();
    XFAIL(UN(NtUserCreateAcceleratorTable)((void *)1, 2), XE_NOACCESS);
    XFAIL(UN(NtUserCreateAcceleratorTable)((void *)0xFFFF800000000000ULL, 2), XE_NOACCESS);
}
XT(create_with_misaligned_table_is_noaccess) {
    NEED_ACC();
    xm_acc6 a[4] AL4; xm_acc_fill(a, 4);
    XFAIL(UN(NtUserCreateAcceleratorTable)((char *)a + 2, 1), XE_NOACCESS);
    XFAIL(UN(NtUserCreateAcceleratorTable)((char *)a + 1, 1), XE_NOACCESS);
    XFAIL(UN(NtUserCreateAcceleratorTable)((char *)a + 3, 1), XE_NOACCESS);
}
XT(create_table_ending_at_page_end_works_and_one_more_entry_fails) {
    NEED_ACC();
    char *pg = VirtualAlloc(NULL, 8192, MEM_RESERVE, PAGE_NOACCESS);
    if (!pg || !VirtualAlloc(pg, 4096, MEM_COMMIT, PAGE_READWRITE)) { t_skip("no memory"); return; }
    xm_acc6 *edge = (xm_acc6 *)(pg + 4096 - 12);   /* 12 bytes: exactly two entries, 4-byte aligned */
    xm_acc_fill(edge, 2);
    HANDLE h = UN(NtUserCreateAcceleratorTable)(edge, 2);
    CHECK(h != NULL);
    if (h) { EXPECT_EQ(COUNT(h), 2); UN(NtUserDestroyAcceleratorTable)(h); }
    SET_ERR(XE_SENT);
    CHECK(UN(NtUserCreateAcceleratorTable)(edge, 3) == NULL);   /* the third entry lies in the guard page */
    VirtualFree(pg, 0, MEM_RELEASE);
}
XT(copy_to_unwritable_buffer_is_noaccess) {
    NEED_ACC();
    HANDLE h = xm_acc_make(2);
    XFAIL(UN(NtUserCopyAcceleratorTable)(h, (void *)1, 2), XE_NOACCESS);
    XFAIL(UN(NtUserCopyAcceleratorTable)(h, (void *)0xFFFF800000000000ULL, 2), XE_NOACCESS);
    xm_acc6 a[4] AL4;
    XFAIL(UN(NtUserCopyAcceleratorTable)(h, (char *)a + 2, 2), XE_NOACCESS);
    EXPECT_EQ(COUNT(h), 2);   /* the table is unharmed */
    UN(NtUserDestroyAcceleratorTable)(h);
}
XT(copy_probes_capacity_times_six_bytes) {
    NEED_ACC();
    HANDLE h = xm_acc_make(2);
    char *pg = VirtualAlloc(NULL, 8192, MEM_RESERVE, PAGE_NOACCESS);
    if (!pg || !VirtualAlloc(pg, 4096, MEM_COMMIT, PAGE_READWRITE)) { t_skip("no memory"); UN(NtUserDestroyAcceleratorTable)(h); return; }
    xm_acc6 *edge = (xm_acc6 *)(pg + 4096 - 12);
    XOK(UN(NtUserCopyAcceleratorTable)(h, edge, 2), 2);
    XFAIL(UN(NtUserCopyAcceleratorTable)(h, edge, 3), XE_NOACCESS);   /* asks for room the buffer does not have, though the table has two */
    VirtualFree(pg, 0, MEM_RELEASE);
    UN(NtUserDestroyAcceleratorTable)(h);
}
XT(copy_with_zero_capacity_and_a_buffer_copies_nothing) {
    NEED_ACC();
    HANDLE h = xm_acc_make(2);
    xm_acc6 o[2] AL4; memset(o, 0xCC, sizeof o);
    SET_ERR(XE_SENT);
    EXPECT_EQ(UN(NtUserCopyAcceleratorTable)(h, o, 0), 0);
    EXPECT_ERR(XE_SENT);
    EXPECT_EQ(o[0].key, 0xCCCC);
    UN(NtUserDestroyAcceleratorTable)(h);
}

/* ---- handle forms ---- */
XT(handle_with_wildcard_high_word_is_accepted) {
    NEED_ACC();
    HANDLE h = xm_acc_make(3);
    HANDLE w = (HANDLE)(((ULONG_PTR)h & 0xFFFF) | 0xFFFF0000u);
    EXPECT_EQ(COUNT(w), 3);
    XOK(UN(NtUserDestroyAcceleratorTable)(w), 1);       /* and it is the same table: the real handle is now dead */
    XFAIL(COUNT(h), XE_INVALID_ACCEL_HANDLE);
}
XT(handle_with_zero_high_word_is_rejected) {
    NEED_ACC();
    HANDLE h = xm_acc_make(3);
    HANDLE z = (HANDLE)((ULONG_PTR)h & 0xFFFF);
    XFAIL(COUNT(z), XE_INVALID_ACCEL_HANDLE);
    XFAIL(UN(NtUserDestroyAcceleratorTable)(z), XE_INVALID_ACCEL_HANDLE);
    EXPECT_EQ(COUNT(h), 3);
    UN(NtUserDestroyAcceleratorTable)(h);
}
XT(handle_with_wrong_high_word_is_rejected) {
    NEED_ACC();
    HANDLE h = xm_acc_make(3);
    for (ULONG_PTR x = 1; x <= 0x8000; x <<= 1) {
        HANDLE f = (HANDLE)((ULONG_PTR)h ^ (x << 16));
        if (((ULONG_PTR)f >> 16 & 0xFFFF) == 0xFFFF || ((ULONG_PTR)f >> 16 & 0xFFFF) == 0) continue;
        SET_ERR(XE_SENT);
        int r = COUNT(f);
        CHECKF(r == 0 && GetLastError() == XE_INVALID_ACCEL_HANDLE, "high word flip 0x%llx: ret %d err %lu", (unsigned long long)x, r, GetLastError());
    }
    EXPECT_EQ(COUNT(h), 3);
    UN(NtUserDestroyAcceleratorTable)(h);
}
XT(handle_upper_half_of_the_value_is_ignored) {
    NEED_ACC();
    HANDLE h = xm_acc_make(3);
    EXPECT_EQ(COUNT((HANDLE)((ULONG_PTR)h | 0x100000000ULL)), 3);
    EXPECT_EQ(COUNT((HANDLE)((ULONG_PTR)h | 0xFFFFFFFF00000000ULL)), 3);
    XOK(UN(NtUserDestroyAcceleratorTable)((HANDLE)((ULONG_PTR)h | 0x7777000000000000ULL)), 1);
    XFAIL(COUNT(h), XE_INVALID_ACCEL_HANDLE);
}
XT(handle_values_that_are_no_object_are_rejected) {
    NEED_ACC();
    ULONG_PTR bad[] = { 0, 1, 2, 0x1234, 0xFFFF, 0x10000, 0x7FFE7777, 0xDEADBEEF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFFULL, 0x7FFFFFFF0000ULL };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(XE_SENT);
        int c = COUNT((HANDLE)bad[i]);
        DWORD e = GetLastError();
        CHECKF(c == 0 && e == XE_INVALID_ACCEL_HANDLE, "value 0x%llx: ret %d err %lu", (unsigned long long)bad[i], c, e);
        SET_ERR(XE_SENT);
        c = UN(NtUserDestroyAcceleratorTable)((HANDLE)bad[i]);
        e = GetLastError();
        CHECKF(c == 0 && e == XE_INVALID_ACCEL_HANDLE, "destroy 0x%llx: ret %d err %lu", (unsigned long long)bad[i], c, e);
    }
}

/* ---- handles of other kinds ---- */
XT(destroy_refuses_every_other_object_kind_and_touches_none) {
    NEED_ACC();
    HWND w = k_make_window("xm");
    HMENU m = U(CreatePopupMenu)();
    HICON ic = U(CreateIcon)(GetModuleHandleA(NULL), 1, 1, 1, 1, (BYTE[]){0xFF}, (BYTE[]){0x00});
    HBITMAP bm = U(CreateBitmap)(1, 1, 1, 1, NULL);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE objs[] = { w, m, ic, bm, ev, GetCurrentProcess(), (HANDLE)U(GetStockObject)(BLACK_BRUSH) };
    const char *names[] = { "window", "menu", "icon", "bitmap", "event", "pseudo process", "stock brush" };
    for (unsigned i = 0; i < sizeof objs / sizeof objs[0]; i++) {
        if (!objs[i]) continue;
        SET_ERR(XE_SENT);
        int r = UN(NtUserDestroyAcceleratorTable)(objs[i]);
        DWORD e = GetLastError();
        CHECKF(r == 0 && e == XE_INVALID_ACCEL_HANDLE, "%s: ret %d err %lu", names[i], r, e);
    }
    CHECK(U(IsWindow)(w)); CHECK(U(IsMenu)(m));
    if (ic) { ICONINFO ii; CHECK(U(GetIconInfo)(ic, &ii)); if (ii.hbmMask) U(DeleteObject)(ii.hbmMask); if (ii.hbmColor) U(DeleteObject)(ii.hbmColor); }
    BITMAP b; CHECK(U(GetObjectA)(bm, sizeof b, &b) != 0);
    DWORD fl; CHECK(GetHandleInformation(ev, &fl));
    U(DestroyWindow)(w); U(DestroyMenu)(m); if (ic) U(DestroyIcon)(ic); U(DeleteObject)(bm); CloseHandle(ev);
}
XT(table_handle_is_rejected_by_the_menu_window_cursor_and_hotkey_paths) {
    NEED_ACC(); NEED(NtUserSetMenuFlagRtoL); NEED(NtUserRegisterHotKey);
    HANDLE h = xm_acc_make(2);
    SET_ERR(XE_SENT); CHECK(!U(DestroyMenu)((HMENU)h)); EXPECT_ERR(XE_INVALID_MENU_HANDLE);
    SET_ERR(XE_SENT); CHECK(!U(IsMenu)((HMENU)h)); EXPECT_ERR(XE_INVALID_MENU_HANDLE);
    CHECK(!U(IsWindow)((HWND)h));
    SET_ERR(XE_SENT); CHECK(!U(DestroyCursor)((HCURSOR)h)); EXPECT_ERR(1402);
    SET_ERR(XE_SENT); CHECK(!U(DestroyIcon)((HICON)h)); EXPECT_ERR(1402);
    XFAIL(UN(NtUserSetMenuFlagRtoL)((HMENU)h), XE_INVALID_MENU_HANDLE);
    KEY(k);
    XFAIL(UN(NtUserRegisterHotKey)((HWND)h, 1, k.mods, k.vk), XE_INVALID_HWND);
    EXPECT_EQ(COUNT(h), 2);   /* none of it touched the table */
    UN(NtUserDestroyAcceleratorTable)(h);
}
XT(other_objects_are_not_tables_for_copy_either) {
    NEED_ACC();
    HWND w = k_make_window("xm");
    HMENU m = U(CreatePopupMenu)();
    xm_acc6 o[2] AL4; memset(o, 0xCC, sizeof o);
    XFAIL(UN(NtUserCopyAcceleratorTable)((HANDLE)w, o, 2), XE_INVALID_ACCEL_HANDLE);
    XFAIL(UN(NtUserCopyAcceleratorTable)((HANDLE)m, o, 2), XE_INVALID_ACCEL_HANDLE);
    XFAIL(UN(NtUserCopyAcceleratorTable)((HANDLE)m, NULL, 0), XE_INVALID_ACCEL_HANDLE);
    EXPECT_EQ(o[0].key, 0xCCCC);
    U(DestroyWindow)(w); U(DestroyMenu)(m);
}

/* ---- destroy twice, destroy and use ---- */
XT(destroy_twice_second_is_invalid_handle) {
    NEED_ACC();
    HANDLE h = xm_acc_make(2);
    XOK(UN(NtUserDestroyAcceleratorTable)(h), 1);
    for (int i = 0; i < 5; i++) XFAIL(UN(NtUserDestroyAcceleratorTable)(h), XE_INVALID_ACCEL_HANDLE);
}
XT(destroyed_table_cannot_be_copied_or_counted) {
    NEED_ACC();
    HANDLE h = xm_acc_make(2);
    UN(NtUserDestroyAcceleratorTable)(h);
    xm_acc6 o[2] AL4; memset(o, 0xCC, sizeof o);
    XFAIL(UN(NtUserCopyAcceleratorTable)(h, o, 2), XE_INVALID_ACCEL_HANDLE);
    XFAIL(COUNT(h), XE_INVALID_ACCEL_HANDLE);
    EXPECT_EQ(o[0].key, 0xCCCC);
}
XT(translate_accelerator_with_a_destroyed_or_null_table_is_invalid_accel_handle) {
    NEED_ACC();
    HWND w = k_make_window("xm");
    HANDLE h = xm_acc_make(2);
    UN(NtUserDestroyAcceleratorTable)(h);
    MSG msg; memset(&msg, 0, sizeof msg); msg.hwnd = w; msg.message = WM_KEYDOWN; msg.wParam = 'A';
    SET_ERR(XE_SENT); EXPECT_EQ(U(TranslateAcceleratorW)(w, (HACCEL)h, &msg), 0); EXPECT_ERR(XE_INVALID_ACCEL_HANDLE);
    SET_ERR(XE_SENT); EXPECT_EQ(U(TranslateAcceleratorW)(w, NULL, &msg), 0); EXPECT_ERR(XE_INVALID_ACCEL_HANDLE);
    U(DestroyWindow)(w);
}
XT(translate_accelerator_without_a_window_does_nothing_and_sets_no_error) {
    NEED_ACC();
    HANDLE h = xm_acc_make(2);
    MSG msg; memset(&msg, 0, sizeof msg); msg.message = WM_KEYDOWN; msg.wParam = 'A';
    SET_ERR(XE_SENT);
    EXPECT_EQ(U(TranslateAcceleratorW)(NULL, (HACCEL)h, &msg), 0);
    EXPECT_ERR(XE_SENT);
    UN(NtUserDestroyAcceleratorTable)(h);
}
XT(user32_destroy_matches_the_syscall) {
    NEED_ACC();
    HANDLE h = xm_acc_make(2);
    XOK(U(DestroyAcceleratorTable)((HACCEL)h), 1);
    SET_ERR(XE_SENT); EXPECT_EQ(U(DestroyAcceleratorTable)((HACCEL)h), 0); EXPECT_ERR(XE_INVALID_ACCEL_HANDLE);
    SET_ERR(XE_SENT); EXPECT_EQ(U(CopyAcceleratorTableW)((HACCEL)h, NULL, 0), 0); EXPECT_ERR(XE_INVALID_ACCEL_HANDLE);
}
XT(user32_copy_agrees_with_the_syscall) {
    NEED_ACC();
    HANDLE h = xm_acc_make(4);
    EXPECT_EQ(U(CopyAcceleratorTableW)((HACCEL)h, NULL, 0), 4);
    ACCEL a[4]; memset(a, 0, sizeof a);
    EXPECT_EQ(U(CopyAcceleratorTableW)((HACCEL)h, a, 4), 4);
    EXPECT_EQ(a[3].key, 'D'); EXPECT_EQ(a[3].cmd, 1003);
    UN(NtUserDestroyAcceleratorTable)(h);
}

/* ---- threads ---- */
struct tctx { HANDLE h[256]; int n; HANDLE start; };
static DWORD WINAPI make_on_thread(LPVOID p) { struct tctx *c = p; for (int i = 0; i < c->n; i++) c->h[i] = xm_acc_make(1 + i % 4); return 0; }
static DWORD WINAPI destroy_on_thread(LPVOID p) { struct tctx *c = p; for (int i = 0; i < c->n; i++) if (c->h[i]) c->h[i] = (HANDLE)(ULONG_PTR)UN(NtUserDestroyAcceleratorTable)(c->h[i]); return 0; }
static void run_thread(LPTHREAD_START_ROUTINE f, void *p) { HANDLE t = CreateThread(NULL, 0, f, p, 0, NULL); if (t) { WaitForSingleObject(t, 20000); CloseHandle(t); } }

XT(table_outlives_the_thread_that_made_it) {
    NEED_ACC();
    struct tctx c; memset(&c, 0, sizeof c); c.n = 1;
    run_thread(make_on_thread, &c);
    CHECK(c.h[0] != NULL); if (!c.h[0]) return;
    EXPECT_EQ(COUNT(c.h[0]), 1);
    xm_acc6 o[1] AL4;
    EXPECT_EQ(UN(NtUserCopyAcceleratorTable)(c.h[0], o, 1), 1);
    XOK(UN(NtUserDestroyAcceleratorTable)(c.h[0]), 1);
}
XT(table_made_on_a_thread_is_destroyed_by_another) {
    NEED_ACC();
    struct tctx c; memset(&c, 0, sizeof c); c.n = 1;
    run_thread(make_on_thread, &c);
    HANDLE h = c.h[0];
    run_thread(destroy_on_thread, &c);
    EXPECT_EQ((ULONG_PTR)c.h[0], 1);   /* the helper stores the destroy result */
    XFAIL(COUNT(h), XE_INVALID_ACCEL_HANDLE);
}
XT(table_made_here_is_destroyed_on_a_thread_and_gone_here) {
    NEED_ACC();
    struct tctx c; memset(&c, 0, sizeof c); c.n = 1;
    c.h[0] = xm_acc_make(2);
    HANDLE h = c.h[0];
    run_thread(destroy_on_thread, &c);
    EXPECT_EQ((ULONG_PTR)c.h[0], 1);
    XFAIL(UN(NtUserDestroyAcceleratorTable)(h), XE_INVALID_ACCEL_HANDLE);
}
XT(many_tables_across_threads_counts_and_distinct_handles) {
    NEED_ACC();
    static struct tctx a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    a.n = b.n = 100;
    run_thread(make_on_thread, &a); run_thread(make_on_thread, &b);
    int bad = 0, dup = 0;
    for (int i = 0; i < 100; i++) {
        if (!a.h[i] || !b.h[i]) { bad++; continue; }
        if (COUNT(a.h[i]) != 1 + i % 4 || COUNT(b.h[i]) != 1 + i % 4) bad++;
        for (int j = 0; j < 100; j++) { if (a.h[i] == b.h[j]) dup++; if (j > i && a.h[i] == a.h[j]) dup++; }
    }
    EXPECT_EQ(bad, 0); EXPECT_EQ(dup, 0);
    run_thread(destroy_on_thread, &a);                       /* a's tables die on a third thread */
    for (int i = 0; i < 100; i++) UN(NtUserDestroyAcceleratorTable)(b.h[i]);
}
struct race { HANDLE h; volatile LONG wins; HANDLE go; };
static DWORD WINAPI race_destroy(LPVOID p) {
    struct race *r = p;
    WaitForSingleObject(r->go, 5000);
    if (UN(NtUserDestroyAcceleratorTable)(r->h) == 1) InterlockedIncrement(&r->wins);
    return 0;
}
XT(concurrent_destroy_has_exactly_one_winner) {
    NEED_ACC();
    for (int round = 0; round < 20; round++) {
        struct race r; r.h = xm_acc_make(2); r.wins = 0; r.go = CreateEventA(NULL, TRUE, FALSE, NULL);
        HANDLE t[4];
        for (int i = 0; i < 4; i++) t[i] = CreateThread(NULL, 0, race_destroy, &r, 0, NULL);
        SetEvent(r.go);
        for (int i = 0; i < 4; i++) { WaitForSingleObject(t[i], 10000); CloseHandle(t[i]); }
        CloseHandle(r.go);
        CHECKF(r.wins == 1, "round %d: %ld winners", round, r.wins);
        if (r.wins != 1) break;
    }
}
XT(destroy_then_create_never_revives_the_old_handle) {
    NEED_ACC();
    int revived = 0;
    for (int i = 0; i < 50; i++) {
        HANDLE h = xm_acc_make(1);
        UN(NtUserDestroyAcceleratorTable)(h);
        HANDLE n = xm_acc_make(1);
        if (COUNT(h) != 0) revived++;
        if (COUNT(n) != 1) revived++;
        UN(NtUserDestroyAcceleratorTable)(n);
    }
    EXPECT_EQ(revived, 0);
}

/* ---- other processes ---- */
/* Child: makes a table (and optionally more), publishes the handles, waits for the go flag, ends in the way asked. */
CHILD(xm_acc_owner) {
    HANDLE m; struct xm_shm *s = xm_shm_open(argv[1], &m); if (!s) return 1;
    t_module("user32.dll");
    int n = argc > 2 ? atoi(argv[2]) : 1;
    for (int i = 0; i < n && i < 12; i++) s->v[i] = (LONG64)(ULONG_PTR)xm_acc_make(3);
    InterlockedExchange(&s->ready, 1);
    xm_wait(&s->go, 20000);
    int me = 0;
    for (int i = 0; i < n && i < 12; i++) if (UN(NtUserCopyAcceleratorTable)((HANDLE)(ULONG_PTR)s->v[i], NULL, 0) == 3) me++;
    s->e[0] = me;
    InterlockedExchange(&s->done, 1);
    xm_wait(&s->go, 1);
    if (argc > 3 && !strcmp(argv[3], "kill")) TerminateProcess(GetCurrentProcess(), 9);
    return 0;
}
struct owner { HANDLE p; PROCESS_INFORMATION pi; struct xm_shm *s; HANDLE sm; };
static int owner_start(struct owner *o, int n, const char *how) {
    memset(o, 0, sizeof *o);
    char ex[32]; snprintf(ex, sizeof ex, "%d %s", n, how);
    const char *tag = xm_tag();
    o->s = xm_shm_create(tag, &o->sm);
    if (!o->s) return 0;
    o->p = xm_spawn("xm_acc_owner", tag, ex, &o->pi);
    if (!o->p) return 0;
    if (!xm_wait(&o->s->ready, 20000)) { TerminateProcess(o->p, 1); xm_reap(o->p, &o->pi, 5000); o->p = NULL; return 0; }
    return 1;
}
static DWORD owner_finish(struct owner *o) {
    InterlockedExchange(&o->s->go, 1);
    xm_wait(&o->s->done, 10000);
    DWORD ec = xm_reap(o->p, &o->pi, 15000);
    o->p = NULL;
    UnmapViewOfFile(o->s); CloseHandle(o->sm);
    return ec;
}
#define OH(o, i) ((HANDLE)(ULONG_PTR)(o).s->v[i])
#define NEED_CHILD() do { NEED_ACC(); } while (0)

XT(table_of_another_process_can_be_counted_and_copied) {
    NEED_CHILD();
    struct owner o; if (!owner_start(&o, 1, "")) { CHECK(0 && "child did not start"); return; }
    HANDLE h = OH(o, 0);
    EXPECT_EQ(COUNT(h), 3);
    xm_acc6 a[3] AL4; memset(a, 0, sizeof a);
    EXPECT_EQ(UN(NtUserCopyAcceleratorTable)(h, a, 3), 3);
    EXPECT_EQ(a[0].key, 'A'); EXPECT_EQ(a[1].cmd, 1001); EXPECT_EQ(a[2].key, 'C');
    EXPECT_EQ(U(CopyAcceleratorTableW)((HACCEL)h, NULL, 0), 3);
    EXPECT_EQ(owner_finish(&o), 0);
}
XT(table_of_another_process_can_be_destroyed_and_its_owner_then_sees_it_gone) {
    NEED_CHILD();
    struct owner o; if (!owner_start(&o, 1, "")) { CHECK(0 && "child did not start"); return; }
    XOK(UN(NtUserDestroyAcceleratorTable)(OH(o, 0)), 1);
    XFAIL(COUNT(OH(o, 0)), XE_INVALID_ACCEL_HANDLE);
    EXPECT_EQ(owner_finish(&o), 0);
}
XT(owner_sees_its_table_alive_when_nobody_destroyed_it) {
    NEED_CHILD();
    struct owner o; if (!owner_start(&o, 1, "")) { CHECK(0 && "child did not start"); return; }
    InterlockedExchange(&o.s->go, 1);
    xm_wait(&o.s->done, 10000);
    EXPECT_EQ(o.s->e[0], 1);   /* the child counted its own table (3 entries) */
    EXPECT_EQ(xm_reap(o.p, &o.pi, 15000), 0);
}
XT(tables_die_with_their_process_on_normal_exit) {
    NEED_CHILD();
    struct owner o; if (!owner_start(&o, 6, "")) { CHECK(0 && "child did not start"); return; }
    HANDLE h[6]; for (int i = 0; i < 6; i++) { h[i] = OH(o, i); EXPECT_EQ(COUNT(h[i]), 3); }
    EXPECT_EQ(owner_finish(&o), 0);
    int alive = 0;
    for (int i = 0; i < 6; i++) { SET_ERR(XE_SENT); if (COUNT(h[i]) != 0) alive++; else if (GetLastError() != XE_INVALID_ACCEL_HANDLE) alive += 100; }
    EXPECT_EQ(alive, 0);
}
XT(tables_die_with_their_process_when_it_is_killed) {
    NEED_CHILD();
    struct owner o; if (!owner_start(&o, 4, "kill")) { CHECK(0 && "child did not start"); return; }
    HANDLE h[4]; for (int i = 0; i < 4; i++) h[i] = OH(o, i);
    InterlockedExchange(&o.s->go, 1);
    xm_wait(&o.s->done, 10000);
    EXPECT_EQ(xm_reap(o.p, &o.pi, 15000), 9);
    int alive = 0;
    for (int i = 0; i < 4; i++) if (COUNT(h[i]) != 0) alive++;
    EXPECT_EQ(alive, 0);
}
XT(tables_die_with_their_process_when_it_is_terminated_from_outside) {
    NEED_CHILD();
    struct owner o; if (!owner_start(&o, 3, "")) { CHECK(0 && "child did not start"); return; }
    HANDLE h[3]; for (int i = 0; i < 3; i++) h[i] = OH(o, i);
    TerminateProcess(o.p, 5);
    EXPECT_EQ(xm_reap(o.p, &o.pi, 15000), 5);
    int alive = 0;
    for (int i = 0; i < 3; i++) { SET_ERR(XE_SENT); if (COUNT(h[i]) != 0) alive++; }
    EXPECT_EQ(alive, 0);
}
XT(a_new_table_after_owner_exit_does_not_revive_the_old_handles) {
    NEED_CHILD();
    struct owner o; if (!owner_start(&o, 8, "")) { CHECK(0 && "child did not start"); return; }
    HANDLE h[8]; for (int i = 0; i < 8; i++) h[i] = OH(o, i);
    owner_finish(&o);
    HANDLE n[16]; for (int i = 0; i < 16; i++) n[i] = xm_acc_make(1);   /* slots just freed are the ones reused */
    int revived = 0;
    for (int i = 0; i < 8; i++) if (COUNT(h[i]) != 0) revived++;
    EXPECT_EQ(revived, 0);
    for (int i = 0; i < 16; i++) UN(NtUserDestroyAcceleratorTable)(n[i]);
}
