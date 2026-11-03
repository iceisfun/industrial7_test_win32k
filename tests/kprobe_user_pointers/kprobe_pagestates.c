/* kprobe_user_pointers/kprobe_pagestates.c
 *
 * User addresses that are canonical and below MmUserProbeAddress (so the range clamp/ProbeFor*
 * range test passes) but whose page is not usable: MEM_RESERVE only, committed PAGE_NOACCESS, and
 * committed PAGE_READONLY (a write destination that is mapped but not writable). The kernel's
 * guarded access (ProbeForWrite followed by the store inside __try/__except, per the analysis of
 * each syscall cited in kprobe_nullreq.c / kprobe_guardpage.c) must turn the fault into the
 * syscall's failure return; nothing may surface as a user-mode exception here (kp_veh_hits()).
 * Table-driven: one TEST per page state, looping over every syscall in the area.
 */
#include "kprobe.h"

enum kp_page { KPG_RESERVED, KPG_NOACCESS, KPG_READONLY, KPG_COUNT };
static const char *const kp_page_name[KPG_COUNT] = { "reserved-only", "committed-PAGE_NOACCESS", "committed-PAGE_READONLY" };

static void *kp_page_make(enum kp_page k) {
    void *p = VirtualAlloc(NULL, 4096, k == KPG_RESERVED ? MEM_RESERVE : MEM_RESERVE | MEM_COMMIT,
                           k == KPG_READONLY ? PAGE_READONLY : PAGE_NOACCESS);
    return p;
}

enum kp_sys { KS_WINTEXT, KS_CLASSNAME, KS_ATOMNAME, KS_KBLAYOUT, KS_CLIPNAME, KS_KBSTATE,
              KS_TEXTMETRICS, KS_BITMAPBITS, KS_GETPATH_PTS, KS_GETPATH_TYPES, KS_EXTGETOBJ, KS_COUNT };
static const char *const kp_sys_name[KS_COUNT] = {
    "NtUserInternalGetWindowText", "NtUserGetClassName(inner)", "NtUserGetAtomName(inner)",
    "NtUserGetKeyboardLayoutName(inner)", "NtUserGetClipboardFormatName", "NtUserGetKeyboardState",
    "NtGdiGetTextMetricsW", "NtGdiGetBitmapBits", "NtGdiGetPath(pts)", "NtGdiGetPath(types)",
    "NtGdiExtGetObjectW" };

struct kp_fix { HWND hwnd; ATOM atom; UINT fmt; HDC dc; HDC pathdc; int pathcount; HBITMAP bmp; };

static bool kp_fix_make(struct kp_fix *f) {
    memset(f, 0, sizeof *f);
    f->hwnd = kp_fixture_window();
    f->atom = kp_fixture_atom(L"win32kt_kprobe_pg_atom");
    f->fmt = kp_fixture_clip_format();
    f->dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    f->pathdc = kp_fixture_dc_with_path();
    f->bmp = kp_fixture_bitmap(4, 4);
    if (f->pathdc) {
        typedef int (WINAPI *gp_t)(HDC, POINT *, BYTE *, int);
        gp_t gp = (gp_t)t_proc_in("win32u.dll", "NtGdiGetPath");
        f->pathcount = gp ? gp(f->pathdc, NULL, NULL, 0) : 0;
    }
    return true; /* each probe checks the fixture it needs */
}
static void kp_fix_free(struct kp_fix *f) {
    if (f->hwnd) U(DestroyWindow)(f->hwnd);
    if (f->atom) U(GlobalDeleteAtom)(f->atom);
    if (f->dc) U(DeleteDC)(f->dc);
    if (f->pathdc) U(DeleteDC)(f->pathdc);
    if (f->bmp) U(DeleteObject)(f->bmp);
}

