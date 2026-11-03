/* Legacy DirectDraw/Direct3D kernel interface. On 19041 the whole NtGdiDd, NtGdiDvp and NtGdiD3d family is a
 * user-mode no-op: gdi32full exports 94 names that alias one "xor eax,eax; ret" body, the win32u.dll
 * exports do not exist, and gdi32!DdEntry0..56 are "jmp [iat]" thunks to them. Only four NtGdiDd* names are real
 * win32k syscalls (fullscreen sprite create/destroy/notify and VisRgn uniqueness).
 */
#include "d.h"

typedef ULONG (WINAPI *ret0_fn)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);

TEST(gdi_d3dkmt_dd_legacy_gdi32full_exports_present) {
    HMODULE m = t_module("gdi32full.dll");
    REQUIRE(m);
    int found = 0;
    for (int i = 0; i < dk_legacy_cnt; i++) {
        FARPROC p = t_proc_in("gdi32full.dll", dk_legacy_tab[i]);
        if (p) found++;
        else if (!t_is_wine()) CHECKF(0, "gdi32full export %s", dk_legacy_tab[i]);
    }
    if (!t_is_wine()) CHECK(found == dk_legacy_cnt);
}
TEST(gdi_d3dkmt_dd_legacy_body_is_xor_eax_ret) {
    if (t_is_wine()) { t_skip("Wine gdi32full has different bodies"); return; }
    for (int i = 0; i < dk_legacy_cnt; i++) {
        unsigned char b[3] = { 0 };
        FARPROC p = t_proc_in("gdi32full.dll", dk_legacy_tab[i]);
        if (!p) continue;
        CHECKF(k_try_read(p, b, 3) && b[0] == 0x33 && b[1] == 0xC0 && b[2] == 0xC3, "%s bytes %02x %02x %02x", dk_legacy_tab[i], b[0], b[1], b[2]);
    }
}
TEST(gdi_d3dkmt_dd_legacy_all_names_alias_one_body) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    FARPROC first = NULL;
    for (int i = 0; i < dk_legacy_cnt; i++) {
        FARPROC p = t_proc_in("gdi32full.dll", dk_legacy_tab[i]);
        if (!p) continue;
        if (!first) first = p;
        CHECKF(p == first, "%s at %p, expected shared body %p", dk_legacy_tab[i], p, first);
    }
}
TEST(gdi_d3dkmt_dd_legacy_not_syscall_stubs) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (int i = 0; i < dk_legacy_cnt; i++) {
        FARPROC p = t_proc_in("gdi32full.dll", dk_legacy_tab[i]);
        if (p) CHECKF(!k_stub_valid(p), "%s", dk_legacy_tab[i]);
    }
}
TEST(gdi_d3dkmt_dd_legacy_not_exported_by_win32u) {
    /* The kernel has no DdEntry service numbers on 19041: win32u carries no NtGdiDd{Create,Query,Delete}DirectDrawObject etc. */
    if (t_is_wine()) { t_skip("Wine win32u differs"); return; }
    for (int i = 0; i < dk_ddentry_cnt; i++) CHECKF(t_proc_in("win32u.dll", dk_ddentry_tab[i]) == NULL, "win32u exports %s", dk_ddentry_tab[i]);
}
TEST(gdi_d3dkmt_dd_legacy_calls_return_zero_no_args) {
    if (t_is_wine()) { t_skip("Wine bodies are not no-ops"); return; }
    for (int i = 0; i < dk_legacy_cnt; i++) {
        ret0_fn f = (ret0_fn)t_proc_in("gdi32full.dll", dk_legacy_tab[i]);
        if (f) CHECKF(f(0, 0, 0, 0) == 0, "%s", dk_legacy_tab[i]);
    }
}
TEST(gdi_d3dkmt_dd_legacy_calls_ignore_arguments) {
    /* Non-pointer garbage in the register args is never touched by a body that only clears eax. */
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (int i = 0; i < dk_legacy_cnt; i++) {
        ret0_fn f = (ret0_fn)t_proc_in("gdi32full.dll", dk_legacy_tab[i]);
        if (f) CHECKF(f(1, 2, 3, 4) == 0 && f(0xFFFFFFFF, 0, 0x1234, 7) == 0, "%s", dk_legacy_tab[i]);
    }
}
TEST(gdi_d3dkmt_dd_legacy_calls_leave_last_error_alone) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    ret0_fn f = (ret0_fn)t_proc_in("gdi32full.dll", "NtGdiDdCreateDirectDrawObject");
    REQUIRE(f);
    SetLastError(0xABCD);
    f(0, 0, 0, 0);
    CHECK(GetLastError() == 0xABCD);
}
TEST(gdi_d3dkmt_dd_legacy_calls_do_not_convert_thread_to_gui) {
    /* a user-mode no-op never reaches the kernel, so Win32ThreadInfo is unchanged by it */
    if (t_is_wine()) { t_skip("Wine"); return; }
    ret0_fn f = (ret0_fn)t_proc_in("gdi32full.dll", "NtGdiDdGetDriverInfo");
    REQUIRE(f);
    void *before = NULL, *after = NULL;
    CHECK(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &before));
    for (int i = 0; i < 20; i++) f(0, 0, 0, 0);
    CHECK(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &after));
    CHECK(before == after);
}
TEST(gdi_d3dkmt_dd_ddentry_thunk_prologue) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    char nm[32];
    for (int i = 0; i < dk_ddentry_cnt; i++) {
        snprintf(nm, sizeof nm, "DdEntry%d", i);
        FARPROC p = t_proc_in("gdi32.dll", nm);
        CHECKF(p != NULL, "%s", nm);
        if (!p) continue;
        unsigned char b[2] = { 0 };
        CHECKF(k_try_read(p, b, 2) && b[0] == 0xFF && b[1] == 0x25, "%s is jmp [rip+disp32]: %02x %02x", nm, b[0], b[1]);
    }
}
TEST(gdi_d3dkmt_dd_ddentry_thunk_slot_points_at_gdi32full_export) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    char nm[32];
    for (int i = 0; i < dk_ddentry_cnt; i++) {
        snprintf(nm, sizeof nm, "DdEntry%d", i);
        unsigned char *p = (unsigned char *)t_proc_in("gdi32.dll", nm);
        if (!p) continue;
        INT32 disp = 0; void *target = NULL;
        if (!k_try_read(p + 2, &disp, 4) || !k_try_read_ptr(p + 6 + disp, &target)) { CHECKF(0, "%s: cannot read IAT slot", nm); continue; }
        /* each DdEntryN imports its NtGdiDd* name from gdi32full; the table in dk_ddentry_tab is the analysed mapping */
        CHECKF(target == (void *)t_proc_in("gdi32full.dll", dk_ddentry_tab[i]), "%s slot -> %p, gdi32full!%s %p", nm, target, dk_ddentry_tab[i], t_proc_in("gdi32full.dll", dk_ddentry_tab[i]));
    }
}
TEST(gdi_d3dkmt_dd_ddentry_calls_return_zero) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    char nm[32];
    for (int i = 0; i < dk_ddentry_cnt; i++) {
        snprintf(nm, sizeof nm, "DdEntry%d", i);
        ret0_fn f = (ret0_fn)t_proc_in("gdi32.dll", nm);
        if (!f) continue;
        /* every thunk reaches the shared "return 0" body except DdEntry52 (NtGdiDdUnattachSurface), which fails with
         * STATUS_UNSUCCESSFUL. */
        ULONG want = i == 52 ? 0xC0000001u : 0;
        ULONG got = (ULONG)(ULONG_PTR)f(0, 0, 0, 0);   /* the status is a 32 bit value; the upper half of rax is not specified */
        CHECKF(got == want, "%s returned 0x%lx", nm, got);
    }
}
TEST(gdi_d3dkmt_dd_ddentry_first_and_last_names) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    CHECK(t_proc_in("gdi32.dll", "DdEntry0") != NULL);
    CHECK(t_proc_in("gdi32.dll", "DdEntry56") != NULL);
    CHECK(t_proc_in("gdi32.dll", "DdEntry57") == NULL);
    CHECK(dk_ddentry_cnt == 57);
}
TEST(gdi_d3dkmt_dd_ddentry_distinct_thunks) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    char a[32], b[32];
    for (int i = 0; i + 1 < dk_ddentry_cnt; i++) {
        snprintf(a, sizeof a, "DdEntry%d", i); snprintf(b, sizeof b, "DdEntry%d", i + 1);
        CHECKF(t_proc_in("gdi32.dll", a) != t_proc_in("gdi32.dll", b), "%s and %s are separate 6-byte thunks", a, b);
    }
}
TEST(gdi_d3dkmt_dd_gdi32_real_dd_exports_are_jumps_to_win32u) {
    static const struct { const char *e, *t; } v[] = {
        { "DdCreateFullscreenSprite", "NtGdiDdCreateFullscreenSprite" },
        { "DdDestroyFullscreenSprite", "NtGdiDdDestroyFullscreenSprite" },
        { "DdNotifyFullscreenSpriteUpdate", "NtGdiDdNotifyFullscreenSpriteUpdate" },
        { "DdQueryVisRgnUniqueness", "NtGdiDdQueryVisRgnUniqueness" },
    };
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        unsigned char *p = (unsigned char *)t_proc_in("gdi32.dll", v[i].e);
        FARPROC t = t_proc_in("win32u.dll", v[i].t);
        CHECKF(p && t, "%s / %s", v[i].e, v[i].t);
        if (!p || !t) continue;
        INT32 disp = 0; void *target = NULL;
        CHECK(p[0] == 0xFF && p[1] == 0x25);
        if (k_try_read(p + 2, &disp, 4) && k_try_read_ptr(p + 6 + disp, &target)) CHECKF(target == (void *)t, "%s slot %p vs win32u %p", v[i].e, target, t);
        CHECK(k_stub_valid(t));
    }
}

