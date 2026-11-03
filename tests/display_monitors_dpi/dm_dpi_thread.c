/* DPI awareness contexts: validation helpers, the per-thread context and its mirror in the TEB, and the kernel's view of it.
 *
 * user32!ValidateDpiAwarenessContextEx maps the handle-like constants to packed values: -1 -> 0x6010, -2 -> (systemDpi<<8)|0x11,
 * -3 -> 0x12, -4 -> 0x22, -5 -> 0x40006010; anything else must be one of those packed forms (mask 0x5fffffff / 0x5ffe00ff).
 * The low nibble is the awareness (0 unaware, 1 system, 2 per monitor).
 * SetThreadDpiAwarenessContext stores the packed value in TEB.Win32ClientInfo+0xe8 (0 when bit 31 is set, i.e. "use the process
 * default"), sets the changed flag +0xec, returns the previous value (process default | 0x80000000 when there was no override)
 * and caches that returned token at +0x10c. The kernel (win32kbase!CaptureAndValidateUserModeDpiAwarenessContext) re-reads
 * +0xe8 whenever +0xec is non-zero, validates it with IsValidKernelDpiAwarenessContext, zeroes +0xe8 if it is bad and clears +0xec.
 */
#include "dm.h"

#define NEED_DPI_API() do { NEED_GUI(); REQUIRE(U(SetThreadDpiAwarenessContext) && U(GetThreadDpiAwarenessContext) && U(IsValidDpiAwarenessContext) && U(AreDpiAwarenessContextsEqual) && U(GetAwarenessFromDpiAwarenessContext) && U(GetDpiFromDpiAwarenessContext)); } while (0)

static unsigned sys_dpi(void) {
    if (!t_is_wine() && UN(NtUserGetDpiForCurrentProcess)) return UN(NtUserGetDpiForCurrentProcess)() & 0xffff;
    return U(GetDpiForSystem) ? U(GetDpiForSystem)() : 96;
}
static unsigned kernel_ctx(void) { return UN(NtUserGetProcessDpiAwarenessContext) ? UN(NtUserGetProcessDpiAwarenessContext)(NULL) : 0; }

struct ctxrow { DPI_AWARENESS_CONTEXT c; const char *name; int awareness; unsigned raw; int has_raw; };
#define ROWS_ALL \
    { DPI_AWARENESS_CONTEXT_UNAWARE,           "UNAWARE",           0, DM_RAW_UNAWARE,   1 }, \
    { DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,      "SYSTEM_AWARE",      1, 0,                0 }, \
    { DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, "PER_MONITOR_AWARE", 2, DM_RAW_PMONITOR,  1 }, \
    { DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, "PMV2",           2, DM_RAW_PMONITOR2, 1 }, \
    { DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED, "UNAWARE_GDISCALED", 0, DM_RAW_GDISCALED, 1 }

TEST(display_monitors_dpi_ctx_pseudo_constants_have_documented_values) {
    CHECK((LONG_PTR)DPI_AWARENESS_CONTEXT_UNAWARE == -1);
    CHECK((LONG_PTR)DPI_AWARENESS_CONTEXT_SYSTEM_AWARE == -2);
    CHECK((LONG_PTR)DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE == -3);
    CHECK((LONG_PTR)DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == -4);
    CHECK((LONG_PTR)DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED == -5);
}

TEST(display_monitors_dpi_ctx_is_valid_accepts_the_five_constants) {
    NEED_DPI_API();
    struct ctxrow rows[] = { ROWS_ALL };
    for (unsigned i = 0; i < 5; i++) CHECKF(U(IsValidDpiAwarenessContext)(rows[i].c), "%s", rows[i].name);
}

TEST(display_monitors_dpi_ctx_is_valid_accepts_packed_values) {
    NEED_DPI_API();
    unsigned ok[] = { 0x6010, 0x40006010, 0x12, 0x22, 0x80006010, 0x20000012, 0x80000022, 0xC0006010, 0x6011, 0x7811, 0x1011 };
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++)
        CHECKF(U(IsValidDpiAwarenessContext)(DM_CTX(ok[i])), "0x%x", ok[i]);       /* masks 0x5fffffff / 0x5ffe00ff in ValidateDpiAwarenessContextEx */
}

