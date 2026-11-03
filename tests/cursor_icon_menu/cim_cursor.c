/* Per-thread cursor state kept by the kernel (queue current cursor, ShowCursor counter), CURSORINFO,
 * the session-wide cursor clip rectangle, and object lifetime across threads. */
#include "cim.h"

static int dpi_is_96(void) {
    UINT (WINAPI *f)(void) = (UINT (WINAPI *)(void))t_proc_in("user32.dll", "GetDpiForSystem");
    return !f || f() == 96;
}

/* ---------------- SetCursor / GetCursor: the cursor of the calling thread's queue ---------------- */
struct sc { int fails; HCURSOR a, b; };
static DWORD sc_roundtrip(void *p) {
    struct sc *s = p;
    HCURSOR prev = U(GetCursor)();
    HCURSOR r = U(SetCursor)(s->a);
    if (r != prev) s->fails++;                     /* SetCursor returns the previous queue cursor */
    if (U(GetCursor)() != s->a) s->fails++;        /* NtUserGetCursor reads pq->spcurCurrent->head */
    r = U(SetCursor)(s->b);
    if (r != s->a) s->fails++;
    if (U(GetCursor)() != s->b) s->fails++;
    r = U(SetCursor)(NULL);                        /* NULL hides the cursor for this queue */
    if (r != s->b) s->fails++;
    if (U(GetCursor)() != NULL) s->fails++;
    r = U(SetCursor)(prev);
    if (r != NULL) s->fails++;
    if (U(GetCursor)() != prev) s->fails++;
    return 0;
}
TEST(cursor_icon_menu_cursor_setcursor_getcursor_roundtrip_thread) {
    CIM_GUI();
    struct sc s = {0, NULL, NULL};
    s.a = cim_mk_cursor_ii(16, 16, 0, 0); s.b = cim_mk_cursor_ii(16, 16, 1, 1);
    if (!s.a || !s.b) { t_skip("cannot create cursors"); return; }
    k_run_gui_thread(sc_roundtrip, &s);
    EXPECT_EQ(s.fails, 0);
    U(DestroyCursor)(s.a); U(DestroyCursor)(s.b);
}
static DWORD sc_nt(void *p) {
    struct sc *s = p;
    HANDLE prev = UN(NtUserGetCursor)();
    if (UN(NtUserSetCursor)(s->a) != prev) s->fails++;
    if (UN(NtUserGetCursor)() != s->a) s->fails++;
    if (U(GetCursor)() != s->a) s->fails++;
    if (UN(NtUserSetCursor)(s->b) != s->a) s->fails++;
    if (UN(NtUserSetCursor)(prev) != s->b) s->fails++;
    return 0;
}
TEST(cursor_icon_menu_cursor_nt_setcursor_getcursor_thread) {
    CIM_GUI(); REQUIRE(UN(NtUserSetCursor)); REQUIRE(UN(NtUserGetCursor));
    struct sc s = {0, NULL, NULL};
    s.a = cim_mk_cursor_ii(16, 16, 0, 0); s.b = cim_mk_cursor_ii(16, 16, 1, 1);
    if (!s.a || !s.b) { t_skip("cannot create cursors"); return; }
    k_run_gui_thread(sc_nt, &s);
    EXPECT_EQ(s.fails, 0);
    U(DestroyCursor)(s.a); U(DestroyCursor)(s.b);
}
static DWORD sc_invalid(void *p) {
    struct sc *s = p;
    HCURSOR cur = U(GetCursor)();
    HANDLE bad[3] = { s->a, (HANDLE)(ULONG_PTR)0x1234, s->b };
    for (int i = 0; i < 3; i++) {
        HCURSOR r; CIM_CALL_ERR(r, U(SetCursor)((HCURSOR)bad[i]));
        if (r != NULL || GetLastError() != E_INVALID_CURSOR_HANDLE) s->fails++;
        if (U(GetCursor)() != cur) s->fails++; /* a rejected handle leaves the queue cursor alone */
    }
    return 0;
}
TEST(cursor_icon_menu_cursor_setcursor_invalid_handles) {
    CIM_GUI();
    struct sc s = {0, NULL, NULL};
    HICON dead = cim_mk_cursor_ii(16, 16, 0, 0);
    HMENU m = U(CreateMenu)();
    if (!dead || !m) { t_skip("setup failed"); return; }
    U(DestroyCursor)(dead);
    s.a = dead; s.b = (HCURSOR)m;       /* destroyed cursor, forged value, and a handle of the wrong type */
    k_run_gui_thread(sc_invalid, &s);
    EXPECT_EQ(s.fails, 0);
    U(DestroyMenu)(m);
}
static DWORD sc_isolated(void *p) {
    struct sc *s = p;
    if (U(GetCursor)() == s->a) s->fails++; /* another thread's SetCursor must not be visible here */
    return 0;
}
TEST(cursor_icon_menu_cursor_current_cursor_is_per_thread) {
    CIM_GUI();
    struct sc s = {0, NULL, NULL};
    s.a = cim_mk_cursor_ii(16, 16, 0, 0);
    if (!s.a) { t_skip("no cursor"); return; }
    HCURSOR prev = U(SetCursor)(s.a);
    k_run_gui_thread(sc_isolated, &s);
    EXPECT_EQ(s.fails, 0);
    EXPECT_EQ(U(GetCursor)(), s.a);
    U(SetCursor)(prev);
    U(DestroyCursor)(s.a);
}
TEST(cursor_icon_menu_cursor_setcursor_system_cursor_returns_previous) {
    CIM_GUI();
    HCURSOR arrow = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512)), cross = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32515));
    if (!arrow || !cross) { t_skip("system cursors missing"); return; }
    HCURSOR old = U(GetCursor)();
    HCURSOR p1 = U(SetCursor)(arrow);
    EXPECT_EQ(p1, old);
    HCURSOR p2 = U(SetCursor)(cross);
    EXPECT_EQ(p2, arrow);
    HCURSOR cur = U(GetCursor)();
    if (cur != cross) t_note("GetCursor returned %p after SetCursor(%p): DPI variant substituted", cur, cross);
    U(SetCursor)(old);
}