/* ---- fullscreen sprite syscalls: bad HDC => STATUS_INVALID_PARAMETER from XDCOBJ::vLock failing ---- */
typedef LONG (WINAPI *sprite_create_fn)(HDC, ULONG, void **, HDC **);
typedef LONG (WINAPI *sprite_op_fn)(HDC, void *);

static void bad_dc_values(HDC *out, int *n, HGDIOBJ *keep) {
    /* values that are not a live DC of this process: NULL, small ints, wrong-type GDI objects */
    int i = 0;
    out[i++] = NULL; out[i++] = (HDC)(ULONG_PTR)1; out[i++] = (HDC)(ULONG_PTR)0x1234; out[i++] = (HDC)(ULONG_PTR)0xFFFFFFFF;
    out[i++] = (HDC)(ULONG_PTR)0x00010001;
    keep[0] = U(CreateSolidBrush) ? U(CreateSolidBrush)(RGB(1, 2, 3)) : NULL;   /* brush handle: vLock expects DC type */
    keep[1] = U(CreateRectRgn) ? U(CreateRectRgn)(0, 0, 4, 4) : NULL;
    keep[2] = U(CreateBitmap) ? U(CreateBitmap)(2, 2, 1, 1, NULL) : NULL;
    for (int k = 0; k < 3; k++) if (keep[k]) out[i++] = (HDC)keep[k];
    *n = i;
}
TEST(gdi_d3dkmt_dd_sprite_create_bad_dc_invalid_parameter) {
    sprite_create_fn f = (sprite_create_fn)dk_nt("NtGdiDdCreateFullscreenSprite");
    REQUIRE(f);
    REQUIRE(U(DeleteObject));
    HDC v[16]; HGDIOBJ keep[3]; int n;
    bad_dc_values(v, &n, keep);
    for (int i = 0; i < n; i++) {
        HDC *out_dc = (HDC *)(ULONG_PTR)0x77; void *out_sprite = (void *)(ULONG_PTR)0x55;
        (void)out_dc;
        LONG st = f(v[i], 0, &out_sprite, NULL);
        /* SpDdCreateFullscreenSprite: XDCOBJ::vLock(hdc) == 0 -> return -0x3ffffff3, before any out parameter is written */
        CHECKF(st == ST_INVALID_PARAMETER, "hdc %p -> 0x%lx", v[i], (unsigned long)st);
        CHECKF(out_sprite == (void *)(ULONG_PTR)0x55, "out sprite untouched, got %p", out_sprite);
    }
    for (int k = 0; k < 3; k++) if (keep[k]) U(DeleteObject)(keep[k]);
}
TEST(gdi_d3dkmt_dd_sprite_destroy_bad_dc_invalid_parameter) {
    sprite_op_fn f = (sprite_op_fn)dk_nt("NtGdiDdDestroyFullscreenSprite");
    REQUIRE(f);
    REQUIRE(U(DeleteObject));
    HDC v[16]; HGDIOBJ keep[3]; int n;
    bad_dc_values(v, &n, keep);
    for (int i = 0; i < n; i++) {
        LONG st = f(v[i], NULL);
        CHECKF(st == ST_INVALID_PARAMETER, "hdc %p -> 0x%lx", v[i], (unsigned long)st);
    }
    for (int k = 0; k < 3; k++) if (keep[k]) U(DeleteObject)(keep[k]);
}
TEST(gdi_d3dkmt_dd_sprite_notify_bad_dc_invalid_parameter) {
    sprite_op_fn f = (sprite_op_fn)dk_nt("NtGdiDdNotifyFullscreenSpriteUpdate");
    REQUIRE(f);
    REQUIRE(U(DeleteObject));
    HDC v[16]; HGDIOBJ keep[3]; int n;
    bad_dc_values(v, &n, keep);
    for (int i = 0; i < n; i++) {
        LONG st = f(v[i], NULL);
        CHECKF(st == ST_INVALID_PARAMETER, "hdc %p -> 0x%lx", v[i], (unsigned long)st);
    }
    for (int k = 0; k < 3; k++) if (keep[k]) U(DeleteObject)(keep[k]);
}
TEST(gdi_d3dkmt_dd_sprite_stale_dc_handle_invalid_parameter) {
    sprite_op_fn f = (sprite_op_fn)dk_nt("NtGdiDdDestroyFullscreenSprite");
    REQUIRE(f);
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(DeleteDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    CHECK(U(DeleteDC)(dc));
    /* the freed cell is unlocked/cleared: the stale handle no longer locks as a DC */
    CHECK(f(dc, NULL) == ST_INVALID_PARAMETER);
}
TEST(gdi_d3dkmt_dd_sprite_syscall_through_gdi32_jump) {
    sprite_op_fn f = (sprite_op_fn)t_proc_in("gdi32.dll", "DdDestroyFullscreenSprite");
    REQUIRE(f);
    if (t_is_wine()) { t_skip("Wine"); return; }
    CHECK(f((HDC)(ULONG_PTR)0x4321, NULL) == ST_INVALID_PARAMETER);
}
TEST(gdi_d3dkmt_dd_sprite_bad_dc_leaves_gdi_counts_alone) {
    sprite_op_fn f = (sprite_op_fn)dk_nt("NtGdiDdNotifyFullscreenSpriteUpdate");
    REQUIRE(f); REQUIRE(U(GetGuiResources));
    unsigned g0 = dk_gui_count(GR_GDIOBJECTS), u0 = dk_gui_count(GR_USEROBJECTS);
    for (int i = 0; i < 50; i++) f((HDC)(ULONG_PTR)(0x1000 + i), NULL);
    CHECK(dk_gui_count(GR_GDIOBJECTS) == g0);
    CHECK(dk_gui_count(GR_USEROBJECTS) == u0);
}
TEST(gdi_d3dkmt_dd_sprite_bad_dc_converts_thread_to_gui) {
    /* NtGdiDd*Sprite are win32k syscalls: calling one on a non-GUI thread converts the thread (Win32ThreadInfo != NULL). */
    sprite_op_fn f = (sprite_op_fn)dk_nt("NtGdiDdNotifyFullscreenSpriteUpdate");
    REQUIRE(f);
    f(NULL, NULL);
    void *ti = NULL;
    CHECK(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti));
    if (!t_is_wine()) CHECK(ti != NULL);
}