TEST(display_monitors_dpi_ctx_is_valid_rejects_garbage_with_invalid_parameter) {
    NEED_DPI_API();
    ULONG_PTR bad[] = { 0, (ULONG_PTR)-6, (ULONG_PTR)-7, (ULONG_PTR)-100, 0x1234, 0x16010, 0x80000000, 0xffffffff, 0x23, 0x10, 0x6012, 0x42, 0x32, 0x100 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        CHECKF(!U(IsValidDpiAwarenessContext)((DPI_AWARENESS_CONTEXT)bad[i]), "0x%llx", (unsigned long long)bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(display_monitors_dpi_ctx_awareness_from_context_table) {
    NEED_DPI_API();
    struct ctxrow rows[] = { ROWS_ALL };
    for (unsigned i = 0; i < 5; i++)
        CHECKF((int)U(GetAwarenessFromDpiAwarenessContext)(rows[i].c) == rows[i].awareness, "%s -> %d", rows[i].name, (int)U(GetAwarenessFromDpiAwarenessContext)(rows[i].c));
}

TEST(display_monitors_dpi_ctx_awareness_from_invalid_context_is_invalid) {
    NEED_DPI_API();
    ULONG_PTR bad[] = { 0, (ULONG_PTR)-6, 0x1234, 0xffffffff };
    for (unsigned i = 0; i < 4; i++) CHECKF((int)U(GetAwarenessFromDpiAwarenessContext)((DPI_AWARENESS_CONTEXT)bad[i]) == (int)DPI_AWARENESS_INVALID, "0x%llx", (unsigned long long)bad[i]);
}

TEST(display_monitors_dpi_ctx_awareness_from_packed_values) {
    NEED_DPI_API();
    CHECK((int)U(GetAwarenessFromDpiAwarenessContext)(DM_CTX(0x6010)) == DPI_AWARENESS_UNAWARE);
    CHECK((int)U(GetAwarenessFromDpiAwarenessContext)(DM_CTX(0x40006010)) == DPI_AWARENESS_UNAWARE);
    CHECK((int)U(GetAwarenessFromDpiAwarenessContext)(DM_CTX(0x7811)) == DPI_AWARENESS_SYSTEM_AWARE);
    CHECK((int)U(GetAwarenessFromDpiAwarenessContext)(DM_CTX(0x12)) == DPI_AWARENESS_PER_MONITOR_AWARE);
    CHECK((int)U(GetAwarenessFromDpiAwarenessContext)(DM_CTX(0x22)) == DPI_AWARENESS_PER_MONITOR_AWARE);
    CHECK((int)U(GetAwarenessFromDpiAwarenessContext)(DM_CTX(0x80000022)) == DPI_AWARENESS_PER_MONITOR_AWARE);
}

TEST(display_monitors_dpi_ctx_dpi_from_context) {
    NEED_DPI_API();
    CHECK(U(GetDpiFromDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE) == 96);               /* (0x6010 >> 8) & 0x1ff */
    CHECK(U(GetDpiFromDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) == 96);
    CHECK(U(GetDpiFromDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) == 0);
    CHECK(U(GetDpiFromDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == 0);
    CHECKF(U(GetDpiFromDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) == (sys_dpi() & 0x1ff), "system aware dpi %u vs process %u", U(GetDpiFromDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE), sys_dpi());
    CHECK(U(GetDpiFromDpiAwarenessContext)(DM_CTX(0x7811)) == 0x78);
}

TEST(display_monitors_dpi_ctx_dpi_from_invalid_context_is_zero) {
    NEED_DPI_API();
    ULONG_PTR bad[] = { 0, (ULONG_PTR)-6, 0x1234 };
    for (unsigned i = 0; i < 3; i++) CHECK(U(GetDpiFromDpiAwarenessContext)((DPI_AWARENESS_CONTEXT)bad[i]) == 0);
}

TEST(display_monitors_dpi_ctx_are_equal_table) {
    NEED_DPI_API();
    struct { ULONG_PTR a, b; int eq; } t[] = {
        { (ULONG_PTR)-1, (ULONG_PTR)-1, 1 }, { (ULONG_PTR)-3, (ULONG_PTR)-3, 1 }, { (ULONG_PTR)-4, (ULONG_PTR)-4, 1 }, { (ULONG_PTR)-5, (ULONG_PTR)-5, 1 }, { (ULONG_PTR)-2, (ULONG_PTR)-2, 1 },
        { (ULONG_PTR)-1, (ULONG_PTR)-5, 0 },       /* 0x6010 vs 0x40006010: bit 30 is significant */
        { (ULONG_PTR)-3, (ULONG_PTR)-4, 0 }, { (ULONG_PTR)-1, (ULONG_PTR)-3, 0 }, { (ULONG_PTR)-2, (ULONG_PTR)-3, 0 }, { (ULONG_PTR)-2, (ULONG_PTR)-1, 0 },
        { (ULONG_PTR)-1, 0x6010, 1 }, { (ULONG_PTR)-3, 0x12, 1 }, { (ULONG_PTR)-4, 0x22, 1 }, { (ULONG_PTR)-5, 0x40006010, 1 },
        { 0x80006010, 0x6010, 1 }, { 0x80000022, (ULONG_PTR)-4, 1 },                       /* bit 31 is masked off by 0x7fffffff */
        { 0, 0, 0 }, { 0, (ULONG_PTR)-1, 0 }, { (ULONG_PTR)-1, 0, 0 }, { (ULONG_PTR)-6, (ULONG_PTR)-6, 0 }, { 0x1234, 0x1234, 0 },   /* an invalid operand is never equal, even to itself */
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++)
        CHECKF((U(AreDpiAwarenessContextsEqual)((DPI_AWARENESS_CONTEXT)t[i].a, (DPI_AWARENESS_CONTEXT)t[i].b) != 0) == t[i].eq, "row %u (0x%llx,0x%llx)", i, (unsigned long long)t[i].a, (unsigned long long)t[i].b);
}

TEST(display_monitors_dpi_ctx_set_get_round_trip_for_each_constant) {
    NEED_DPI_API();
    DM_CTX_GUARD();
    struct ctxrow rows[] = { ROWS_ALL };
    for (unsigned i = 0; i < 5; i++) {
        DPI_AWARENESS_CONTEXT tok = U(SetThreadDpiAwarenessContext)(rows[i].c);
        CHECKF(tok != NULL, "%s: Set returned NULL (error %lu)", rows[i].name, GetLastError());
        unsigned cur = dm_cur_ctx();
        unsigned want = rows[i].has_raw ? rows[i].raw : DM_RAW_SYSTEM(sys_dpi());
        CHECKF(cur == want, "%s: Get 0x%x want 0x%x", rows[i].name, cur, want);
        CHECK(U(AreDpiAwarenessContextsEqual)(U(GetThreadDpiAwarenessContext)(), rows[i].c));
        CHECK((int)U(GetAwarenessFromDpiAwarenessContext)(U(GetThreadDpiAwarenessContext)()) == rows[i].awareness);
    }
}

TEST(display_monitors_dpi_ctx_set_returns_previous_explicit_context) {
    NEED_DPI_API();
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    CHECK(dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) == DM_RAW_UNAWARE);
    CHECK(dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == DM_RAW_PMONITOR);
    CHECK(dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) == DM_RAW_PMONITOR2);
    CHECK(dm_set_ctx(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) == DM_RAW_GDISCALED);
    CHECK(dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE) == DM_RAW_SYSTEM(sys_dpi()));
}

TEST(display_monitors_dpi_ctx_set_same_context_returns_itself) {
    NEED_DPI_API();
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    for (int i = 0; i < 3; i++) CHECK(dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == DM_RAW_PMONITOR2);
    CHECK(dm_cur_ctx() == DM_RAW_PMONITOR2);
}

struct fresh { unsigned def, tok, after_get, ci_ctx, ci_after; int ok; DWORD tid; unsigned cur_after_restore; };
static DWORD WINAPI fresh_thread(LPVOID p) {
    struct fresh *f = (struct fresh *)p;
    unsigned long v = 0;
    f->def = dm_cur_ctx();                                   /* first user32 call on this thread: NtUserGetThreadState(0xe) converts it */
    f->tok = dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    f->after_get = dm_cur_ctx();
    dm_ci(DM_CI_CTX, &v); f->ci_ctx = v;
    U(SetThreadDpiAwarenessContext)(DM_CTX(f->tok));          /* the token has bit 31: back to "no override" */
    f->cur_after_restore = dm_cur_ctx();
    dm_ci(DM_CI_CTX, &v); f->ci_after = v;
    f->ok = 1;
    return 0;
}
static int run_fresh(struct fresh *f) {
    memset(f, 0, sizeof *f);
    HANDLE h = CreateThread(NULL, 0, fresh_thread, f, 0, &f->tid);
    if (!h) return 0;
    WaitForSingleObject(h, 10000); CloseHandle(h);
    return f->ok;
}

TEST(display_monitors_dpi_ctx_fresh_thread_set_returns_default_token_with_bit31) {
    NEED_DPI_API();
    struct fresh f; REQUIRE(run_fresh(&f));
    CHECKF((f.tok & 0x80000000u) != 0, "token 0x%x of a thread without override must carry bit 31", f.tok);     /* gDefaultDpiContext | 0x80000000 */
    CHECKF((f.tok & 0x7fffffffu) == f.def, "token 0x%x vs the thread's default 0x%x", f.tok, f.def);
    CHECK(f.after_get == DM_RAW_PMONITOR2);
}

TEST(display_monitors_dpi_ctx_fresh_thread_restoring_token_clears_the_teb_override) {
    NEED_DPI_API();
    struct fresh f; REQUIRE(run_fresh(&f));
    CHECKF(f.ci_ctx == DM_RAW_PMONITOR2, "TEB ctx word 0x%x while overridden", f.ci_ctx);
    CHECKF(f.ci_after == 0, "TEB ctx word 0x%x after restoring the bit-31 token (SetValidatedThreadDpiAwarenessContext stores 0 for negative values)", f.ci_after);
    CHECKF(f.cur_after_restore == f.def, "default 0x%x restored as 0x%x", f.def, f.cur_after_restore);
}

TEST(display_monitors_dpi_ctx_teb_mirror_tracks_set) {
    NEED_DPI_API();
    if (t_is_wine()) { t_skip("Wine keeps DPI state elsewhere"); return; }
    DM_CTX_GUARD();
    unsigned long v = 0;
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    CHECK(dm_ci(DM_CI_CTX, &v) && v == DM_RAW_PMONITOR);
    DPI_AWARENESS_CONTEXT tok = U(SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CHECK(dm_ci(DM_CI_CTX, &v) && v == DM_RAW_PMONITOR2);
    CHECK(dm_ci(DM_CI_CTXCHG, &v) && v == 1);                           /* "changed" flag for the kernel to re-capture */
    CHECKF(dm_ci(DM_CI_TOKEN, &v) && v == DM_LO(tok), "token cache 0x%lx vs returned 0x%x", v, DM_LO(tok));   /* *(Self+0x90c) = previous value */
}

TEST(display_monitors_dpi_ctx_kernel_captures_and_clears_changed_flag) {
    NEED_DPI_API(); NEED_NT(NtUserGetProcessDpiAwarenessContext);
    if (t_is_wine()) { t_skip("Wine has no TEB client info / kernel capture"); return; }
    DM_CTX_GUARD();
    unsigned long v = 0;
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    kernel_ctx();                                                        /* W32GetCurrentThreadDpiAwarenessContext -> CaptureAndValidate... */
    CHECKF(dm_ci(DM_CI_CTXCHG, &v) && v == 0, "changed flag 0x%lx after a kernel round trip", v);
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);                 /* same value: SetValidatedThreadDpiAwarenessContext leaves the flag alone */
    CHECKF(dm_ci(DM_CI_CTXCHG, &v) && v == 0, "flag raised by a no-op set (0x%lx)", v);
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CHECKF(dm_ci(DM_CI_CTXCHG, &v) && v == 1, "flag 0x%lx after a real change", v);
    kernel_ctx();
    CHECK(dm_ci(DM_CI_CTXCHG, &v) && v == 0);
}

TEST(display_monitors_dpi_ctx_kernel_sees_the_thread_awareness) {
    NEED_DPI_API(); NEED_NT(NtUserGetProcessDpiAwarenessContext);
    DM_CTX_GUARD();
    struct ctxrow rows[] = { ROWS_ALL };
    for (unsigned i = 0; i < 5; i++) {
        dm_set_ctx(rows[i].c);
        unsigned k = kernel_ctx();
        CHECKF((int)(k & 0xf) == rows[i].awareness, "%s: kernel awareness nibble %u in 0x%x", rows[i].name, k & 0xf, k);
        if (rows[i].has_raw && (rows[i].raw & 0xf) != 0)
            CHECKF((k & 0xdfffffffu) == rows[i].raw, "%s: kernel context 0x%x want 0x%x", rows[i].name, k, rows[i].raw);   /* bit 29 is the kernel's mixed-mode hosting marker */
    }
}

TEST(display_monitors_dpi_ctx_kernel_agrees_with_getprocessdpiawareness_internal) {
    NEED_DPI_API(); NEED_NT(GetProcessDpiAwarenessInternal);
    DM_CTX_GUARD();
    DPI_AWARENESS_CONTEXT cs[] = { DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 };
    for (unsigned i = 0; i < 3; i++) {
        dm_set_ctx(cs[i]);
        int a = -5; SetLastError(0);
        BOOL r = UN(GetProcessDpiAwarenessInternal)(GetCurrentProcess(), &a);
        CHECKF(r == TRUE && a == (int)i, "ctx %u: ret %d awareness %d", i, r, a);        /* user32!GetProcessDpiAwarenessInternal maps the kernel nibble 0/1/2 */
    }
}

TEST(display_monitors_dpi_ctx_kernel_zeroes_an_invalid_teb_context) {
    NEED_DPI_API(); NEED_NT(NtUserGetProcessDpiAwarenessContext);
    if (t_is_wine()) { t_skip("Wine has no TEB client info / kernel capture"); return; }
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    kernel_ctx();                                                        /* clear the pending flag first */
    volatile unsigned *ctx = (volatile unsigned *)((char *)k_teb() + TEB_Win32ClientInfo + DM_CI_CTX);
    volatile unsigned *flag = (volatile unsigned *)((char *)k_teb() + TEB_Win32ClientInfo + DM_CI_CTXCHG);
    *ctx = 0x1234; *flag = 1;                                            /* not accepted by IsValidKernelDpiAwarenessContext */
    kernel_ctx();
    unsigned c = *ctx, fl = *flag;
    *ctx = 0; *flag = 1;                                                 /* leave the TEB consistent; the guard re-sets the saved context below */
    kernel_ctx();
    CHECKF(c == 0, "TEB context word 0x%x after the kernel rejected it", c);
    CHECKF(fl == 0, "flag 0x%x after capture", fl);
}

TEST(display_monitors_dpi_ctx_kernel_accepts_valid_packed_teb_context) {
    NEED_DPI_API(); NEED_NT(NtUserGetProcessDpiAwarenessContext);
    if (t_is_wine()) { t_skip("Wine has no TEB client info / kernel capture"); return; }
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    kernel_ctx();
    volatile unsigned *ctx = (volatile unsigned *)((char *)k_teb() + TEB_Win32ClientInfo + DM_CI_CTX);
    volatile unsigned *flag = (volatile unsigned *)((char *)k_teb() + TEB_Win32ClientInfo + DM_CI_CTXCHG);
    *ctx = DM_RAW_PMONITOR2; *flag = 1;
    unsigned k = kernel_ctx();
    unsigned c = *ctx;
    CHECKF((k & 0xf) == 2, "kernel nibble %u after writing 0x22 into the TEB word", k & 0xf);
    CHECK(c == DM_RAW_PMONITOR2);
    *ctx = DM_RAW_UNAWARE; *flag = 1; kernel_ctx();
}

TEST(display_monitors_dpi_ctx_set_invalid_leaves_all_state_untouched) {
    NEED_DPI_API();
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    unsigned long tok0 = 0, ctx0 = 0, tok1, ctx1;
    dm_ci(DM_CI_TOKEN, &tok0); dm_ci(DM_CI_CTX, &ctx0);
    ULONG_PTR bad[] = { 0, (ULONG_PTR)-6, (ULONG_PTR)-100, 0x1234, 0x16010, 0x80000000, 0xffffffff, 0x23, 0x6012 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        DPI_AWARENESS_CONTEXT r = U(SetThreadDpiAwarenessContext)((DPI_AWARENESS_CONTEXT)bad[i]);
        CHECKF(r == NULL, "0x%llx returned %p", (unsigned long long)bad[i], (void *)r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECKF(dm_cur_ctx() == DM_RAW_PMONITOR, "0x%llx changed the context to 0x%x", (unsigned long long)bad[i], dm_cur_ctx());
    }
    dm_ci(DM_CI_TOKEN, &tok1); dm_ci(DM_CI_CTX, &ctx1);
    if (!t_is_wine()) { CHECK(tok1 == tok0); CHECK(ctx1 == ctx0); }
}

TEST(display_monitors_dpi_ctx_set_system_aware_with_foreign_dpi_is_rejected) {
    NEED_DPI_API();
    DM_CTX_GUARD();
    unsigned dpi = sys_dpi();
    if (dpi == 0 || dpi > 0x1fe) { t_skip("process system dpi %u not usable", dpi); return; }
    unsigned other = DM_RAW_SYSTEM(dpi + 1);
    CHECK(U(IsValidDpiAwarenessContext)(DM_CTX(other)));                  /* ValidateDpiAwarenessContextEx(.., false): any dpi packs fine */
    SetLastError(0);
    CHECK(U(SetThreadDpiAwarenessContext)(DM_CTX(other)) == NULL);        /* (.., true): dpi bits must equal GetDpiForCurrentProcess */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(U(SetThreadDpiAwarenessContext)(DM_CTX(DM_RAW_SYSTEM(dpi))) != NULL);
}

TEST(display_monitors_dpi_ctx_set_negative_value_stores_zero) {
    NEED_DPI_API();
    if (t_is_wine()) { t_skip("Wine keeps DPI state elsewhere"); return; }
    DM_CTX_GUARD();
    unsigned long v = 1;
    CHECK(U(SetThreadDpiAwarenessContext)(DM_CTX(0x80000022)) != NULL);
    CHECKF(dm_ci(DM_CI_CTX, &v) && v == 0, "TEB word 0x%lx: (int)param < 0 is stored as 0", v);
}

struct iso { unsigned main_ctx_seen_by_worker; unsigned worker_def; int ok; };
static DWORD WINAPI iso_thread(LPVOID p) {
    struct iso *s = (struct iso *)p;
    s->main_ctx_seen_by_worker = dm_cur_ctx();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);
    s->worker_def = dm_cur_ctx();
    s->ok = 1;
    return 0;
}
TEST(display_monitors_dpi_ctx_context_is_per_thread) {
    NEED_DPI_API();
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct iso s; memset(&s, 0, sizeof s);
    HANDLE h = CreateThread(NULL, 0, iso_thread, &s, 0, NULL); REQUIRE(h);
    WaitForSingleObject(h, 10000); CloseHandle(h);
    CHECK(s.ok);
    CHECKF(s.main_ctx_seen_by_worker != DM_RAW_PMONITOR2 || sys_dpi() == 0, "a new thread inherited the creator's thread override (0x%x)", s.main_ctx_seen_by_worker);
    CHECK(s.worker_def == DM_RAW_GDISCALED);
    CHECK(dm_cur_ctx() == DM_RAW_PMONITOR2);                              /* the worker's Set did not leak back */
}

/* ---- DPI hosting behavior ---------------------------------------------------------------------- */

struct host { unsigned def, prev1, get1, prev0, get0, inv, inv_err, get_after_inv; unsigned long ci_host, ci_chg; int ok; };
static DWORD WINAPI host_thread(LPVOID p) {
    struct host *h = (struct host *)p;
    unsigned long v = 0;
    h->def = (unsigned)U(GetThreadDpiHostingBehavior)();
    h->prev1 = (unsigned)U(SetThreadDpiHostingBehavior)(DPI_HOSTING_BEHAVIOR_MIXED);
    h->get1 = (unsigned)U(GetThreadDpiHostingBehavior)();
    dm_ci(DM_CI_HOST, &v); h->ci_host = v; dm_ci(DM_CI_HOSTCHG, &v); h->ci_chg = v;
    h->prev0 = (unsigned)U(SetThreadDpiHostingBehavior)(DPI_HOSTING_BEHAVIOR_DEFAULT);
    h->get0 = (unsigned)U(GetThreadDpiHostingBehavior)();
    SetLastError(0);
    h->inv = (unsigned)U(SetThreadDpiHostingBehavior)((DPI_HOSTING_BEHAVIOR)2);
    h->inv_err = GetLastError();
    h->get_after_inv = (unsigned)U(GetThreadDpiHostingBehavior)();
    h->ok = 1;
    return 0;
}
static int run_host(struct host *h) {
    memset(h, 0, sizeof *h);
    HANDLE t = CreateThread(NULL, 0, host_thread, h, 0, NULL);
    if (!t) return 0;
    WaitForSingleObject(t, 10000); CloseHandle(t);
    return h->ok;
}
#define NEED_HOST() do { NEED_GUI(); REQUIRE(U(SetThreadDpiHostingBehavior) && U(GetThreadDpiHostingBehavior)); } while (0)

TEST(display_monitors_dpi_host_default_and_mixed_round_trip) {
    NEED_HOST();
    struct host h; REQUIRE(run_host(&h));
    CHECK(h.def == DPI_HOSTING_BEHAVIOR_DEFAULT);
    CHECK(h.prev1 == DPI_HOSTING_BEHAVIOR_DEFAULT);        /* Set returns the previous value */
    CHECK(h.get1 == DPI_HOSTING_BEHAVIOR_MIXED);
    CHECK(h.prev0 == DPI_HOSTING_BEHAVIOR_MIXED);
    CHECK(h.get0 == DPI_HOSTING_BEHAVIOR_DEFAULT);
}

TEST(display_monitors_dpi_host_invalid_value_returns_invalid_and_keeps_state) {
    NEED_HOST();
    struct host h; REQUIRE(run_host(&h));
    CHECK(h.inv == (unsigned)DPI_HOSTING_BEHAVIOR_INVALID);   /* param >= 2 -> 0xffffffff, ERROR_INVALID_PARAMETER */
    CHECK(h.inv_err == ERROR_INVALID_PARAMETER);
    CHECK(h.get_after_inv == DPI_HOSTING_BEHAVIOR_DEFAULT);
}

TEST(display_monitors_dpi_host_teb_mirror) {
    NEED_HOST();
    if (t_is_wine()) { t_skip("Wine keeps hosting behavior elsewhere"); return; }
    struct host h; REQUIRE(run_host(&h));
    CHECKF(h.ci_host == 1, "TEB hosting word 0x%lx while MIXED", h.ci_host);           /* *(Self+0x8f0) */
    CHECKF(h.ci_chg == 1, "TEB hosting changed flag 0x%lx", h.ci_chg);                 /* *(Self+0x8f4) = 1 on change */
}

TEST(display_monitors_dpi_host_invalid_values_table) {
    NEED_HOST();
    DWORD bad[] = { 2, 3, 100, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < 5; i++) {
        SetLastError(0);
        CHECKF((DWORD)U(SetThreadDpiHostingBehavior)((DPI_HOSTING_BEHAVIOR)bad[i]) == 0xffffffffu, "value 0x%lx", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

struct hs { unsigned long f0, f1, host0; unsigned prev; int ok; };
static DWORD WINAPI hs_thread(LPVOID p) {
    struct hs *s = (struct hs *)p;
    U(GetThreadDpiHostingBehavior)();                       /* connects/converts the thread */
    dm_ci(DM_CI_HOSTCHG, &s->f0); dm_ci(DM_CI_HOST, &s->host0);
    s->prev = (unsigned)U(SetThreadDpiHostingBehavior)(DPI_HOSTING_BEHAVIOR_DEFAULT);
    dm_ci(DM_CI_HOSTCHG, &s->f1);
    s->ok = 1;
    return 0;
}
TEST(display_monitors_dpi_host_no_change_does_not_raise_flag) {
    NEED_HOST();
    if (t_is_wine()) { t_skip("Wine keeps hosting behavior elsewhere"); return; }
    struct hs s; memset(&s, 0, sizeof s);
    HANDLE t = CreateThread(NULL, 0, hs_thread, &s, 0, NULL); REQUIRE(t);
    WaitForSingleObject(t, 10000); CloseHandle(t);
    CHECK(s.ok);
    CHECK(s.host0 == 0);
    CHECK(s.prev == DPI_HOSTING_BEHAVIOR_DEFAULT);
    CHECKF(s.f1 == s.f0, "hosting changed flag went from %lu to %lu on a no-op set", s.f0, s.f1);
}