/* Returns 1 = failed cleanly, 0 = reported success, -1 = not applicable here (skip). */
static int kp_probe(enum kp_sys s, struct kp_fix *f, void *addr, unsigned *hits) {
    int r = -1;
    *hits = 0;
    if ((s == KS_WINTEXT || s == KS_CLASSNAME) && !f->hwnd) return -1;
    if (s == KS_ATOMNAME && !f->atom) return -1;
    if (s == KS_CLIPNAME && !f->fmt) return -1;
    if (s == KS_TEXTMETRICS && !f->dc) return -1;
    if ((s == KS_BITMAPBITS || s == KS_EXTGETOBJ) && !f->bmp) return -1;
    if ((s == KS_GETPATH_PTS || s == KS_GETPATH_TYPES) && !f->pathdc) return -1;
    kp_veh_reset();
    switch (s) {
    case KS_WINTEXT: {
        typedef int (WINAPI *fn_t)(HWND, LPWSTR, int);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserInternalGetWindowText");
        if (!p) return -1;
        r = p(f->hwnd, (LPWSTR)addr, 8) == 0; break; }
    case KS_CLASSNAME: {
        typedef BOOL (WINAPI *fn_t)(HWND, int, struct kp_strbuf *);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClassName");
        struct kp_strbuf d = { 0, 64, 0, addr };
        if (!p) return -1;
        r = !p(f->hwnd, 0, &d); break; }
    case KS_ATOMNAME: {
        typedef BOOL (WINAPI *fn_t)(ATOM, struct kp_strbuf *);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetAtomName");
        struct kp_strbuf d = { 0, 64, 0, addr };
        if (!p) return -1;
        r = !p(f->atom, &d); break; }
    case KS_KBLAYOUT: {
        typedef BOOL (WINAPI *fn_t)(struct kp_strbuf *);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardLayoutName");
        struct kp_strbuf d = { 0, 64, 0, addr };
        if (!p || !k_has_interactive_desktop()) return -1;
        r = !p(&d); break; }
    case KS_CLIPNAME: {
        typedef BOOL (WINAPI *fn_t)(UINT, LPWSTR, int);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClipboardFormatName");
        if (!p) return -1;
        r = !p(f->fmt, (LPWSTR)addr, 32); break; }
    case KS_KBSTATE: {
        typedef BOOL (WINAPI *fn_t)(BYTE *);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardState");
        if (!p) return -1;
        r = !p((BYTE *)addr); break; }
    case KS_TEXTMETRICS: {
        typedef int (WINAPI *fn_t)(HDC, void *, UINT);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetTextMetricsW");
        if (!p || t_is_wine()) return -1; /* Wine can leave its GDI lock stuck, see kprobe_nullreq.c */
        r = p(f->dc, addr, 0x44) == 0; break; }
    case KS_BITMAPBITS: {
        typedef DWORD (WINAPI *fn_t)(HBITMAP, DWORD, LPVOID);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetBitmapBits");
        DWORD sz = kp_fixture_bitmap_bits_size(f->bmp);
        if (!p || !sz) return -1;
        r = p(f->bmp, sz, addr) == 0; break; }
    case KS_GETPATH_PTS:
    case KS_GETPATH_TYPES: {
        typedef int (WINAPI *fn_t)(HDC, POINT *, BYTE *, int);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetPath");
        POINT pts[64]; BYTE types[64];
        if (!p || f->pathcount <= 0 || f->pathcount > 64) return -1;
        if (s == KS_GETPATH_PTS) r = p(f->pathdc, (POINT *)addr, types, f->pathcount) != f->pathcount;
        else r = p(f->pathdc, pts, (BYTE *)addr, f->pathcount) != f->pathcount;
        break; }
    case KS_EXTGETOBJ: {
        typedef UINT (WINAPI *fn_t)(HGDIOBJ, int, LPVOID);
        fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiExtGetObjectW");
        if (!p) return -1;
        r = p(f->bmp, sizeof(BITMAP), addr) == 0; break; }
    default: return -1;
    }
    *hits = kp_veh_hits();
    return r;
}

