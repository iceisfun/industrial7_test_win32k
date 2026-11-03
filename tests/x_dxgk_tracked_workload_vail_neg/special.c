/* x_dxgk_tracked_workload_vail_neg / special: the three thunks that read their arguments, the privilege-dependent
 * display-port thunk, the Vail entry points and whole-family properties.
 *
 *   NtDxgkDispMgrOperation: the first argument points to a block of at least 32 bytes; the 64-bit value at byte
 *     offset 8 of it is an NT handle. A null, closed, or out-of-range handle answers STATUS_INVALID_HANDLE, a handle
 *     of any live object (event, mutex, thread, process, pseudo handles, ...) STATUS_OBJECT_TYPE_MISMATCH, because
 *     none of them is a display-manager object. Nothing else of the block, and no other argument, matters.
 *   NtDxgkRegisterVailProcess: a first argument that points to at least 16 readable bytes answers one fixed
 *     graphics status whatever the bytes hold; anything shorter or unreadable answers STATUS_INVALID_PARAMETER.
 *     It never succeeds, for an administrator or for an unprivileged token.
 *   NtDxgkVailPromoteCompositionSurface: first argument 0 or second argument 0 answers STATUS_INVALID_PARAMETER;
 *     a second argument that does not have eight readable bytes answers STATUS_ACCESS_VIOLATION as a returned
 *     status (no exception reaches the caller); otherwise STATUS_ACCESS_DENIED, with the first eight bytes of
 *     the second argument cleared.
 *   NtDxgkDisplayPortOperation answers STATUS_ACCESS_DENIED to a token below high integrity or without the
 *     administrators group and STATUS_INVALID_PARAMETER to a high-integrity administrator, for every argument.
 *   NtDxgkVailConnect and NtDxgkVailDisconnect answer STATUS_ACCESS_DENIED to both kinds of token.
 *
 * Determinism: only symbolic status names are printed; no handle or address is printed.
 */
#include "x.h"

static LONG call1(int sc, ULONG_PTR a0, ULONG_PTR a1) { ULONG_PTR a[8] = { a0, a1 }; return x_call(sc, 0, a); }
static int need(int sc) { if (!x_resolve(sc, 0)) { t_skip("%s not available (emulator or missing export)", xsc_tab[sc].nt); return 0; } return 1; }

/* ---- NtDxgkDispMgrOperation -------------------------------------------------------------------------- */
static unsigned char *dm_block(int slot, ULONGLONG handle) {
    unsigned char *p = x_blk(slot, 0x40, 0);
    memcpy(p + 8, &handle, 8);
    return p;
}
static LONG dm(ULONGLONG handle) { return call1(X_DISPMGR, (ULONG_PTR)dm_block(0, handle), 0); }

