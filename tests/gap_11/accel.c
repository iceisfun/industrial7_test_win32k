/* gap_11 / NtUserCreateAcceleratorTable with Copy and Destroy.
 *   Create(ptr, n): 1 <= n <= 0x7fff else SetLastError(0x57)/NULL; ProbeForRead(ptr, n*6, align 4 (2 for wow64));
 *     the kernel allocates a type 8 handle-table object, copies n*6 bytes of entries, records cEntries = n,
 *     and the first byte of the LAST entry gets |= 0x80 (FLAST). Returns the object's handle.
 *   Copy(h, NULL, _)   -> cEntries (cap ignored). Copy(h, buf, cap): ProbeForWrite(buf, cap*6), n = min(cap, cEntries),
 *     per entry: the pad byte := 0, 4 bytes (fVirt,pad,key) and 2 bytes (cmd) copied out, out fVirt &= 0x7f.
 *     Invalid handle -> 0 (handle validation sets ERROR_INVALID_ACCEL_HANDLE).
 *   Destroy(h): handle validation fails -> 0; else the object is destroyed and freed, return 1.
 * The object lives outside the desktop heap, so local state is limited to the USER handle-table entry. */
#include "g11.h"

#define AC_NEED() NtUserCreateAcceleratorTable_pfn cr = (NtUserCreateAcceleratorTable_pfn)g11_nt("NtUserCreateAcceleratorTable"); \
    NtUserCopyAcceleratorTable_pfn cp = (NtUserCopyAcceleratorTable_pfn)g11_nt("NtUserCopyAcceleratorTable"); \
    NtUserDestroyAcceleratorTable_pfn de = (NtUserDestroyAcceleratorTable_pfn)g11_nt("NtUserDestroyAcceleratorTable"); \
    if (!cr || !cp || !de) { t_skip("missing stub"); return; } \
    if (!U(CreateWindowExW)) { t_skip("no user32"); return; }

typedef struct { BYTE fVirt, pad; WORD key, cmd; } acc6;   /* the 6-byte wire/object format */
/* The kernel probes the table with 4-byte alignment (a 2-aligned buffer fails with ERROR_NOACCESS), but a 6-byte element only
 * guarantees 2: every local table is forced to a 4-byte boundary. */
#define AL4 __attribute__((aligned(4)))

static void ac_fill(acc6 *a, int n) {
    for (int i = 0; i < n; i++) { a[i].fVirt = FVIRTKEY; a[i].pad = 0; a[i].key = (WORD)(0x41 + i % 26); a[i].cmd = (WORD)(1000 + i); }
}

TEST(gap11_acc_entry_size_is_six) {
    CHECK(sizeof(acc6) == 6);
    CHECK(sizeof(ACCEL) == 6);
    EXPECT_EQ(offsetof(acc6, key), 2);
    EXPECT_EQ(offsetof(acc6, cmd), 4);
}

TEST(gap11_acc_handle_table_entry_state) {
    AC_NEED();
    acc6 a[2] AL4; ac_fill(a, 2);
    HANDLE h = cr(a, 2);
    CHECK(h != NULL);
    if (!h) return;
    struct k_user_entry e;
    if (k_user_entry(h, &e)) {
        EXPECT_EQ(e.type, G11_TYPE_ACCEL);                   /* handle-table type 8 */
        EXPECT_EQ(e.uniq, ((ULONG_PTR)h >> 16) & 0xffff);    /* uniq half of the handle matches the table entry */
        CHECK((e.flags & 1) == 0);                           /* live object: destroy bit clear */
    } else t_note("handle table not readable (Wine or layout)");
    CHECK(de(h) == 1);
}

TEST(gap11_acc_handles_distinct_and_typed) {
    AC_NEED();
    acc6 a[1] AL4; ac_fill(a, 1);
    HANDLE h[8];
    for (int i = 0; i < 8; i++) h[i] = cr(a, 1);
    for (int i = 0; i < 8; i++) {
        CHECK(h[i] != NULL);
        for (int j = i + 1; j < 8; j++) CHECK(h[i] != h[j]);
        struct k_user_entry e;
        if (h[i] && k_user_entry(h[i], &e)) EXPECT_EQ(e.type, G11_TYPE_ACCEL);
    }
    for (int i = 0; i < 8; i++) if (h[i]) CHECK(de(h[i]) == 1);
}