/* ---------------- ShowCursor counter ---------------- */
struct sh { int fails; int base; int other; };
static DWORD sh_seq(void *p) {
    struct sh *s = p;
    int b = U(ShowCursor)(TRUE);          /* ShowCursor increments the cursor level and returns the new value */
    s->base = b;
    if (U(ShowCursor)(FALSE) != b - 1) s->fails++;
    if (U(ShowCursor)(FALSE) != b - 2) s->fails++;
    if (U(ShowCursor)(FALSE) != b - 3) s->fails++;
    if (U(ShowCursor)(TRUE) != b - 2) s->fails++;
    if (U(ShowCursor)(TRUE) != b - 1) s->fails++;
    if (U(ShowCursor)(TRUE) != b) s->fails++;
    if (U(ShowCursor)(TRUE) != b + 1) s->fails++;
    if (U(ShowCursor)(FALSE) != b) s->fails++;
    U(ShowCursor)(FALSE);                 /* undo the first TRUE */
    return 0;
}
TEST(cursor_icon_menu_cursor_showcursor_counter_sequence_thread) {
    CIM_GUI();
    struct sh s = {0, 0, 0};
    k_run_gui_thread(sh_seq, &s);
    EXPECT_EQ(s.fails, 0);
    t_note("ShowCursor(TRUE) on a fresh queue returned %d (counter was %d)", s.base, s.base - 1);
}
static DWORD sh_nt(void *p) {
    struct sh *s = p;
    int b = UN(NtUserShowCursor)(TRUE);
    if (UN(NtUserShowCursor)(FALSE) != b - 1) s->fails++;
    if (UN(NtUserShowCursor)(2) != b) s->fails++;      /* any non-zero value counts as TRUE */
    if (UN(NtUserShowCursor)((BOOL)0x100) != b + 1) s->fails++;
    if (UN(NtUserShowCursor)(FALSE) != b) s->fails++;
    UN(NtUserShowCursor)(FALSE);
    return 0;
}
TEST(cursor_icon_menu_cursor_nt_showcursor_nonzero_is_true) {
    CIM_GUI(); REQUIRE(UN(NtUserShowCursor));
    struct sh s = {0, 0, 0};
    k_run_gui_thread(sh_nt, &s);
    EXPECT_EQ(s.fails, 0);
}
static DWORD sh_fresh(void *p) {
    struct sh *s = p;
    s->base = U(ShowCursor)(TRUE);
    U(ShowCursor)(FALSE);
    return 0;
}
TEST(cursor_icon_menu_cursor_showcursor_counter_is_per_thread) {
    CIM_GUI();
    struct sh a = {0, 0, 0}, b = {0, 0, 0};
    k_run_gui_thread(sh_fresh, &a);
    int my0 = U(ShowCursor)(TRUE);           /* push this thread's counter away from the fresh value */
    U(ShowCursor)(TRUE); U(ShowCursor)(TRUE);
    k_run_gui_thread(sh_fresh, &b);
    EXPECT_EQ(b.base, a.base);               /* a fresh queue always starts at the same level */
    EXPECT_EQ(U(ShowCursor)(FALSE), my0 + 1);
    EXPECT_EQ(U(ShowCursor)(FALSE), my0);
    EXPECT_EQ(U(ShowCursor)(FALSE), my0 - 1);   /* back at the level this thread started with */
}
TEST(cursor_icon_menu_cursor_showcursor_main_thread_restores) {
    CIM_GUI();
    int base = U(ShowCursor)(TRUE);
    EXPECT_EQ(U(ShowCursor)(FALSE), base - 1);
    EXPECT_EQ(U(ShowCursor)(FALSE), base - 2);
    EXPECT_EQ(U(ShowCursor)(TRUE), base - 1);   /* net effect of the four calls: none */
}
static DWORD sh_many(void *p) {
    struct sh *s = p;
    int b = U(ShowCursor)(TRUE);
    for (int i = 1; i <= 200; i++) if (U(ShowCursor)(TRUE) != b + i) s->fails++;
    for (int i = 199; i >= 0; i--) if (U(ShowCursor)(FALSE) != b + i) s->fails++;
    U(ShowCursor)(FALSE);
    return 0;
}
TEST(cursor_icon_menu_cursor_showcursor_deep_counter) {
    CIM_GUI();
    struct sh s = {0, 0, 0};
    k_run_gui_thread(sh_many, &s);
    EXPECT_EQ(s.fails, 0);
}

