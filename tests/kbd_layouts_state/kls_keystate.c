/* Per-thread key state: NtUserGetKeyState / GetAsyncKeyState / GetKeyboardState / SetKeyboardState
 * and the key-state caches the kernel publishes in _CLIENTINFO.
 *
 * win32kbase!_GetKeyState: result = down ? 0xFF80 : 0, | toggle (bit 1 of the 2-bit cell); vk >= 0x100
 *   sets ERROR_INVALID_PARAMETER and returns 0. Cells come from pti->pq->afKeyState (queue + 0xE4,
 *   2 bits per vk: bit 0 down, bit 1 toggle).
 * win32kbase!NtUserGetKeyState epilogue: CI+0x70 = the shared-info key-state generation, CI+0x74 = first 8 bytes of afKeyState.
 * win32kbase!_GetAsyncKeyState: down = 0x8000, bit 0 = "pressed since last call" (consumed on read),
 *   vk >= 0x100 -> ERROR_INVALID_PARAMETER. NtUserGetAsyncKeyState epilogue: CI+0x7c = the shared-info async generation,
 *   CI+0x80 / CI+0x88 = first 8 bytes of gafAsyncKeyState / gafAsyncKeyStateRecentDown.
 * win32kbase!NtUserGetKeyboardState: 256 bytes, 0x80 when down, |1 when toggled.
 * win32kfull!_SetKeyboardState: for each vk down = byte & 0x80, toggle = byte & 1 into the calling
 *   thread's afKeyState, then increments the shared-info key-state generation (the client cache generation). The async key table is not touched.
 * user32!GetKeyState / GetAsyncKeyState answer vk < 0x20 from the CI caches when the generation matches.
 */
#include "kls.h"

#define NEED_KS() KLS_NEED(); REQUIRE(U(GetKeyState)); REQUIRE(U(GetAsyncKeyState)); REQUIRE(U(GetKeyboardState)); \
    REQUIRE(U(SetKeyboardState)); REQUIRE(UN(NtUserGetKeyState)); REQUIRE(UN(NtUserGetAsyncKeyState)); \
    REQUIRE(UN(NtUserGetKeyboardState)); REQUIRE(UN(NtUserSetKeyboardState)); kls_cur();

static int ks_isgood(SHORT s) { return s == 0 || s == 1 || s == (SHORT)0xFF80 || s == (SHORT)0xFF81; }
static int as_isgood(SHORT s) { return s == 0 || s == 1 || s == (SHORT)0x8000 || s == (SHORT)0x8001; }
static SHORT ks_expect(BYTE b) { return (SHORT)((KS_DOWN(b) ? 0xFF80 : 0) | (KS_TOGGLE(b) ? 1 : 0)); }
static unsigned long long pack32(const BYTE st[256]) {
    unsigned long long v = 0;
    for (int vk = 0; vk < 32; vk++) {
        if (KS_DOWN(st[vk])) v |= 1ull << (2 * vk);
        if (KS_TOGGLE(st[vk])) v |= 1ull << (2 * vk + 1);
    }
    return v;
}
static void pattern(BYTE st[256]) {
    for (int vk = 0; vk < 256; vk++) st[vk] = (BYTE)((vk % 3 == 0 ? 0x80 : 0) | (vk % 5 < 2 ? 1 : 0));
}

/* ==================================================================== GetKeyboardState */

TEST(kbd_layouts_state_ks_getkeyboardstate_shape) {
    NEED_KS();
    BYTE st[260]; memset(st, 0xCC, sizeof st);
    CHECK(UN(NtUserGetKeyboardState)(st) == 1);
    for (int i = 0; i < 256; i++) CHECKF(st[i] == 0 || st[i] == 1 || st[i] == 0x80 || st[i] == 0x81, "vk %x = %02x", i, st[i]);
    for (int i = 256; i < 260; i++) CHECK(st[i] == 0xCC);   /* exactly 256 bytes are written */
}

TEST(kbd_layouts_state_ks_getkeyboardstate_user_wrapper_matches_nt) {
    NEED_KS();
    BYTE a[256], b[256];
    for (int attempt = 0; attempt < 3; attempt++) {
        CHECK(U(GetKeyboardState)(a));
        CHECK(UN(NtUserGetKeyboardState)(b));
        if (!memcmp(a, b, 256)) break;
        if (attempt == 2) CHECK(!memcmp(a, b, 256));
    }
}

