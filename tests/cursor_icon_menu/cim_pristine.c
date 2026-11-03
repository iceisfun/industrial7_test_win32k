/* First-ever win32k call of a process being a menu / cursor / icon API: the kernel converts the thread
 * (Win32ThreadInfo becomes non-NULL) and the object is created in the process's first handle table use.
 * Every scenario runs in a child that has not touched user32 yet. */
#include "cim.h"

static int cfails;
#define CK(c) do { int ok_ = !!(c); if (!ok_) cfails++; CHECKF(ok_, "%s", #c); } while (0)

static void *teb78(void) { void *p = (void *)1; if (!k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &p)) return (void *)1; return p; }

static int pristine_ok(void) {
    if (GetModuleHandleA("user32.dll")) { t_note("user32 already loaded in child: pristine checks skipped"); return 0; }
    CK(teb78() == NULL);
    return 1;
}
static void sc_menu(void) {
    int pr = pristine_ok();
    HMENU h = U(CreateMenu)();
    CK(h != NULL);
    if (pr) CK(teb78() != NULL);           /* NtUserCallNoParam(0) was the first win32k call: thread converted */
    if (!h) return;
    if (cim_ready()) {
        cim_he e;
        CK(cim_he_read(h, &e));
        CK(e.type == CT_MENU);
        CK(e.owner == GetCurrentProcessId());
        CK(e.uniq == CIM_UNIQ(h));
    }
    CK(U(IsMenu)(h));
    CK(U(DestroyMenu)(h));
    CK(!U(IsMenu)(h));
}
static void sc_popup(void) {
    int pr = pristine_ok();
    HMENU h = U(CreatePopupMenu)();
    CK(h != NULL);
    if (pr) CK(teb78() != NULL);
    if (!h) return;
    CK(U(AppendMenuW)(h, MF_STRING, 1, L"a"));
    CK(U(GetMenuItemCount)(h) == 1);
    CK(U(DestroyMenu)(h));
}
static void sc_menu_destroy_first(void) {
    int pr = pristine_ok();
    /* DestroyMenu is a bare Nt stub: with a bogus handle as the very first call it still converts the thread and fails cleanly */
    BOOL r; SetLastError(CIM_SENTINEL);
    r = U(DestroyMenu)((HMENU)(ULONG_PTR)0x00010002);
    CK(r == 0);
    CK(GetLastError() == E_INVALID_MENU_HANDLE);
    if (pr) CK(teb78() != NULL);
}
static void sc_cursor_load(void) {
    int pr = pristine_ok();
    HCURSOR c = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512));
    CK(c != NULL);                          /* LoadIcoCur: NtUserGetThreadState(0xe) converts the thread, then the load proceeds */
    if (pr) CK(teb78() != NULL);
    if (c && cim_ready()) CK(cim_entry_is(c, CT_CURSOR));
}
static void sc_getcursor(void) {
    int pr = pristine_ok();
    HCURSOR c = U(GetCursor)();
    if (pr) CK(teb78() != NULL);            /* NtUserGetCursor: EnterCrit converts */
    t_note("first GetCursor() = %p", c);
    if (c && cim_ready()) CK(cim_entry_is(c, CT_CURSOR));
}
static void sc_showcursor(void) {
    int pr = pristine_ok();
    int a = U(ShowCursor)(TRUE);
    if (pr) CK(teb78() != NULL);
    int b = U(ShowCursor)(FALSE);
    CK(b == a - 1);
    t_note("first ShowCursor(TRUE) = %d", a);
}
static void sc_icon_create(void) {
    int pr = pristine_ok();
    HICON h = cim_mk_icon(16, 16, 0xff, 0);
    CK(h != NULL);
    if (pr) CK(teb78() != NULL);
    if (!h) return;
    if (cim_ready()) CK(cim_entry_is(h, CT_CURSOR));
    ICONINFO ii;
    CK(U(GetIconInfo)(h, &ii));
    if (ii.hbmMask) U(DeleteObject)(ii.hbmMask);
    if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
    CK(U(DestroyIcon)(h));
}
static void sc_clip(void) {
    int pr = pristine_ok();
    RECT r = {0};
    BOOL ok = U(GetClipCursor)(&r);
    if (pr) CK(teb78() != NULL);            /* EnterSharedCrit converts as well */
    if (ok) CK(r.right > r.left && r.bottom > r.top);
    else t_note("GetClipCursor failed err %lu", GetLastError());
}
static void sc_cursorinfo(void) {
    int pr = pristine_ok();
    CURSORINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
    BOOL ok = U(GetCursorInfo)(&ci);
    if (pr) CK(teb78() != NULL);
    if (ok) CK(ci.cbSize == 0);          /* the kernel does not echo cbSize back */
    else t_note("GetCursorInfo failed err %lu", GetLastError());
}
static void sc_nt_stub_first(void) {
    int pr = pristine_ok();
    /* a raw win32u stub as the first call: NtUserGetCursor takes no arguments */
    if (!UN(NtUserGetCursor)) { t_note("no NtUserGetCursor"); return; }
    HANDLE c = UN(NtUserGetCursor)();
    if (pr) CK(teb78() != NULL);
    t_note("NtUserGetCursor first = %p", c);
}

static const struct { const char *n; void (*f)(void); } scs[] = {
    {"menu", sc_menu}, {"popup", sc_popup}, {"menu_destroy_first", sc_menu_destroy_first}, {"cursor_load", sc_cursor_load},
    {"getcursor", sc_getcursor}, {"showcursor", sc_showcursor}, {"icon_create", sc_icon_create}, {"clip", sc_clip},
    {"cursorinfo", sc_cursorinfo}, {"nt_stub_first", sc_nt_stub_first},
};
CHILD(cim_pr) {
    const char *sel = argc > 1 ? argv[1] : "";
    setvbuf(stdout, NULL, _IONBF, 0);
    for (unsigned i = 0; i < sizeof scs / sizeof *scs; i++) {
        if (strcmp(scs[i].n, sel)) continue;
        scs[i].f();
        return cfails > 250 ? 250 : cfails;
    }
    printf("  FAIL unknown scenario '%s'\n", sel);
    return 0xC0DE0002;
}

static void run(const char *sel) {
    if (t_is_wine()) { t_skip("pristine child scenarios are meaningful on Windows only"); return; }
    struct k_child_result r = k_child_run("cim_pr", sel, 60000);
    CHECKF(r.exit_code == 0, "child scenario %s exit code 0x%lx", sel, (unsigned long)r.exit_code);
}
TEST(cursor_icon_menu_pristine_createmenu_first_call) { run("menu"); }
TEST(cursor_icon_menu_pristine_createpopupmenu_first_call) { run("popup"); }
TEST(cursor_icon_menu_pristine_destroymenu_bogus_first_call) { run("menu_destroy_first"); }
TEST(cursor_icon_menu_pristine_loadcursor_first_call) { run("cursor_load"); }
TEST(cursor_icon_menu_pristine_getcursor_first_call) { run("getcursor"); }
TEST(cursor_icon_menu_pristine_showcursor_first_call) { run("showcursor"); }
TEST(cursor_icon_menu_pristine_createicon_first_call) { run("icon_create"); }
TEST(cursor_icon_menu_pristine_getclipcursor_first_call) { run("clip"); }
TEST(cursor_icon_menu_pristine_getcursorinfo_first_call) { run("cursorinfo"); }
TEST(cursor_icon_menu_pristine_nt_stub_first_call) { run("nt_stub_first"); }