/* ---------------- GetCursorInfo ---------------- */
static int cursorinfo_usable(CURSORINFO *ci) {
    memset(ci, 0, sizeof *ci); ci->cbSize = sizeof *ci;
    SetLastError(0);
    if (!U(GetCursorInfo)(ci)) { t_skip("GetCursorInfo failed err %lu (not the input desktop?)", GetLastError()); return 0; }
    return 1;
}
TEST(cursor_icon_menu_cursorinfo_fields_sane) {
    CIM_GUI();
    CURSORINFO ci; if (!cursorinfo_usable(&ci)) return;
    EXPECT_EQ(ci.cbSize, 0);   /* the kernel writes the result without echoing cbSize: it comes back 0 on Windows 10 */
    EXPECT_EQ(sizeof(CURSORINFO), 0x18);
    CHECK((ci.flags & ~(DWORD)(CURSOR_SHOWING | 2)) == 0); /* CURSOR_SHOWING | CURSOR_SUPPRESSED */
    if (ci.hCursor && cim_ready()) CHECK(cim_entry_is(ci.hCursor, CT_CURSOR));
    int vx = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    int vw = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), vh = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    if (dpi_is_96()) {
        CHECK(ci.ptScreenPos.x >= vx && ci.ptScreenPos.x <= vx + vw);
        CHECK(ci.ptScreenPos.y >= vy && ci.ptScreenPos.y <= vy + vh);
    }
}
TEST(cursor_icon_menu_cursorinfo_matches_getcursorpos) {
    CIM_GUI();
    CURSORINFO ci; POINT pt;
    int same = 0;
    for (int i = 0; i < 5 && !same; i++) {
        if (!cursorinfo_usable(&ci)) return;
        CHECK(U(GetCursorPos)(&pt));
        same = ci.ptScreenPos.x == pt.x && ci.ptScreenPos.y == pt.y;
    }
    if (!same) t_note("GetCursorInfo %ld,%ld vs GetCursorPos %ld,%ld differ (pointer moving?)", ci.ptScreenPos.x, ci.ptScreenPos.y, pt.x, pt.y);
}
TEST(cursor_icon_menu_cursorinfo_bad_cbsize_table) {
    CIM_GUI();
    CURSORINFO ok; if (!cursorinfo_usable(&ok)) return;
    static const DWORD sizes[] = { 0, 4, 0x10, 0x14, 0x17, 0x19, 0x1c, 0x20, 0x28, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        CURSORINFO ci; memset(&ci, 0x5a, sizeof ci); ci.cbSize = sizes[i];
        BOOL r; CIM_CALL_ERR(r, U(GetCursorInfo)(&ci));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "cbSize 0x%lx err %lu", sizes[i], GetLastError());
        CHECKF(ci.flags == 0x5a5a5a5a && ci.hCursor == (HCURSOR)0x5a5a5a5a5a5a5a5aull, "cbSize 0x%lx: output was written", sizes[i]); /* only cbSize == 0x18 reaches the copy-out */
    }
}
TEST(cursor_icon_menu_cursorinfo_nt_direct) {
    CIM_GUI(); REQUIRE(UN(NtUserGetCursorInfo));
    CURSORINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
    BOOL r; CIM_CALL_ERR(r, UN(NtUserGetCursorInfo)(&ci));
    if (!r) { t_skip("NtUserGetCursorInfo failed err %lu", GetLastError()); return; }
    EXPECT_EQ(ci.cbSize, 0);   /* not echoed back (see cursorinfo_fields_sane) */
    CHECK((ci.flags & ~(DWORD)3) == 0);
}
TEST(cursor_icon_menu_cursorinfo_hcursor_is_cursor_object) {
    CIM_NEED();
    CURSORINFO ci; if (!cursorinfo_usable(&ci)) return;
    if (!ci.hCursor) { t_note("no cursor image (hidden)"); return; }
    /* the reported handle is a live cursor object that GetIconInfo accepts */
    ICONINFO ii;
    if (U(GetIconInfo)(ci.hCursor, &ii)) { U(DeleteObject)(ii.hbmMask); if (ii.hbmColor) U(DeleteObject)(ii.hbmColor); }
    else t_note("GetIconInfo(current cursor) failed err %lu", GetLastError());
}