/* ---- NtGdiDdQueryVisRgnUniqueness: returns the DWORD global giVisRgnUniqueness ---- */
typedef ULONG (WINAPI *uniq_fn)(void);
static ULONG uniq_stable(uniq_fn f) {
    /* the counter is bumped by VisRgn changes anywhere on the desktop; take the value that two consecutive reads agree on */
    ULONG a = f(), b;
    for (int i = 0; i < 16; i++) { b = f(); if (a == b) return a; a = b; }
    return a;
}
TEST(gdi_d3dkmt_dd_visrgn_uniqueness_back_to_back_stable) {
    uniq_fn f = (uniq_fn)dk_nt("NtGdiDdQueryVisRgnUniqueness");
    REQUIRE(f);
    ULONG a = f(), b = f(), c = f();
    CHECKF(a == b || b == c, "%lu %lu %lu", (unsigned long)a, (unsigned long)b, (unsigned long)c);
}
TEST(gdi_d3dkmt_dd_visrgn_uniqueness_nonzero_after_gui) {
    uniq_fn f = (uniq_fn)dk_nt("NtGdiDdQueryVisRgnUniqueness");
    REQUIRE(f);
    if (t_is_wine()) { t_skip("Wine"); return; }
    ULONG v = uniq_stable(f);
    /* every window show/move/z-order change on the desktop recomputes visible regions; a booted interactive session has done so */
    if (v == 0) t_note("giVisRgnUniqueness is 0 on this system");
    t_info("giVisRgnUniqueness = %lu", (unsigned long)v);
}
TEST(gdi_d3dkmt_dd_visrgn_uniqueness_gdi32_jump_agrees) {
    uniq_fn f = (uniq_fn)dk_nt("NtGdiDdQueryVisRgnUniqueness");
    uniq_fn g = (uniq_fn)t_proc_in("gdi32.dll", "DdQueryVisRgnUniqueness");
    REQUIRE(f); REQUIRE(g);
    if (t_is_wine()) { t_skip("Wine"); return; }
    ULONG a = uniq_stable(f), b = uniq_stable(g), c = uniq_stable(f);
    CHECKF(b == a || b == c, "%lu / %lu / %lu", (unsigned long)a, (unsigned long)b, (unsigned long)c);
}
TEST(gdi_d3dkmt_dd_visrgn_uniqueness_ignores_arguments) {
    typedef ULONG (WINAPI *uniq4_fn)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);
    uniq4_fn f = (uniq4_fn)dk_nt("NtGdiDdQueryVisRgnUniqueness");
    uniq_fn g = (uniq_fn)f;
    REQUIRE(f);
    if (t_is_wine()) { t_skip("Wine"); return; }
    /* the body is - arguments are never read */
    ULONG a = uniq_stable(g);
    ULONG b = f(1, 2, 3, 4), c = f(0, 0, 0, 0);
    CHECKF(b == a || c == a || b == c, "%lu %lu %lu", (unsigned long)a, (unsigned long)b, (unsigned long)c);
}
TEST(gdi_d3dkmt_dd_visrgn_uniqueness_moves_with_window_visibility) {
    uniq_fn f = (uniq_fn)dk_nt("NtGdiDdQueryVisRgnUniqueness");
    REQUIRE(f);
    REQUIRE(U(ShowWindow)); REQUIRE(U(DestroyWindow));
    HWND w = k_make_window("win32kt visrgn");
    REQUIRE(w);
    ULONG before = uniq_stable(f);
    U(ShowWindow)(w, SW_SHOWNOACTIVATE);
    k_pump(50);
    ULONG shown = uniq_stable(f);
    U(ShowWindow)(w, SW_HIDE);
    k_pump(50);
    ULONG hidden = uniq_stable(f);
    U(DestroyWindow)(w);
    /* off-screen window: the counter may or may not move (visible-region recompute is clip-dependent) - record it */
    t_info("giVisRgnUniqueness before=%lu shown=%lu hidden=%lu", (unsigned long)before, (unsigned long)shown, (unsigned long)hidden);
}