TEST(gap11_acc_reused_slot_changes_handle) {
    AC_NEED();
    acc6 a[1] AL4; ac_fill(a, 1);
    HANDLE h1 = cr(a, 1);
    CHECK(h1 != NULL); if (!h1) return;
    CHECK(de(h1) == 1);
    HANDLE h2 = cr(a, 1);
    CHECK(h2 != NULL);
    CHECK(h2 != h1);                                         /* the unique counter in the handle's upper half advances on reuse */
    SET_ERR(G11_SENT);
    CHECK(cp(h1, NULL, 0) == 0);                             /* stale handle fails HMValidateHandle even if the index is reused */
    EXPECT_ERR(ERROR_INVALID_ACCEL_HANDLE);
    if (h2) CHECK(de(h2) == 1);
}

TEST(gap11_acc_destroy_marks_entry_unusable) {
    AC_NEED();
    acc6 a[1] AL4; ac_fill(a, 1);
    HANDLE h = cr(a, 1);
    CHECK(h != NULL); if (!h) return;
    CHECK(de(h) == 1);
    struct k_user_entry e;
    if (k_user_entry(h, &e)) CHECKF(e.type != G11_TYPE_ACCEL || (e.flags & 1), "freed entry still looks live: type %u flags %u", e.type, e.flags);
    SET_ERR(G11_SENT);
    CHECK(de(h) == 0); EXPECT_ERR(ERROR_INVALID_ACCEL_HANDLE);
}

TEST(gap11_acc_count_query_ignores_cap) {
    AC_NEED();
    static const int ns[] = { 1, 2, 3, 7, 255, 256, 1000 };
    for (unsigned k = 0; k < sizeof ns / sizeof ns[0]; k++) {
        int n = ns[k];
        acc6 *a = calloc(n, sizeof *a); if (!a) { t_skip("no memory"); return; }
        ac_fill(a, n);
        HANDLE h = cr(a, n);
        CHECKF(h != NULL, "n=%d", n);
        if (h) {
            G11_EQ(cp(h, NULL, 0), n);                    /* NULL buffer: cEntries, whatever the cap */
            G11_EQ(cp(h, NULL, 1), n);
            G11_EQ(cp(h, NULL, -5), n);
            G11_EQ(cp(h, NULL, 0x7fffffff), n);
            CHECK(de(h) == 1);
        }
        free(a);
    }
}

TEST(gap11_acc_copy_full_roundtrip_table) {
    AC_NEED();
    static const int ns[] = { 1, 2, 5, 26, 27, 300 };
    for (unsigned k = 0; k < sizeof ns / sizeof ns[0]; k++) {
        int n = ns[k];
        acc6 *a = calloc(n, sizeof *a), *o = calloc(n + 1, sizeof *o);
        if (!a || !o) { t_skip("no memory"); return; }
        ac_fill(a, n);
        HANDLE h = cr(a, n);
        CHECKF(h != NULL, "n=%d", n);
        if (h) {
            memset(o, 0xCC, (n + 1) * sizeof *o);
            G11_EQ(cp(h, o, n), n);
            for (int i = 0; i < n; i++)
                CHECKF(!memcmp(&o[i], &a[i], 6), "n=%d entry %d differs", n, i);
            CHECKF(((BYTE *)&o[n])[0] == 0xCC && ((BYTE *)&o[n])[5] == 0xCC, "n=%d wrote past the end", n);
            CHECK(de(h) == 1);
        }
        free(a); free(o);
    }
}

TEST(gap11_acc_fvirt_bits_survive_except_flast) {
    AC_NEED();
    for (int fv = 0; fv < 256; fv++) {
        acc6 a[2] AL4; ac_fill(a, 2);
        a[0].fVirt = (BYTE)fv; a[1].fVirt = (BYTE)fv;
        HANDLE h = cr(a, 2);
        CHECKF(h != NULL, "fVirt 0x%02x not accepted", fv);        /* contents are not validated */
        if (!h) continue;
        acc6 o[2] AL4; memset(o, 0, sizeof o);
        G11_EQ(cp(h, o, 2), 2);
        /* bit 7 is the kernel's FLAST marker: set on the last entry at creation, masked out of every copied entry */
        CHECKF(o[0].fVirt == (fv & 0x7f) && o[1].fVirt == (fv & 0x7f), "fVirt 0x%02x -> 0x%02x/0x%02x", fv, o[0].fVirt, o[1].fVirt);
        de(h);
    }
}