/* ---------------- ClipCursor / GetClipCursor ---------------- */
struct clipsave { RECT r; int ok; };
static void clip_save(struct clipsave *c) { c->ok = U(GetClipCursor)(&c->r); }
static void clip_restore(struct clipsave *c) {
    RECT v = { U(GetSystemMetrics)(SM_XVIRTUALSCREEN), U(GetSystemMetrics)(SM_YVIRTUALSCREEN), 0, 0 };
    v.right = v.left + U(GetSystemMetrics)(SM_CXVIRTUALSCREEN); v.bottom = v.top + U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    if (!c->ok) { U(ClipCursor)(NULL); return; }
    if (!memcmp(&c->r, &v, sizeof v)) U(ClipCursor)(NULL);
    else U(ClipCursor)(&c->r);
}
TEST(cursor_icon_menu_clip_getclipcursor_returns_nonempty) {
    CIM_GUI();
    RECT r = {-1, -1, -1, -1};
    BOOL ok; CIM_CALL_ERR(ok, U(GetClipCursor)(&r));
    if (!ok) { t_skip("GetClipCursor failed err %lu", GetLastError()); return; }
    CHECK(r.right > r.left && r.bottom > r.top);
}
TEST(cursor_icon_menu_clip_default_is_virtual_screen) {
    CIM_GUI();
    struct clipsave sv; clip_save(&sv);
    if (!sv.ok) { t_skip("GetClipCursor failed"); return; }
    CHECK(U(ClipCursor)(NULL));
    RECT r; CHECK(U(GetClipCursor)(&r));
    if (dpi_is_96()) {
        EXPECT_EQ(r.left, U(GetSystemMetrics)(SM_XVIRTUALSCREEN));
        EXPECT_EQ(r.top, U(GetSystemMetrics)(SM_YVIRTUALSCREEN));
        EXPECT_EQ(r.right, U(GetSystemMetrics)(SM_XVIRTUALSCREEN) + U(GetSystemMetrics)(SM_CXVIRTUALSCREEN));
        EXPECT_EQ(r.bottom, U(GetSystemMetrics)(SM_YVIRTUALSCREEN) + U(GetSystemMetrics)(SM_CYVIRTUALSCREEN));
    } else t_note("system DPI != 96: virtual screen comparison skipped");
    clip_restore(&sv);
}
TEST(cursor_icon_menu_clip_set_and_get_roundtrip) {
    CIM_GUI();
    struct clipsave sv; clip_save(&sv);
    if (!sv.ok) { t_skip("GetClipCursor failed"); return; }
    RECT want = { 50, 60, 250, 260 };
    BOOL ok; CIM_CALL_ERR(ok, U(ClipCursor)(&want));
    if (!ok) { clip_restore(&sv); t_skip("ClipCursor failed err %lu", GetLastError()); return; }
    RECT got = {0};
    CHECK(U(GetClipCursor)(&got));
    if (dpi_is_96()) {
        EXPECT_EQ(got.left, want.left); EXPECT_EQ(got.top, want.top);
        EXPECT_EQ(got.right, want.right); EXPECT_EQ(got.bottom, want.bottom);
    } else t_note("clip %ld,%ld,%ld,%ld at non-96 DPI", got.left, got.top, got.right, got.bottom);
    clip_restore(&sv);
    RECT back; CHECK(U(GetClipCursor)(&back));
    EXPECT_EQ(back.left, sv.r.left); EXPECT_EQ(back.top, sv.r.top);
    EXPECT_EQ(back.right, sv.r.right); EXPECT_EQ(back.bottom, sv.r.bottom);
}
TEST(cursor_icon_menu_clip_clear_after_set) {
    CIM_GUI();
    struct clipsave sv; clip_save(&sv);
    if (!sv.ok) { t_skip("GetClipCursor failed"); return; }
    RECT want = { 10, 10, 110, 110 };
    if (!U(ClipCursor)(&want)) { clip_restore(&sv); t_skip("ClipCursor failed"); return; }
    CHECK(U(ClipCursor)(NULL));
    RECT r; CHECK(U(GetClipCursor)(&r));
    CHECK(r.right - r.left > 110 || r.bottom - r.top > 110 || !dpi_is_96()); /* back to a screen-sized rectangle */
    clip_restore(&sv);
}
TEST(cursor_icon_menu_clip_nt_direct_matches_userland) {
    CIM_GUI(); REQUIRE(UN(NtUserClipCursor)); REQUIRE(UN(NtUserGetClipCursor));
    struct clipsave sv; clip_save(&sv);
    if (!sv.ok) { t_skip("GetClipCursor failed"); return; }
    RECT want = { 30, 40, 330, 340 }, a = {0}, b = {0};
    BOOL ok = UN(NtUserClipCursor)(&want);
    if (!ok) { clip_restore(&sv); t_skip("NtUserClipCursor failed err %lu", GetLastError()); return; }
    CHECK(UN(NtUserGetClipCursor)(&a));
    CHECK(U(GetClipCursor)(&b));
    EXPECT_EQ(a.left, b.left); EXPECT_EQ(a.top, b.top); EXPECT_EQ(a.right, b.right); EXPECT_EQ(a.bottom, b.bottom);
    EXPECT_EQ(UN(NtUserClipCursor)(NULL), 1);
    clip_restore(&sv);
}
TEST(cursor_icon_menu_clip_repeated_set_idempotent) {
    CIM_GUI();
    struct clipsave sv; clip_save(&sv);
    if (!sv.ok) { t_skip("GetClipCursor failed"); return; }
    RECT want = { 100, 100, 300, 300 };
    int ok = 1;
    for (int i = 0; i < 20; i++) if (!U(ClipCursor)(&want)) ok = 0;
    EXPECT_EQ(ok, 1);
    for (int i = 0; i < 20; i++) if (!U(ClipCursor)(NULL)) ok = 0;
    EXPECT_EQ(ok, 1);
    clip_restore(&sv);
}
struct clipread { RECT r; BOOL ok; };
static DWORD clip_reader(void *p) { struct clipread *c = p; c->ok = U(GetClipCursor)(&c->r); return 0; }
TEST(cursor_icon_menu_clip_visible_to_other_thread) {
    CIM_GUI();
    struct clipsave sv; clip_save(&sv);
    if (!sv.ok) { t_skip("GetClipCursor failed"); return; }
    RECT want = { 70, 80, 270, 280 };
    if (!U(ClipCursor)(&want)) { clip_restore(&sv); t_skip("ClipCursor failed"); return; }
    struct clipread c = { {0}, 0 };
    k_run_gui_thread(clip_reader, &c);   /* gpCursorClip is session wide, not per queue */
    CHECK(c.ok);
    if (dpi_is_96()) { EXPECT_EQ(c.r.left, want.left); EXPECT_EQ(c.r.top, want.top); EXPECT_EQ(c.r.right, want.right); EXPECT_EQ(c.r.bottom, want.bottom); }
    clip_restore(&sv);
}