TEST(kbd_layouts_state_ks_getkeyboardstate_overwrites_whole_buffer) {
    NEED_KS();
    BYTE st[256]; memset(st, 0xFF, 256);
    CHECK(UN(NtUserGetKeyboardState)(st) == 1);
    /* every cell is rewritten ( first), none keeps the 0xFF pre-fill */
    for (int i = 0; i < 256; i++) CHECKF(st[i] != 0xFF && (st[i] & 0x7E) == 0, "vk %x = %02x", i, st[i]);
}

/* ========================================================================== GetKeyState */

TEST(kbd_layouts_state_ks_getkeystate_result_shape_all_vk) {
    NEED_KS();
    for (int vk = 0; vk < 256; vk++) {
        SHORT a = UN(NtUserGetKeyState)(vk), b = U(GetKeyState)(vk);
        CHECKF(ks_isgood(a), "Nt vk %x = %04x", vk, (unsigned short)a);
        CHECKF(ks_isgood(b), "user vk %x = %04x", vk, (unsigned short)b);
    }
}

TEST(kbd_layouts_state_ks_getkeystate_matches_getkeyboardstate_all_vk) {
    NEED_KS();
    /* both read the calling thread's afKeyState and share the same foreground-access gate */
    for (int attempt = 0; attempt < 3; attempt++) {
        BYTE st[256]; int bad = 0, firstbad = -1;
        UN(NtUserGetKeyboardState)(st);
        for (int vk = 0; vk < 256; vk++) {
            SHORT s = UN(NtUserGetKeyState)(vk);
            if (s != ks_expect(st[vk])) { bad++; if (firstbad < 0) firstbad = vk; }
        }
        if (!bad) { CHECK(1); return; }
        if (attempt == 2) CHECKF(!bad, "%d vks differ, first %x", bad, firstbad);
    }
}