TEST(x_dispmgr_live_objects_are_type_mismatch) {
    if (!need(X_DISPMGR)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL), mx = CreateMutexA(NULL, FALSE, NULL), sm = CreateSemaphoreA(NULL, 0, 1, NULL);
    HANDLE tm = CreateWaitableTimerA(NULL, FALSE, NULL), sec = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
    HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, GetCurrentThreadId()), pr = x_open_self(PROCESS_ALL_ACCESS);
    HANDLE syn = NULL; DuplicateHandle(GetCurrentProcess(), ev, GetCurrentProcess(), &syn, SYNCHRONIZE, FALSE, 0);
    /* fixture check: the duplicate really is synchronize-only (the event cannot be signalled through it) */
    if (syn) { SetLastError(0); CHECKF(!SetEvent(syn) && GetLastError() == ERROR_ACCESS_DENIED, "the synchronize-only duplicate is not synchronize-only"); }
    HANDLE all[] = { ev, mx, sm, tm, sec, th, pr, syn };
    const char *nm[] = { "event", "mutex", "semaphore", "waitable timer", "section", "thread", "process", "synchronize-only event" };
    for (unsigned i = 0; i < XARR(all); i++) {
        if (!all[i]) { CHECKF(0, "setup failed for %s", nm[i]); continue; }
        X_ST(dm((ULONGLONG)(ULONG_PTR)all[i]), XS_TYPE_MISMATCH, "handle of a %s", nm[i]);
        CloseHandle(all[i]);
    }
}
TEST(x_dispmgr_pseudo_handles_are_type_mismatch) {
    if (!need(X_DISPMGR)) return;
    X_ST(dm((ULONGLONG)-1), XS_TYPE_MISMATCH, "current-process pseudo handle");
    X_ST(dm((ULONGLONG)-2), XS_TYPE_MISMATCH, "current-thread pseudo handle");
}
TEST(x_dispmgr_closed_and_null_handles_are_invalid) {
    if (!need(X_DISPMGR)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    CloseHandle(ev);
    X_ST(dm((ULONGLONG)(ULONG_PTR)ev), XS_INVALID_HANDLE, "closed event handle");
    X_ST(dm(0), XS_INVALID_HANDLE, "null handle");
    X_ST(dm(0x10000000ull), XS_INVALID_HANDLE, "handle far above any in use");
}
TEST(x_dispmgr_handle_low_bits_are_ignored) {
    if (!need(X_DISPMGR)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (unsigned k = 0; k < 4; k++) X_ST(dm((ULONGLONG)(ULONG_PTR)ev + k), XS_TYPE_MISMATCH, "event handle plus %u", k);
    CloseHandle(ev);
}
TEST(x_dispmgr_handle_upper_bits_are_significant) {
    if (!need(X_DISPMGR)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    ULONGLONG h = (ULONGLONG)(ULONG_PTR)ev;
    X_ST(dm(h | (1ull << 32)), XS_INVALID_HANDLE, "bit 32 set");
    X_ST(dm(h | (1ull << 40)), XS_INVALID_HANDLE, "bit 40 set");
    X_ST(dm(h | (1ull << 63)), XS_INVALID_HANDLE, "bit 63 set");
    X_ST(dm(h), XS_TYPE_MISMATCH, "plain handle still resolves");
    CloseHandle(ev);
}
TEST(x_dispmgr_only_offset_8_holds_the_handle) {
    if (!need(X_DISPMGR)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    ULONGLONG h = (ULONGLONG)(ULONG_PTR)ev;
    static const unsigned offs[] = { 0, 4, 16, 24 };
    for (unsigned i = 0; i < XARR(offs); i++) {
        unsigned char *p = x_blk(0, 0x40, 0);
        memcpy(p + offs[i], &h, 8);
        X_ST(call1(X_DISPMGR, (ULONG_PTR)p, 0), XS_INVALID_HANDLE, "handle stored at offset %u instead", offs[i]);
    }
    CloseHandle(ev);
}
TEST(x_dispmgr_other_bytes_and_arguments_are_ignored) {
    if (!need(X_DISPMGR)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    ULONGLONG h = (ULONGLONG)(ULONG_PTR)ev;
    static const int fills[] = { 0x00, 0x41, 0xFF, 0x01 };
    for (unsigned i = 0; i < XARR(fills); i++) {
        unsigned char *p = x_blk(0, 0x40, fills[i]);
        memcpy(p + 8, &h, 8);
        X_ST(call1(X_DISPMGR, (ULONG_PTR)p, 0), XS_TYPE_MISMATCH, "bytes around the handle filled with %02x", fills[i]);
    }
    ULONG_PTR a[8] = { (ULONG_PTR)dm_block(0, h), ~0ull, 0xDEADBEEF, 1, (ULONG_PTR)x_blk(1, 0x40, 0xFF), 5, 6, 7 };
    X_ST(x_call(X_DISPMGR, 0, a), XS_TYPE_MISMATCH, "garbage in arguments 1..7");
    CloseHandle(ev);
}
TEST(x_dispmgr_block_is_not_written_and_alignment_is_free) {
    if (!need(X_DISPMGR)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    unsigned char *p = dm_block(0, (ULONGLONG)(ULONG_PTR)ev), q[0x40];
    for (int i = 0; i < 0x40; i++) if (i < 8 || i >= 16) p[i] = (unsigned char)(0x5A ^ i);
    memcpy(q, p, 0x40);
    X_ST(call1(X_DISPMGR, (ULONG_PTR)p, 0), XS_TYPE_MISMATCH, "patterned block");
    CHECK(!memcmp(p, q, 0x40));
    for (unsigned off = 1; off < 8; off += 2) {
        unsigned char *m = x_blk(2, 0x40 + 8, 0) + 0;      /* 0x48 readable bytes: every misaligned start still has 0x40 */
        ULONGLONG h = (ULONGLONG)(ULONG_PTR)ev;
        memcpy(m + off + 8, &h, 8);
        X_ST(call1(X_DISPMGR, (ULONG_PTR)(m + off), 0), XS_TYPE_MISMATCH, "block misaligned by %u", off);
        X_ST(call1(X_DISPMGR, (ULONG_PTR)x_blk(3, 0x48, 0) + off, 0), XS_INVALID_HANDLE, "zero block misaligned by %u", off);
    }
    CloseHandle(ev);
}
TEST(x_dispmgr_block_threshold_is_32_bytes) {
    if (!need(X_DISPMGR)) return;
    for (unsigned n = 24; n <= 40; n++) {
        unsigned char *p = x_blk(0, n, 0);
        X_ST(call1(X_DISPMGR, (ULONG_PTR)p, 0), n < 32 ? XS_INVALID_PARAMETER : XS_INVALID_HANDLE, "block of %u bytes", n);
    }
}

/* ---- NtDxgkRegisterVailProcess ----------------------------------------------------------------------- */
TEST(x_regvail_any_readable_16_bytes_get_one_status) {
    if (!need(X_REGVAIL)) return;
    static const int fills[] = { 0x00, 0x01, 0x41, 0x7F, 0x80, 0xFF };
    for (unsigned i = 0; i < XARR(fills); i++) {
        X_ST(call1(X_REGVAIL, (ULONG_PTR)x_blk(0, 16, fills[i]), 0), XS_GFX_C01E0011, "16 bytes of %02x", fills[i]);
        X_ST(call1(X_REGVAIL, (ULONG_PTR)x_blk(0, 0x100, fills[i]), 0), XS_GFX_C01E0011, "256 bytes of %02x", fills[i]);
    }
}
TEST(x_regvail_short_or_unreadable_is_invalid_parameter) {
    if (!need(X_REGVAIL)) return;
    for (unsigned n = 1; n < 16; n++) X_ST(call1(X_REGVAIL, (ULONG_PTR)x_blk(0, n, 0), 0), XS_INVALID_PARAMETER, "block of %u bytes", n);
    static const ULONG_PTR bad[] = { 0, 1, 0x10000, 0xFFFF800000000000ull, ~0ull, 0x7FFFFFFF0000ull };
    for (unsigned i = 0; i < XARR(bad); i++) X_ST(call1(X_REGVAIL, bad[i], 0), XS_INVALID_PARAMETER, "pointer kind %u", i);
    X_ST(call1(X_REGVAIL, (ULONG_PTR)x_freed(), 0), XS_INVALID_PARAMETER, "released region");
}
TEST(x_regvail_other_arguments_and_alignment_are_ignored) {
    if (!need(X_REGVAIL)) return;
    ULONG_PTR a[8] = { (ULONG_PTR)x_blk(0, 0x40, 0), ~0ull, 0xDEADBEEF, 1, (ULONG_PTR)x_blk(1, 0x40, 0xFF), 5, 6, 7 };
    X_ST(x_call(X_REGVAIL, 0, a), XS_GFX_C01E0011, "garbage in arguments 1..7");
    for (unsigned off = 1; off < 8; off += 2) X_ST(call1(X_REGVAIL, (ULONG_PTR)x_blk(2, 0x40, 0) + off, 0), XS_GFX_C01E0011, "block misaligned by %u", off);
}
TEST(x_regvail_never_succeeds_and_leaves_the_block_alone) {
    if (!need(X_REGVAIL)) return;
    unsigned char *p = x_blk(0, 0x40, 0), q[0x40];
    for (int i = 0; i < 0x40; i++) p[i] = (unsigned char)(0x33 ^ i);
    memcpy(q, p, 0x40);
    for (int i = 0; i < 8; i++) {
        LONG st = call1(X_REGVAIL, (ULONG_PTR)p, 0);
        CHECKF(st < 0, "registration succeeded (%s)", xs_name(st));
        X_ST(st, XS_GFX_C01E0011, "attempt %d", i);
    }
    CHECK(!memcmp(p, q, 0x40));
}

/* ---- NtDxgkVailPromoteCompositionSurface ------------------------------------------------------------- */
TEST(x_promote_null_arguments_beat_everything) {
    if (!need(X_PROMOTE)) return;
    unsigned char *ok = x_blk(1, 0x40, 0);
    X_ST(call1(X_PROMOTE, 0, 0), XS_INVALID_PARAMETER, "both null");
    X_ST(call1(X_PROMOTE, 0, (ULONG_PTR)ok), XS_INVALID_PARAMETER, "first null, second readable");
    X_ST(call1(X_PROMOTE, (ULONG_PTR)x_blk(0, 0x40, 0), 0), XS_INVALID_PARAMETER, "first readable, second null");
    X_ST(call1(X_PROMOTE, 0, ~0ull), XS_INVALID_PARAMETER, "first null, second all ones");
    X_ST(call1(X_PROMOTE, 0, 1), XS_INVALID_PARAMETER, "first null, second address 1");
}
TEST(x_promote_second_argument_needs_eight_readable_bytes) {
    if (!need(X_PROMOTE)) return;
    for (unsigned n = 1; n <= 16; n++)
        X_ST(call1(X_PROMOTE, 1, (ULONG_PTR)x_blk(1, n, 0)), n < 8 ? XS_ACCESS_VIOLATION : XS_ACCESS_DENIED, "second argument block of %u bytes", n);
}
TEST(x_promote_unreadable_second_argument_returns_access_violation_status) {
    if (!need(X_PROMOTE)) return;
    static const ULONG_PTR bad[] = { 1, 0x10000, 0xFFFF800000000000ull, ~0ull, 0x7FFFFFFF0000ull, 0xDEADBEEF, 0xFFFFFFFF };
    for (unsigned i = 0; i < XARR(bad); i++) X_ST(call1(X_PROMOTE, (ULONG_PTR)x_blk(0, 0x40, 0), bad[i]), XS_ACCESS_VIOLATION, "second argument kind %u", i);
    X_ST(call1(X_PROMOTE, (ULONG_PTR)x_blk(0, 0x40, 0), (ULONG_PTR)x_freed()), XS_ACCESS_VIOLATION, "released region");
}
TEST(x_promote_first_argument_is_never_dereferenced) {
    if (!need(X_PROMOTE)) return;
    static const ULONG_PTR any[] = { 1, 0x10000, 0xFFFF800000000000ull, ~0ull, 0x7FFFFFFF0000ull, 0xDEADBEEF };
    for (unsigned i = 0; i < XARR(any); i++) X_ST(call1(X_PROMOTE, any[i], (ULONG_PTR)x_blk(1, 0x40, 0)), XS_ACCESS_DENIED, "first argument kind %u", i);
}
TEST(x_promote_clears_only_the_first_eight_bytes_of_the_output) {
    if (!need(X_PROMOTE)) return;
    unsigned char *p = x_blk(1, 0x40, 0), q[0x40];
    for (int i = 0; i < 0x40; i++) p[i] = (unsigned char)(0x5A ^ i);
    memcpy(q, p, 0x40);
    X_ST(call1(X_PROMOTE, 1, (ULONG_PTR)p), XS_ACCESS_DENIED, "patterned output");
    for (int i = 0; i < 8; i++) CHECKF(p[i] == 0, "byte %d of the output was not cleared", i);
    CHECK(!memcmp(p + 8, q + 8, 0x38));
}
TEST(x_promote_later_arguments_and_alignment_are_ignored) {
    if (!need(X_PROMOTE)) return;
    ULONG_PTR a[8] = { 1, (ULONG_PTR)x_blk(1, 0x40, 0), ~0ull, 0xDEADBEEF, 7, 0xFFFF800000000000ull, 0, 3 };
    X_ST(x_call(X_PROMOTE, 0, a), XS_ACCESS_DENIED, "garbage in arguments 2..7");
    for (unsigned off = 1; off < 8; off += 2) X_ST(call1(X_PROMOTE, 1, (ULONG_PTR)x_blk(2, 0x40, 0) + off), XS_ACCESS_DENIED, "output misaligned by %u", off);
}

/* ---- NtDxgkDisplayPortOperation, Vail connect/disconnect ---------------------------------------------- */
TEST(x_dport_answer_follows_the_token_class_for_every_argument_shape) {
    if (!need(X_DPORT)) return;
    int p = x_privileged();
    if (p < 0) { t_skip("caller is neither a high-integrity administrator nor a medium-or-lower integrity token"); return; }
    LONG want = p ? XS_INVALID_PARAMETER : XS_ACCESS_DENIED;
    for (int sh = XS_ZERO; sh <= XS_FILL_01; sh++) {
        ULONG_PTR a[8]; x_args((enum xshape)sh, 3, 2, a);
        X_ST(x_call(X_DPORT, 0, a), want, "argument shape %d", sh);
    }
}
TEST(x_dport_gate_is_not_the_argument_check) {
    if (!need(X_DPORT)) return;
    int p = x_privileged();
    if (p < 0) { t_skip("caller is neither a high-integrity administrator nor a medium-or-lower integrity token"); return; }
    /* a caller below the gate learns nothing of its arguments: a good block and a bad pointer answer alike */
    LONG a1 = call1(X_DPORT, (ULONG_PTR)x_blk(0, 0x100, 0), 0), a2 = call1(X_DPORT, ~0ull, ~0ull);
    EXPECT_EQ(a1, a2);
    EXPECT_EQ(a1, p ? XS_INVALID_PARAMETER : XS_ACCESS_DENIED);
}
TEST(x_vail_connect_and_disconnect_are_denied_to_every_caller) {
    if (!need(X_VCONN) || !need(X_VDISC)) return;
    static const int sc[] = { X_VCONN, X_VDISC };
    for (int i = 0; i < 2; i++) {
        X_ST(call1(sc[i], 0, 0), XS_ACCESS_DENIED, "%s(0, 0)", xsc_tab[sc[i]].nt);
        X_ST(call1(sc[i], (ULONG_PTR)x_blk(0, 0x100, 0), (ULONG_PTR)x_blk(1, 0x100, 0)), XS_ACCESS_DENIED, "%s(blocks)", xsc_tab[sc[i]].nt);
        X_ST(call1(sc[i], ~0ull, ~0ull), XS_ACCESS_DENIED, "%s(all ones)", xsc_tab[sc[i]].nt);
    }
}
TEST(x_vail_connect_denial_does_not_depend_on_the_handle_kinds) {
    if (!need(X_VCONN)) return;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL), pr = x_open_self(PROCESS_ALL_ACCESS);
    HANDLE all[] = { ev, pr, GetCurrentProcess(), GetCurrentThread(), (HANDLE)(ULONG_PTR)-1, NULL };
    for (unsigned i = 0; i < XARR(all); i++) {
        X_ST(call1(X_VCONN, (ULONG_PTR)all[i], 0), XS_ACCESS_DENIED, "connect, handle kind %u in argument 0", i);
        X_ST(call1(X_VDISC, 0, (ULONG_PTR)all[i]), XS_ACCESS_DENIED, "disconnect, handle kind %u in argument 1", i);
    }
    CloseHandle(ev); CloseHandle(pr);
}
TEST(x_vail_no_entry_point_ever_succeeds) {
    static const int vail[] = { X_REGVAIL, X_VCONN, X_VDISC, X_PROMOTE };
    for (unsigned i = 0; i < XARR(vail); i++) {
        if (!need(vail[i])) return;
        for (int sh = XS_ZERO; sh <= XS_FILL_01; sh++) {
            ULONG_PTR a[8]; x_args((enum xshape)sh, 2, 4, a);
            LONG st = x_call(vail[i], 0, a);
            CHECKF(st < 0, "%s succeeded for shape %d (%s)", xsc_tab[vail[i]].nt, sh, xs_name(st));
        }
    }
}

/* ---- the family as a whole -------------------------------------------------------------------------- */
TEST(x_family_exports_exist_in_win32u_and_gdi32) {
    if (t_is_wine()) { t_skip("emulator: no win32k syscall stubs"); return; }
    x_prep();
    for (int i = 0; i < X_COUNT; i++) {
        CHECKF(x_nt8(xsc_tab[i].nt) != NULL, "win32u!%s missing", xsc_tab[i].nt);
        CHECKF(x_gdi8(xsc_tab[i].gdi) != NULL, "gdi32!%s missing", xsc_tab[i].gdi);
    }
}
TEST(x_family_service_numbers_are_distinct_and_in_one_range) {
    if (t_is_wine()) { t_skip("emulator: no win32k syscall stubs"); return; }
    x_prep();
    long n[X_COUNT];
    for (int i = 0; i < X_COUNT; i++) {
        x_fn8 f = x_nt8(xsc_tab[i].nt);
        n[i] = f ? k_stub_number((FARPROC)f) : -1;
        CHECKF(n[i] >= 4433 && n[i] <= 4452, "%s: service number outside the NtDxgk run", xsc_tab[i].nt);
        for (int j = 0; j < i; j++) CHECKF(n[i] != n[j], "%s and %s share a service number", xsc_tab[i].nt, xsc_tab[j].nt);
    }
    /* the two members of the run that are not part of this family sit at 4438 and 4440 */
    for (int i = 0; i < X_COUNT; i++) CHECK(n[i] != 4438 && n[i] != 4440);
}
TEST(x_family_every_status_is_an_error_status) {
    if (t_is_wine()) { t_skip("emulator: no win32k syscall stubs"); return; }
    x_prep();
    for (int i = 0; i < X_COUNT; i++) for (int sh = XS_ZERO; sh <= XS_FILL_01; sh++) {
        ULONG_PTR a[8]; x_args((enum xshape)sh, 1 + sh % 7, sh % X_NWILD, a);
        LONG st = x_call(i, 0, a);
        CHECKF(((DWORD)st >> 30) == 3, "%s shape %d: %s is not an error-severity status", xsc_tab[i].nt, sh, xs_name(st));
    }
}
TEST(x_family_creates_no_handles_and_no_gui_objects) {
    if (t_is_wine()) { t_skip("emulator: no win32k syscall stubs"); return; }
    x_prep();
    DWORD h0 = 0, h1 = 0;
    GetProcessHandleCount(GetCurrentProcess(), &h0);
    DWORD g0 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), u0 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int round = 0; round < 20; round++)
        for (int i = 0; i < X_COUNT; i++) for (int sh = XS_ZERO; sh <= XS_FILL_01; sh++) {
            ULONG_PTR a[8]; x_args((enum xshape)sh, 1 + round % 7, round % X_NWILD, a);
            x_call(i, 0, a);
        }
    GetProcessHandleCount(GetCurrentProcess(), &h1);
    EXPECT_EQ(h1, h0);
    EXPECT_EQ(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), g0);
    EXPECT_EQ(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), u0);
}
CHILD(x_dxgk_family_child) {
    int fails = 0;
    x_prep();
    for (int i = 0; i < X_COUNT; i++) {
        static const enum xshape sh[] = { XS_ZERO, XS_BLOCK0, XS_BLOCKS_ALL, XS_WILD_ALL, XS_FILL_FF };
        for (unsigned k = 0; k < XARR(sh); k++) {
            LONG want; const char *r; ULONG_PTR a[8];
            if (!x_model(i, sh[k], &want, &r)) continue;
            x_args(sh[k], 0, 2, a);
            LONG st = x_call(i, 0, a);
            if (st != want) { fails++; printf("  child: %s shape %u got %s want %s\n", xsc_tab[i].nt, k, xs_name(st), xs_name(want)); }
        }
    }
    return fails;
}
TEST(x_family_answers_the_same_in_a_child_process) {
    if (t_is_wine()) { t_skip("emulator: no win32k syscall stubs"); return; }
    DWORD rc = t_run_child("x_dxgk_family_child", "", 60000);
    EXPECT_EQ(rc, 0);
}