TEST(gap11_acc_key_and_cmd_full_16_bits) {
    AC_NEED();
    static const WORD v[] = { 0, 1, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xfffe, 0xffff };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
        for (unsigned j = 0; j < sizeof v / sizeof v[0]; j++) {
            acc6 a AL4 = { FVIRTKEY, 0, v[i], v[j] }, o AL4;
            HANDLE h = cr(&a, 1);
            CHECK(h != NULL); if (!h) continue;
            memset(&o, 0xEE, sizeof o);
            G11_EQ(cp(h, &o, 1), 1);
            CHECKF(o.key == v[i] && o.cmd == v[j], "key %x cmd %x -> %x %x", v[i], v[j], o.key, o.cmd);
            de(h);
        }
}

TEST(gap11_acc_pad_byte_is_zeroed_by_copy) {
    AC_NEED();
    acc6 a[3] AL4; ac_fill(a, 3);
    for (int i = 0; i < 3; i++) a[i].pad = 0xEE;
    HANDLE h = cr(a, 3);
    CHECK(h != NULL); if (!h) return;
    acc6 o[3] AL4; memset(o, 0x55, sizeof o);
    G11_EQ(cp(h, o, 3), 3);
    /* the copy loop clears obj+0x1d (the pad byte) before copying the first four bytes of each entry */
    for (int i = 0; i < 3; i++) EXPECT_EQ(o[i].pad, 0);
    de(h);
}

TEST(gap11_acc_copy_cap_truncates_and_clamps) {
    AC_NEED();
    acc6 a[10] AL4, o[16] AL4; ac_fill(a, 10);
    HANDLE h = cr(a, 10);
    CHECK(h != NULL); if (!h) return;
    static const int caps[] = { 0, 1, 2, 9, 10, 11, 16 };
    for (unsigned k = 0; k < sizeof caps / sizeof caps[0]; k++) {
        int cap = caps[k], want = cap < 10 ? cap : 10;
        memset(o, 0xCC, sizeof o);
        G11_EQ(cp(h, o, cap), want);                      /* min(cap, cEntries) */
        for (int i = 0; i < want; i++) CHECKF(o[i].cmd == a[i].cmd, "cap %d entry %d", cap, i);
        for (int i = want; i < 16; i++) CHECKF(((BYTE *)&o[i])[0] == 0xCC && ((BYTE *)&o[i])[5] == 0xCC, "cap %d touched entry %d", cap, i);
    }
    de(h);
}

TEST(gap11_acc_copy_negative_cap_with_buffer) {
    AC_NEED();
    acc6 a[2] AL4, o[2] AL4; ac_fill(a, 2);
    HANDLE h = cr(a, 2);
    CHECK(h != NULL); if (!h) return;
    memset(o, 0xCC, sizeof o);
    /* 0x2aaa...aaa < (ulonglong)(longlong)cap is false only for small positive caps; negative caps raise in the kernel
     * (ExRaiseAccessViolation, caught by the syscall thunk) - not exercised here, only cap == 0 with a buffer. */
    G11_EQ(cp(h, o, 0), 0);
    CHECK(((BYTE *)&o[0])[0] == 0xCC);
    de(h);
}