TEST(kbd_layouts_state_ks_getkeystate_invalid_vk) {
    NEED_KS();
    static const int v[] = { 0x100, 0x200, 0x1000, 0x10000, 0x7fff0000 };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        SET_ERR(0);
        SHORT r = UN(NtUserGetKeyState)(v[i]);
        CHECKF(r == 0, "vk %x -> %04x", v[i], (unsigned short)r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(kbd_layouts_state_ks_getkeystate_valid_vk_does_not_set_error) {
    NEED_KS();
    for (int vk = 0; vk < 0x20; vk++) {
        SET_ERR(0x4242);
        UN(NtUserGetKeyState)(vk);
        EXPECT_ERR(0x4242);
    }
}

TEST(kbd_layouts_state_ks_toggle_keys_agree_between_apis) {
    NEED_KS();
    static const int tk[] = { VK_CAPITAL, VK_NUMLOCK, VK_SCROLL };
    BYTE st[256]; UN(NtUserGetKeyboardState)(st);
    for (unsigned i = 0; i < 3; i++) {
        SHORT s = U(GetKeyState)(tk[i]);
        CHECKF((s & 1) == (st[tk[i]] & 1), "vk %x: GetKeyState toggle %d, GetKeyboardState %02x", tk[i], s & 1, st[tk[i]]);
        t_note("vk %x toggle=%d down=%d", tk[i], s & 1, (s & 0x8000) != 0);
    }
}

/* ===================================================================== GetAsyncKeyState */

TEST(kbd_layouts_state_ks_getasynckeystate_result_shape_all_vk) {
    NEED_KS();
    for (int vk = 0; vk < 256; vk++) {
        SHORT a = UN(NtUserGetAsyncKeyState)(vk);
        CHECKF(as_isgood(a), "Nt vk %x = %04x", vk, (unsigned short)a);
    }
}

/* A caller with a restricted token (the unprivileged pass) is refused by NtUserGetAsyncKeyState with ERROR_ACCESS_DENIED before the
 * argument check, and its async cache is never published. Detect that with an out-of-range vk. */
static int ks_async_denied(void) {
    SET_ERR(0);
    UN(NtUserGetAsyncKeyState)(0x100);
    return GetLastError() == ERROR_ACCESS_DENIED;
}
TEST(kbd_layouts_state_ks_getasynckeystate_invalid_vk) {
    NEED_KS();
    if (ks_async_denied()) { t_skip("GetAsyncKeyState is refused with ERROR_ACCESS_DENIED for this unprivileged token"); return; }
    static const int v[] = { 0x100, 0x400, 0x8000, 0x10000, 0x7fff0000 };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        SET_ERR(0);
        SHORT r = UN(NtUserGetAsyncKeyState)(v[i]);
        CHECKF(r == 0, "vk %x -> %04x", v[i], (unsigned short)r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* _GetAsyncKeyState: vKey >= 0x100 */
    }
}

TEST(kbd_layouts_state_ks_getasynckeystate_recent_down_is_consumed) {
    NEED_KS();
    /* bit 0 ("pressed since the last call") is cleared by the read that reports it */
    for (int vk = 0; vk < 256; vk++) UN(NtUserGetAsyncKeyState)(vk);
    /* A physical key press between the two sweeps legitimately sets the bit again, so this is a
     * soft observation: only a wholesale failure (most vks) is a hard error. */
    int again = 0;
    for (int vk = 0; vk < 256; vk++) {
        SHORT s = UN(NtUserGetAsyncKeyState)(vk);
        if (s & 1) again++;
    }
    if (again) t_note("%d vks reported recent-down on the second sweep (live input?)", again);
    CHECKF(again < 8, "%d vks still report recent-down", again);
}

TEST(kbd_layouts_state_ks_getasynckeystate_user_matches_nt) {
    NEED_KS();
    unsigned long swap = 0;
    kls_psi_u32(0x7c4, &swap);   /* SM_SWAPBUTTON: user32 flips VK_LBUTTON/VK_RBUTTON before asking */
    for (int vk = 0; vk < 256; vk++) {
        int nt_vk = (vk == 1 || vk == 2) && swap ? vk ^ 3 : vk;
        SHORT u = 0, n = 0; int ok = 0;
        for (int tries = 0; tries < 3 && !ok; tries++) {   /* live input may change a key between the two reads */
            u = U(GetAsyncKeyState)(vk);
            n = UN(NtUserGetAsyncKeyState)(nt_vk);
            ok = (u & 0x8000) == (n & 0x8000);
        }
        CHECKF(ok, "vk %x: user %04x nt %04x", vk, (unsigned short)u, (unsigned short)n);
    }
}

/* ========================================================================= client caches */

TEST(kbd_layouts_state_ks_ci_keystate_cache_published_by_getkeystate) {
    NEED_KS();
    if (t_is_wine()) { t_skip("CLIENTINFO key-state cache is win32k specific"); return; }
    int ok = 0;
    for (int attempt = 0; attempt < 4 && !ok; attempt++) {
        BYTE st[256];
        UN(NtUserGetKeyState)(VK_SHIFT);
        unsigned long cg = 0, sg = 0; unsigned long long bits = 0;
        if (!kls_ci_u32(KLS_CI_KS_GEN, &cg) || !kls_ci_u64(KLS_CI_KS_BITS, &bits) || !kls_psi_u32(KLS_PSI_KS_GEN, &sg)) { t_skip("cannot read CLIENTINFO/SERVERINFO"); return; }
        UN(NtUserGetKeyboardState)(st);
        ok = (cg == sg) && (bits == pack32(st));
        if (!ok && attempt == 3) { CHECKF(cg == sg, "CI gen %lx psi gen %lx", cg, sg); CHECKF(bits == pack32(st), "CI bits %llx state %llx", bits, pack32(st)); }
    }
    if (ok) CHECK(1);
}

TEST(kbd_layouts_state_ks_ci_async_cache_published_by_getasynckeystate) {
    NEED_KS();
    if (ks_async_denied()) { t_skip("GetAsyncKeyState is refused with ERROR_ACCESS_DENIED for this unprivileged token: no cache is published"); return; }
    if (t_is_wine()) { t_skip("CLIENTINFO async cache is win32k specific"); return; }
    int ok = 0;
    for (int attempt = 0; attempt < 4 && !ok; attempt++) {
        UN(NtUserGetAsyncKeyState)(VK_SHIFT);
        unsigned long cg = 0, sg = 0; unsigned long long bits = 0;
        if (!kls_ci_u32(KLS_CI_AS_GEN, &cg) || !kls_ci_u64(KLS_CI_AS_BITS, &bits) || !kls_psi_u32(KLS_PSI_AS_GEN, &sg)) { t_skip("cannot read CLIENTINFO/SERVERINFO"); return; }
        int good = (cg == sg);
        for (int vk = 1; vk < 32 && good; vk++) {
            int down = (bits >> (2 * vk)) & 1;
            if (down != ((UN(NtUserGetAsyncKeyState)(vk) & 0x8000) != 0)) good = 0;
        }
        ok = good;
        if (!ok && attempt == 3) CHECKF(0, "async cache inconsistent: CI gen %lx psi gen %lx bits %llx", cg, sg, bits);
    }
    if (ok) CHECK(1);
}

TEST(kbd_layouts_state_ks_user_getkeystate_agrees_with_syscall_for_low_vks) {
    NEED_KS();
    for (int attempt = 0; attempt < 3; attempt++) {
        int bad = 0;
        for (int vk = 0; vk < 0x20; vk++) {
            SHORT n = UN(NtUserGetKeyState)(vk);   /* refreshes the CI cache ... */
            SHORT u = U(GetKeyState)(vk);           /* ... which user32 then answers from */
            if (n != u) bad++;
        }
        if (!bad) { CHECK(1); return; }
        if (attempt == 2) CHECKF(!bad, "%d low vks differ", bad);
    }
}

TEST(kbd_layouts_state_ks_user_fast_path_reads_ci_cache) {
    NEED_KS();
    if (t_is_wine()) { t_skip("CLIENTINFO layout is win32k specific"); return; }
    /* user32!GetKeyState: vk < 0x20 and CI+0x70 equals the shared-info generation -> answered from the 2-bit cells at CI+0x74. Forge a
     * cell in our own TEB copy to prove the fast path (and so the meaning of the kernel-written fields), then
     * let the next syscall repair it. */
    UN(NtUserGetKeyState)(VK_SHIFT);
    unsigned long cg = 0, sg = 0; unsigned long long bits = 0;
    if (!kls_ci_u32(KLS_CI_KS_GEN, &cg) || !kls_psi_u32(KLS_PSI_KS_GEN, &sg) || !kls_ci_u64(KLS_CI_KS_BITS, &bits) || cg != sg) { t_skip("cache not valid"); return; }
    volatile unsigned long long *cell = (volatile unsigned long long *)((char *)k_teb() + TEB_Win32ClientInfo + KLS_CI_KS_BITS);
    unsigned long long saved = *cell;
    *cell = saved | (3ull << (2 * VK_BACK));            /* VK_BACK: down + toggle */
    SHORT a = U(GetKeyState)(VK_BACK);
    *cell = saved & ~(3ull << (2 * VK_BACK));
    SHORT b = U(GetKeyState)(VK_BACK);
    *cell = saved;
    CHECK(a == (SHORT)0xFF81);
    CHECK(b == 0);
    UN(NtUserGetKeyState)(VK_SHIFT);   /* kernel rewrites the cache */
    unsigned long long after = 0; kls_ci_u64(KLS_CI_KS_BITS, &after);
    CHECK((after & (3ull << (2 * VK_BACK))) == (saved & (3ull << (2 * VK_BACK))));
}

TEST(kbd_layouts_state_ks_user_fast_path_generation_mismatch_goes_to_kernel) {
    NEED_KS();
    if (t_is_wine()) { t_skip("CLIENTINFO layout is win32k specific"); return; }
    UN(NtUserGetKeyState)(VK_SHIFT);
    unsigned long cg = 0, sg = 0; unsigned long long bits = 0;
    if (!kls_ci_u32(KLS_CI_KS_GEN, &cg) || !kls_psi_u32(KLS_PSI_KS_GEN, &sg) || !kls_ci_u64(KLS_CI_KS_BITS, &bits) || cg != sg) { t_skip("cache not valid"); return; }
    volatile unsigned long *gen = (volatile unsigned long *)((char *)k_teb() + TEB_Win32ClientInfo + KLS_CI_KS_GEN);
    volatile unsigned long long *cell = (volatile unsigned long long *)((char *)k_teb() + TEB_Win32ClientInfo + KLS_CI_KS_BITS);
    unsigned long long saved = *cell; unsigned long sg0 = *gen;
    BYTE st[256]; UN(NtUserGetKeyboardState)(st);
    *cell = saved | (3ull << (2 * VK_BACK));
    *gen = sg0 ^ 0x5a5a5a5a;                              /* stale generation: user32 must ask the kernel */
    SHORT s = U(GetKeyState)(VK_BACK);
    CHECKF(s == ks_expect(st[VK_BACK]), "stale cache answered %04x, kernel says %04x", (unsigned short)s, (unsigned short)ks_expect(st[VK_BACK]));
    unsigned long ng = 0; kls_ci_u32(KLS_CI_KS_GEN, &ng);
    CHECK(ng == sg);     /* and the syscall re-published the current generation */
}

/* ================================================================== SetKeyboardState */

static DWORD w_set_roundtrip(void *arg) {
    (void)arg;
    BYTE snap[256], pat[256], got[256];
    UN(NtUserGetKeyboardState)(snap);
    pattern(pat);
    CHECK(UN(NtUserSetKeyboardState)(pat) == 1);
    CHECK(UN(NtUserGetKeyboardState)(got) == 1);
    int gated_bad = 0;
    for (int vk = 0; vk < 256; vk++) {
        BYTE want = (BYTE)((KS_DOWN(pat[vk]) ? 0x80 : 0) | (KS_TOGGLE(pat[vk]) ? 1 : 0));
        if (kls_ungated(vk)) CHECKF(got[vk] == want, "GetKeyboardState vk %x = %02x want %02x", vk, got[vk], want);
        else if (got[vk] != want) gated_bad++;
    }
    if (gated_bad) t_note("%d foreground-gated vks not readable back after SetKeyboardState (gate closed for this process)", gated_bad);
    for (int vk = 0; vk < 256; vk++) {
        SHORT s = UN(NtUserGetKeyState)(vk), u = U(GetKeyState)(vk);
        SHORT want = ks_expect(pat[vk]);
        if (kls_ungated(vk)) { CHECKF(s == want, "Nt GetKeyState vk %x = %04x want %04x", vk, (unsigned short)s, (unsigned short)want); CHECKF(u == want, "user GetKeyState vk %x = %04x", vk, (unsigned short)u); }
    }
    UN(NtUserSetKeyboardState)(snap);
    return 0;
}
TEST(kbd_layouts_state_ks_set_then_get_roundtrip) {
    NEED_KS();
    kls_thread(w_set_roundtrip, NULL);
}

static DWORD w_set_bytes(void *arg) {
    (void)arg;
    BYTE snap[256]; UN(NtUserGetKeyboardState)(snap);
    static const BYTE vals[] = { 0x00, 0x01, 0x80, 0x81, 0x7E, 0x7F, 0xFE, 0xFF, 0x02, 0x40, 0x55, 0xAA };
    static const int keys[] = { VK_LBUTTON, VK_BACK, VK_RETURN, VK_SHIFT, VK_CONTROL, VK_MENU, VK_CAPITAL, 0x1B, VK_LWIN, VK_LSHIFT, VK_RSHIFT, 0xA2, 0xA5, VK_NUMLOCK, VK_SCROLL };
    for (unsigned k = 0; k < sizeof keys / sizeof *keys; k++)
        for (unsigned v = 0; v < sizeof vals; v++) {
            BYTE st[256] = { 0 }; st[keys[k]] = vals[v];
            CHECK(UN(NtUserSetKeyboardState)(st) == 1);
            /* only bit 7 (down) and bit 0 (toggle) are looked at: _SetKeyboardState tests and `b & 1` */
            SHORT want = ks_expect(vals[v]);
            SHORT s = UN(NtUserGetKeyState)(keys[k]);
            CHECKF(s == want, "vk %x byte %02x -> %04x want %04x", keys[k], vals[v], (unsigned short)s, (unsigned short)want);
            BYTE got[256]; UN(NtUserGetKeyboardState)(got);
            CHECKF(got[keys[k]] == ((vals[v] & 0x80) | (vals[v] & 1)), "GetKeyboardState vk %x byte %02x -> %02x", keys[k], vals[v], got[keys[k]]);
            for (int o = 0; o < 0x20; o++) if (o != keys[k]) CHECKF(UN(NtUserGetKeyState)(o) == 0 || kls_ungated(o) == 0, "neighbour %x disturbed", o);
        }
    UN(NtUserSetKeyboardState)(snap);
    return 0;
}
TEST(kbd_layouts_state_ks_set_byte_value_semantics) {
    NEED_KS();
    kls_thread(w_set_bytes, NULL);
}

static DWORD w_set_all(void *arg) {
    (void)arg;
    BYTE snap[256], st[256];
    UN(NtUserGetKeyboardState)(snap);
    memset(st, 0, 256);
    CHECK(UN(NtUserSetKeyboardState)(st));
    for (int vk = 0; vk < 256; vk++) if (kls_ungated(vk)) CHECKF(UN(NtUserGetKeyState)(vk) == 0, "all-zero: vk %x", vk);
    memset(st, 0xFF, 256);
    CHECK(UN(NtUserSetKeyboardState)(st));
    for (int vk = 0; vk < 256; vk++) if (kls_ungated(vk)) CHECKF(UN(NtUserGetKeyState)(vk) == (SHORT)0xFF81, "all-ones: vk %x = %04x", vk, (unsigned short)UN(NtUserGetKeyState)(vk));
    memset(st, 0x80, 256);
    CHECK(UN(NtUserSetKeyboardState)(st));
    for (int vk = 0; vk < 256; vk++) if (kls_ungated(vk)) CHECKF(UN(NtUserGetKeyState)(vk) == (SHORT)0xFF80, "down only: vk %x", vk);
    memset(st, 0x01, 256);
    CHECK(UN(NtUserSetKeyboardState)(st));
    for (int vk = 0; vk < 256; vk++) if (kls_ungated(vk)) CHECKF(UN(NtUserGetKeyState)(vk) == 1, "toggle only: vk %x", vk);
    CHECK(UN(NtUserSetKeyboardState)(snap));
    BYTE back[256]; UN(NtUserGetKeyboardState)(back);
    for (int vk = 0; vk < 256; vk++) if (kls_ungated(vk)) CHECKF(back[vk] == snap[vk], "restore: vk %x %02x vs %02x", vk, back[vk], snap[vk]);
    return 0;
}
TEST(kbd_layouts_state_ks_set_uniform_tables_and_restore) {
    NEED_KS();
    kls_thread(w_set_all, NULL);
}

static DWORD w_set_async(void *arg) {
    (void)arg;
    BYTE snap[256], st[256];
    UN(NtUserGetKeyboardState)(snap);
    SHORT before[256];
    for (int vk = 0; vk < 256; vk++) before[vk] = UN(NtUserGetAsyncKeyState)(vk);
    memset(st, 0xFF, 256);
    CHECK(UN(NtUserSetKeyboardState)(st));
    /* _SetKeyboardState only writes the thread's queue: the system async table is untouched */
    for (int vk = 0; vk < 256; vk++) {
        SHORT now = UN(NtUserGetAsyncKeyState)(vk);
        CHECKF((now & 0x8000) == (before[vk] & 0x8000), "async vk %x changed %04x -> %04x", vk, (unsigned short)before[vk], (unsigned short)now);
    }
    UN(NtUserSetKeyboardState)(snap);
    return 0;
}
TEST(kbd_layouts_state_ks_set_does_not_touch_async_state) {
    NEED_KS();
    kls_thread(w_set_async, NULL);
}

static DWORD w_set_gen(void *arg) {
    (void)arg;
    BYTE snap[256], st[256];
    UN(NtUserGetKeyboardState)(snap);
    unsigned long g0 = 0, g1 = 0, g2 = 0;
    if (!kls_psi_u32(KLS_PSI_KS_GEN, &g0)) { t_skip("SERVERINFO unreadable"); return 0; }
    memset(st, 0, 256);
    UN(NtUserSetKeyboardState)(st);
    kls_psi_u32(KLS_PSI_KS_GEN, &g1);
    UN(NtUserSetKeyboardState)(st);
    kls_psi_u32(KLS_PSI_KS_GEN, &g2);
    /* once per call; other threads' real input could only add to it */
    CHECKF(g1 > g0 && g2 > g1, "generations %lx %lx %lx", g0, g1, g2);
    CHECKF((g1 - g0) == 1 || (g1 - g0) < 64, "step %lu", g1 - g0);
    UN(NtUserSetKeyboardState)(snap);
    return 0;
}
TEST(kbd_layouts_state_ks_set_bumps_cache_generation) {
    NEED_KS();
    if (t_is_wine()) { t_skip("SERVERINFO layout is win32k specific"); return; }
    kls_thread(w_set_gen, NULL);
}

static DWORD w_set_invalidate(void *arg) {
    (void)arg;
    BYTE snap[256], st[256];
    UN(NtUserGetKeyboardState)(snap);
    memset(st, 0, 256);
    UN(NtUserSetKeyboardState)(st);
    UN(NtUserGetKeyState)(VK_SHIFT);                 /* publish the (all-up) cache */
    CHECK(U(GetKeyState)(VK_SHIFT) == 0);             /* answered locally */
    st[VK_SHIFT] = 0x80; st[VK_CAPITAL] = 0x01;
    UN(NtUserSetKeyboardState)(st);                   /* bumps the generation: the cache is stale now */
    CHECK(U(GetKeyState)(VK_SHIFT) == (SHORT)0xFF80);
    CHECK(U(GetKeyState)(VK_CAPITAL) == 1);
    unsigned long long bits = 0;
    if (!t_is_wine() && kls_ci_u64(KLS_CI_KS_BITS, &bits)) CHECK(bits == pack32(st));   /* the syscall refreshed the cache */
    UN(NtUserSetKeyboardState)(snap);
    return 0;
}
TEST(kbd_layouts_state_ks_set_invalidates_client_cache) {
    NEED_KS();
    kls_thread(w_set_invalidate, NULL);
}

static DWORD w_set_pattern_cache(void *arg) {
    (void)arg;
    BYTE snap[256], pat[256];
    UN(NtUserGetKeyboardState)(snap);
    pattern(pat);
    UN(NtUserSetKeyboardState)(pat);
    UN(NtUserGetKeyState)(VK_SHIFT);
    unsigned long long bits = 0; unsigned long cg = 0, sg = 0;
    if (kls_ci_u64(KLS_CI_KS_BITS, &bits) && kls_ci_u32(KLS_CI_KS_GEN, &cg) && kls_psi_u32(KLS_PSI_KS_GEN, &sg)) {
        CHECK(cg == sg);
        /* CI+0x74 is pq->afKeyState[0..7]: 2 bits per vk for vk 0..31 */
        CHECKF(bits == pack32(pat), "cache %llx pattern %llx", bits, pack32(pat));
    }
    for (int vk = 0; vk < 32; vk++) CHECKF(U(GetKeyState)(vk) == ks_expect(pat[vk]), "vk %x", vk);
    UN(NtUserSetKeyboardState)(snap);
    return 0;
}
TEST(kbd_layouts_state_ks_ci_cache_mirrors_set_pattern) {
    NEED_KS();
    if (t_is_wine()) { t_skip("CLIENTINFO layout is win32k specific"); return; }
    kls_thread(w_set_pattern_cache, NULL);
}

struct iso2 { HANDLE a_set, b_read, b_done; SHORT b_state; BYTE b_kb; SHORT b_async; };
static DWORD WINAPI th_ks_b(LPVOID p) {
    struct iso2 *s = p;
    UN(NtUserGetKeyState)(VK_LSHIFT);    /* make this a GUI thread with its own queue */
    SetEvent(s->b_read);
    WaitForSingleObject(s->a_set, 5000);
    s->b_state = UN(NtUserGetKeyState)(VK_LSHIFT);
    BYTE st[256]; UN(NtUserGetKeyboardState)(st); s->b_kb = st[VK_LSHIFT];
    SetEvent(s->b_done);
    return 0;
}
static DWORD w_set_iso(void *arg) {
    struct iso2 *s = arg;
    BYTE snap[256], st[256];
    UN(NtUserGetKeyboardState)(snap);
    HANDLE hb = CreateThread(NULL, 0, th_ks_b, s, 0, NULL);
    if (!hb) return 0;
    WaitForSingleObject(s->b_read, 5000);
    memset(st, 0, 256); st[VK_LSHIFT] = 0x80;
    UN(NtUserSetKeyboardState)(st);
    CHECK(UN(NtUserGetKeyState)(VK_LSHIFT) == (SHORT)0xFF80);
    SetEvent(s->a_set);
    WaitForSingleObject(s->b_done, 5000);
    UN(NtUserSetKeyboardState)(snap);
    WaitForSingleObject(hb, 5000); CloseHandle(hb);
    return 0;
}
TEST(kbd_layouts_state_ks_set_is_per_thread) {
    NEED_KS();
    BYTE main_before[256], main_after[256];
    UN(NtUserGetKeyboardState)(main_before);
    struct iso2 s = { CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), 0, 0, 0 };
    BYTE phys; { SHORT a = UN(NtUserGetAsyncKeyState)(VK_LSHIFT); phys = (a & 0x8000) != 0; }
    kls_thread(w_set_iso, &s);
    UN(NtUserGetKeyboardState)(main_after);
    if (!phys) {
        /* "Changes made to this table do not affect keyboard input to any other thread" */
        CHECKF(s.b_state == 0 || s.b_state == 1, "sibling thread saw %04x", (unsigned short)s.b_state);
        CHECKF((s.b_kb & 0x80) == 0, "sibling GetKeyboardState %02x", s.b_kb);
        CHECK(main_after[VK_LSHIFT] == main_before[VK_LSHIFT]);
    } else t_note("left shift physically down; isolation not observable");
    CloseHandle(s.a_set); CloseHandle(s.b_read); CloseHandle(s.b_done);
}

TEST(kbd_layouts_state_ks_set_user_wrapper_returns_true) {
    NEED_KS();
    BYTE snap[256]; U(GetKeyboardState)(snap);
    BYTE same[256]; memcpy(same, snap, 256);
    CHECK(U(SetKeyboardState)(same));          /* writing the current table back is harmless */
    BYTE back[256]; U(GetKeyboardState)(back);
    for (int vk = 0; vk < 256; vk++) if (kls_ungated(vk)) CHECKF(back[vk] == snap[vk], "vk %x", vk);
}

static DWORD w_gated(void *arg) {
    (void)arg;
    BYTE snap[256], st[256], got[256];
    UN(NtUserGetKeyboardState)(snap);
    memset(st, 0x80, 256);
    UN(NtUserSetKeyboardState)(st);
    UN(NtUserGetKeyboardState)(got);
    int vis = 0, hid = 0;
    for (int vk = 0; vk < 256; vk++) if (!kls_ungated(vk)) { if (got[vk] == 0x80) vis++; else hid++; }
    t_note("foreground-gated vks: %d readable back, %d hidden (gate %s)", vis, hid, hid ? "closed" : "open");
    /* ungated keys are always readable; the gate is all-or-nothing for the rest */
    CHECK(vis == 0 || hid == 0);
    UN(NtUserSetKeyboardState)(snap);
    return 0;
}
TEST(kbd_layouts_state_ks_foreground_gate_is_all_or_nothing) {
    NEED_KS();
    kls_thread(w_gated, NULL);
}

/* ===================================================================== pristine process */

static int cfail;
#define CC(c) do { if (!(c)) { cfail++; printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
CHILD(kbd_layouts_state_ks_pristine) {
    (void)argc; (void)argv;
    unsigned long long t0 = 0, t1 = 0;
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &t0, 8);
    CC(t0 == 0);
    if (!UN(NtUserGetKeyState)) return 0;
    kls_install_stub_callbacks();
    SHORT s = UN(NtUserGetKeyState)(VK_CAPITAL);
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &t1, 8);
    CC(t1 != 0);                      /* the first win32k syscall converted the thread */
    CC(s == 0 || s == 1 || s == (SHORT)0xFF80 || s == (SHORT)0xFF81);
    unsigned long g = 0; kls_ci_u32(KLS_CI_KS_GEN, &g);
    unsigned long long bits = 0; kls_ci_u64(KLS_CI_KS_BITS, &bits);
    BYTE st[256]; UN(NtUserGetKeyboardState)(st);
    CC(bits == pack32(st));           /* NtUserGetKeyState published the cache even in a process that never loaded user32 */
    t_info("pristine key-state gen %lx cache %llx", g, bits);
    return cfail;
}
TEST(kbd_layouts_state_ks_pristine_process_publishes_cache) {
    KLS_NEED();
    if (t_is_wine()) { t_skip("win32k specific"); return; }
    struct k_child_result r = k_child_run("kbd_layouts_state_ks_pristine", "", 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}