/* ---------------- object lifetime across threads ---------------- */
struct life { HANDLE cur; HANDLE menu; HANDLE icon; };
static DWORD life_create(void *p) {
    struct life *l = p;
    l->cur = cim_mk_cursor_ii(16, 16, 0, 0);
    l->menu = U(CreateMenu)();
    l->icon = cim_mk_icon(16, 16, 0xff, 0);
    return 0;
}
TEST(cursor_icon_menu_life_objects_outlive_creating_thread) {
    CIM_NEED();
    struct life l = {0};
    k_run_gui_thread(life_create, &l);
    if (!l.cur || !l.menu || !l.icon) { t_skip("creation failed"); return; }
    /* menus and cursors are process-owned (owner == pid), unlike windows/hooks which die with their thread */
    CHECK(cim_entry_is(l.cur, CT_CURSOR));
    CHECK(cim_entry_is(l.menu, CT_MENU));
    CHECK(cim_entry_is(l.icon, CT_CURSOR));
    CHECK(U(IsMenu)((HMENU)l.menu));
    ICONINFO ii;
    CHECK(U(GetIconInfo)((HICON)l.cur, &ii));
    U(DeleteObject)(ii.hbmMask); if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
    CHECK(U(DestroyIcon)((HICON)l.cur));
    CHECK(U(DestroyIcon)((HICON)l.icon));
    CHECK(U(DestroyMenu)((HMENU)l.menu));
}
static DWORD life_destroy(void *p) {
    struct life *l = p;
    SetLastError(0);
    l->cur = (HANDLE)(ULONG_PTR)(U(DestroyIcon)((HICON)l->cur) ? 1 : 0);
    l->menu = (HANDLE)(ULONG_PTR)(U(DestroyMenu)((HMENU)l->menu) ? 1 : 0);
    return 0;
}
TEST(cursor_icon_menu_life_other_thread_may_destroy_process_objects) {
    CIM_GUI();
    struct life l = {0};
    l.cur = cim_mk_icon(16, 16, 0xff, 0);
    l.menu = U(CreateMenu)();
    if (!l.cur || !l.menu) { t_skip("creation failed"); return; }
    HANDLE c = l.cur, m = l.menu;
    k_run_gui_thread(life_destroy, &l);
    /* NtUserDestroyCursor compares the cursor owner with the *process*, the destroy path for menus has no thread rule */
    EXPECT_EQ(l.cur, 1);
    EXPECT_EQ(l.menu, 1);
    CHECK(!U(IsMenu)((HMENU)m));
    ICONINFO ii; CHECK(!U(GetIconInfo)((HICON)c, &ii));
}
static DWORD life_menu_user(void *p) {
    struct life *l = p;
    /* a menu created by another thread can be used and modified from this one */
    SetLastError(0);
    l->icon = (HANDLE)(ULONG_PTR)(U(AppendMenuW)((HMENU)l->menu, MF_STRING, 9, L"t") ? 1 : 0);
    l->cur = (HANDLE)(ULONG_PTR)U(GetMenuItemCount)((HMENU)l->menu);
    return 0;
}
TEST(cursor_icon_menu_life_menu_usable_from_other_thread) {
    CIM_GUI();
    struct life l = {0};
    l.menu = U(CreatePopupMenu)();
    if (!l.menu) { t_skip("no menu"); return; }
    HMENU m = (HMENU)l.menu;
    k_run_gui_thread(life_menu_user, &l);
    EXPECT_EQ(l.icon, 1);
    EXPECT_EQ(l.cur, 1);
    EXPECT_EQ(U(GetMenuItemCount)(m), 1);
    U(DestroyMenu)(m);
}