TEST(gap11_acc_copy_invalid_handles) {
    AC_NEED();
    HWND w = g11_mk(WS_POPUP, 0, NULL);
    HMENU m = U(CreatePopupMenu) ? U(CreatePopupMenu)() : NULL;
    acc6 a[1] AL4, o[1] AL4; ac_fill(a, 1);
    HANDLE dead = cr(a, 1); if (dead) de(dead);
    HANDLE bad[] = { NULL, (HANDLE)(LONG_PTR)-1, (HANDLE)0x7FFE7777, (HANDLE)w, (HANDLE)m, dead };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (i >= 3 && !bad[i]) continue;
        memset(o, 0xCC, sizeof o);
        SET_ERR(G11_SENT);
        int r = cp(bad[i], o, 1);
        DWORD e = GetLastError();
        CHECKF(r == 0, "handle %p count query %d", bad[i], r);
        CHECKF(e == ERROR_INVALID_ACCEL_HANDLE, "handle %p error %lu", bad[i], e);
        CHECKF(((BYTE *)&o[0])[0] == 0xCC, "handle %p wrote output", bad[i]);
        SET_ERR(G11_SENT);
        CHECKF(cp(bad[i], NULL, 0) == 0, "handle %p NULL query", bad[i]);
    }
    if (m) U(DestroyMenu)(m);
    g11_kill(w);
}

TEST(gap11_acc_destroy_wrong_type_handles_untouched) {
    AC_NEED(); REQUIRE(U(IsWindow));
    HWND w = g11_mk(WS_POPUP, 0, NULL);
    HMENU m = U(CreatePopupMenu) ? U(CreatePopupMenu)() : NULL;
    if (!w) { t_skip("no window"); return; }
    SET_ERR(G11_SENT);
    G11_EQ(de((HANDLE)w), 0); EXPECT_ERR(ERROR_INVALID_ACCEL_HANDLE);
    CHECK(U(IsWindow)(w));                                   /* wrong type: HMValidateHandle(h, 8) fails, nothing destroyed */
    if (m) {
        SET_ERR(G11_SENT);
        G11_EQ(de((HANDLE)m), 0); EXPECT_ERR(ERROR_INVALID_ACCEL_HANDLE);
        CHECK(U(IsMenu)(m));
        U(DestroyMenu)(m);
    }
    g11_kill(w);
}

TEST(gap11_acc_destroy_returns_exactly_one) {
    AC_NEED();
    acc6 a[1] AL4; ac_fill(a, 1);
    HANDLE h = cr(a, 1);
    CHECK(h != NULL); if (!h) return;
    G11_EQ(de(h), 1);
    SET_ERR(G11_SENT);
    G11_EQ(de(h), 0); EXPECT_ERR(ERROR_INVALID_ACCEL_HANDLE);
}