/* One TEST per page state, every syscall inside. */
static void kp_run_state(enum kp_page st) {
    struct kp_fix f;
    if (!kp_fix_make(&f)) { t_skip("fixtures unavailable"); kp_fix_free(&f); return; }
    void *pg = kp_page_make(st);
    if (!pg) { t_skip("VirtualAlloc failed"); kp_fix_free(&f); return; }
    for (int s = 0; s < KS_COUNT; s++) {
        unsigned hits = 0;
        int r = kp_probe((enum kp_sys)s, &f, pg, &hits);
        if (r < 0) { t_note("%s not applicable here", kp_sys_name[s]); continue; }
        CHECKF(r == 1, "%s with a %s buffer must fail, not report success", kp_sys_name[s], kp_page_name[st]);
        CHECKF(hits == 0, "%s with a %s buffer raised %u user-mode exception(s)", kp_sys_name[s], kp_page_name[st], hits);
    }
    VirtualFree(pg, 0, MEM_RELEASE);
    kp_fix_free(&f);
}
TEST(kprobe_user_pointers_pagestate_reserved_only_all_syscalls)   { kp_run_state(KPG_RESERVED); }
TEST(kprobe_user_pointers_pagestate_noaccess_all_syscalls)        { kp_run_state(KPG_NOACCESS); }
TEST(kprobe_user_pointers_pagestate_readonly_all_syscalls)        { kp_run_state(KPG_READONLY); }

/* One TEST per syscall, every page state inside, plus a buffer straddling the end of a committed
 * page into the reserved one (start 1 byte before the page end, so any multi-byte write crosses). */
static void kp_run_sys(enum kp_sys s) {
    struct kp_fix f;
    if (!kp_fix_make(&f)) { t_skip("fixtures unavailable"); kp_fix_free(&f); return; }
    for (int st = 0; st < KPG_COUNT; st++) {
        void *pg = kp_page_make((enum kp_page)st);
        if (!pg) continue;
        unsigned hits = 0;
        int r = kp_probe(s, &f, pg, &hits);
        if (r < 0) { t_skip("%s not applicable here", kp_sys_name[s]); VirtualFree(pg, 0, MEM_RELEASE); break; }
        CHECKF(r == 1, "%s: %s buffer must fail", kp_sys_name[s], kp_page_name[st]);
        CHECKF(hits == 0, "%s: %s buffer raised %u exception(s)", kp_sys_name[s], kp_page_name[st], hits);
        VirtualFree(pg, 0, MEM_RELEASE);
    }
    struct kp_guard g;
    if (kp_guard_alloc(1, &g)) {
        unsigned hits = 0;
        kp_guard_fill(&g, 0xCC);
        int r = kp_probe(s, &f, g.buf, &hits);
        if (r >= 0) {
            CHECKF(r == 1, "%s: 1-byte buffer ending at the guard page must fail", kp_sys_name[s]);
            CHECKF(hits == 0, "%s: straddling buffer raised %u exception(s)", kp_sys_name[s], hits);
        }
        kp_guard_free(&g);
    }
    kp_fix_free(&f);
}
#define KP_SYS_TEST(n, id) TEST(kprobe_user_pointers_pagestates_##n) { kp_run_sys(id); }
KP_SYS_TEST(internalgetwindowtext, KS_WINTEXT)
KP_SYS_TEST(getclassname, KS_CLASSNAME)
KP_SYS_TEST(getatomname, KS_ATOMNAME)
KP_SYS_TEST(getkeyboardlayoutname, KS_KBLAYOUT)
KP_SYS_TEST(getclipboardformatname, KS_CLIPNAME)
KP_SYS_TEST(getkeyboardstate, KS_KBSTATE)
KP_SYS_TEST(gettextmetricsw, KS_TEXTMETRICS)
KP_SYS_TEST(getbitmapbits, KS_BITMAPBITS)
KP_SYS_TEST(getpath_pts, KS_GETPATH_PTS)
KP_SYS_TEST(getpath_types, KS_GETPATH_TYPES)
KP_SYS_TEST(extgetobjectw, KS_EXTGETOBJ)