TEST(gap11_acc_create_count_bounds_via_nt) {
    AC_NEED();
    acc6 a[1] AL4; ac_fill(a, 1);
    static const int bad[] = { 0, -1, 0x8000, 0x10000, 0x7fffffff, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(G11_SENT);
        HANDLE h = cr(a, bad[i]);
        CHECKF(h == NULL, "count %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);                 /* range test (n-1 < 0x7fff) precedes any read of the buffer */
        if (h) de(h);
    }
}

struct xt_ctx { NtUserCopyAcceleratorTable_pfn cp; NtUserDestroyAcceleratorTable_pfn de; HANDLE h; int n, d; };
static DWORD WINAPI xt_worker(LPVOID q) { struct xt_ctx *x = q; acc6 o[3] AL4; x->n = x->cp(x->h, o, 3); x->d = x->de(x->h); return 0; }
TEST(gap11_acc_other_thread_can_copy_and_destroy) {
    AC_NEED();
    acc6 a[3] AL4; ac_fill(a, 3);
    HANDLE h = cr(a, 3);
    CHECK(h != NULL); if (!h) return;
    /* HMValidateHandle(h, 8) has no thread test: a handle made on thread A is usable from thread B of the process */
    struct xt_ctx c = { cp, de, h, 0, 0 };
    HANDLE th = CreateThread(NULL, 0, xt_worker, &c, 0, NULL);
    if (!th) { de(h); t_skip("no thread"); return; }
    WaitForSingleObject(th, 5000); CloseHandle(th);
    EXPECT_EQ(c.n, 3); EXPECT_EQ(c.d, 1);
    SET_ERR(G11_SENT);
    G11_EQ(de(h), 0);
}

struct mk_ctx { NtUserCreateAcceleratorTable_pfn cr; HANDLE h; };
static DWORD WINAPI mk_worker(LPVOID q) { struct mk_ctx *x = q; acc6 a[2] AL4; ac_fill(a, 2); x->h = x->cr(a, 2); return 0; }
TEST(gap11_acc_table_survives_creating_thread_exit) {
    AC_NEED();
    struct mk_ctx c = { cr, NULL };
    HANDLE th = CreateThread(NULL, 0, mk_worker, &c, 0, NULL);
    if (!th) { t_skip("no thread"); return; }
    WaitForSingleObject(th, 5000); CloseHandle(th);
    CHECK(c.h != NULL); if (!c.h) return;
    int n = cp(c.h, NULL, 0);
    t_note("table made on an exited thread: count query %d (2 = process-owned and still alive)", n);
    if (n) { EXPECT_EQ(n, 2); de(c.h); }
}

TEST(gap11_acc_user32_a_and_w_entry_points_equal) {
    AC_NEED();
    FARPROC fa = t_proc_in("user32.dll", "CreateAcceleratorTableA"), fw = t_proc_in("user32.dll", "CreateAcceleratorTableW");
    if (!fa || !fw) { t_skip("no user32 exports"); return; }
    HACCEL (WINAPI *ca)(LPACCEL, int) = (void *)fa, (WINAPI *cw)(LPACCEL, int) = (void *)fw;
    acc6 a[3] AL4; ac_fill(a, 3);
    HACCEL h1 = ca((LPACCEL)a, 3), h2 = cw((LPACCEL)a, 3);
    CHECK(h1 && h2);
    if (h1 && h2) {
        acc6 o1[3] AL4, o2[3] AL4;
        G11_EQ(cp(h1, o1, 3), 3); G11_EQ(cp(h2, o2, 3), 3);
        CHECK(!memcmp(o1, o2, sizeof o1));                   /* neither variant converts anything */
    }
    if (h1) de(h1);
    if (h2) de(h2);
}

/* ---- TranslateAccelerator drives the stored table  ---- */
TEST(gap11_acc_translate_key_not_in_table_is_zero) {
    AC_NEED(); REQUIRE(U(TranslateAcceleratorW));
    G11_NEED_WND(w);
    acc6 a[2] AL4; ac_fill(a, 2);
    a[0].key = 0x51; a[1].key = 0x52;
    HACCEL h = (HACCEL)cr(a, 2);
    CHECK(h != NULL);
    if (h) {
        MSG m; memset(&m, 0, sizeof m); m.hwnd = w; m.message = WM_KEYDOWN; m.wParam = 0x5A; m.lParam = 1;
        InterlockedExchange(&g11_commands, 0);
        CHECK(U(TranslateAcceleratorW)(w, h, &m) == 0);        /* 'Z' is not in the table */
        EXPECT_EQ(g11_commands, 0);
        de(h);
    }
    g11_kill(w);
}

TEST(gap11_acc_translate_hit_reports_command) {
    AC_NEED(); REQUIRE(U(TranslateAcceleratorW));
    G11_NEED_WND(w);
    acc6 a[3] AL4; ac_fill(a, 3);
    a[0].key = 0x51; a[0].cmd = 111; a[1].key = 0x57; a[1].cmd = 222; a[2].key = 0x45; a[2].cmd = 333;   /* Q W E */
    HACCEL h = (HACCEL)cr(a, 3);
    CHECK(h != NULL); if (!h) { g11_kill(w); return; }
    for (int i = 0; i < 3; i++) {
        MSG m; memset(&m, 0, sizeof m); m.hwnd = w; m.message = WM_KEYDOWN; m.wParam = a[i].key; m.lParam = 1;
        InterlockedExchange(&g11_commands, 0); g11_last_cmd_w = 0;
        int r = U(TranslateAcceleratorW)(w, h, &m);
        /* needs the Ctrl/Shift/Alt keys to be up on the interactive desktop, so a miss is only a note */
        if (r) { EXPECT_EQ(LOWORD(g11_last_cmd_w), a[i].cmd); EXPECT_EQ(HIWORD(g11_last_cmd_w), 1); }
        else t_note("entry %d (key 0x%x) not matched, modifier keys down?", i, a[i].key);
    }
    de(h);
    g11_kill(w);
}
