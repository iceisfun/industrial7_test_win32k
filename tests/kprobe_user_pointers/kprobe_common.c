/* kprobe_user_pointers/kprobe_common.c
 *
 * Helper implementations shared by this area's other files: the "did a user-mode exception
 * happen" VEH, the committed+guard-page allocator, and small fixtures (window/atom/clipboard
 * format/DC-with-a-path/bitmap) built only from ordinary, already-covered user32/gdi32 APIs so
 * none of them depend on the Nt* stubs this area is probing. Not itself a coverage area - like
 * core_k_helpers, it exists so the real test files don't each reinvent this plumbing.
 */
#include "kprobe.h"

/* ---------------------------------------------------------------------------------------------
 * VEH: layered in front of the harness's own (t.c installs its with AddVectoredExceptionHandler
 * too; the most-recently-added handler runs first, so ours - installed from a TEST() body, i.e.
 * strictly after t.c's install in main() - sees every exception before t.c's crash-catcher does).
 * We only count and continue the search; t.c's handler still does the actual longjmp recovery if
 * something truly faults in user mode, so a bug here still fails the test and the run continues.
 * ------------------------------------------------------------------------------------------- */
static volatile unsigned g_veh_hits;
static void *g_veh_handle;

static LONG CALLBACK kp_veh(PEXCEPTION_POINTERS ep) {
    (void)ep;
    g_veh_hits++;
    return EXCEPTION_CONTINUE_SEARCH;
}
void kp_veh_install(void) {
    if (!g_veh_handle) g_veh_handle = AddVectoredExceptionHandler(1, kp_veh);
}
void kp_veh_reset(void) { kp_veh_install(); g_veh_hits = 0; }
unsigned kp_veh_hits(void) { return g_veh_hits; }

/* ---------------------------------------------------------------------------------------------
 * Guard-page allocator. Two pages reserved together (so they are adjacent with nothing else able
 * to land between them), first page committed read/write, second page left reserved-only (no
 * PAGE_NOACCESS commit needed - an uncommitted reserved page already has no mapping at all, which
 * is what the task's "reserved/PROT_NONE" wording calls for; VirtualQuery below just confirms
 * it). `buf` is placed so buf+usable lands exactly on the page boundary.
 * ------------------------------------------------------------------------------------------- */
bool kp_guard_alloc(size_t usable, struct kp_guard *out) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    size_t page = si.dwPageSize;
    if (usable == 0 || usable > page) return false; /* this helper only needs small (<=4K) fixed-size buffers */
    void *base = VirtualAlloc(NULL, page * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!base) return false;
    void *p1 = VirtualAlloc(base, page, MEM_COMMIT, PAGE_READWRITE);
    if (!p1) { VirtualFree(base, 0, MEM_RELEASE); return false; }
    out->base = base;
    out->buf = (char *)base + (page - usable);
    out->usable = usable;
    return true;
}
void kp_guard_free(struct kp_guard *g) { if (g->base) { VirtualFree(g->base, 0, MEM_RELEASE); g->base = NULL; } }
void kp_guard_fill(struct kp_guard *g, unsigned char pattern) { memset(g->buf, pattern, g->usable); }
bool kp_guard_untouched(struct kp_guard *g, size_t off, size_t len, unsigned char pattern) {
    unsigned char *p = (unsigned char *)g->buf + off;
    for (size_t i = 0; i < len; i++) if (p[i] != pattern) return false;
    return true;
}

/* ---------------------------------------------------------------------------------------------
 * Fixtures
 * ------------------------------------------------------------------------------------------- */
HWND kp_fixture_window(void) { return k_make_window("kprobe"); }

ATOM kp_fixture_atom(const wchar_t *name) {
    typedef ATOM (WINAPI *AddAtomW_t)(LPCWSTR);
    AddAtomW_t p = U(AddAtomW);
    if (!p) return 0;
    return p(name);
}

UINT kp_fixture_clip_format(void) {
    wchar_t name[64];
    swprintf(name, 64, L"win32kt_kprobe_%lu_%lu", GetCurrentProcessId(), GetTickCount());
    typedef UINT (WINAPI *RegisterClipboardFormatW_t)(LPCWSTR);
    RegisterClipboardFormatW_t p = U(RegisterClipboardFormatW);
    if (!p) return 0;
    return p(name);
}

HDC kp_fixture_dc_with_path(void) {
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    if (!dc) return NULL;
    if (!U(BeginPath) || !U(MoveToEx) || !U(LineTo) || !U(EndPath)) { U(DeleteDC)(dc); return NULL; }
    U(BeginPath)(dc);
    U(MoveToEx)(dc, 0, 0, NULL);
    U(LineTo)(dc, 10, 0);
    U(LineTo)(dc, 10, 10);
    if (!U(EndPath)(dc)) { U(DeleteDC)(dc); return NULL; }
    return dc;
}

HBITMAP kp_fixture_bitmap(int w, int h) {
    if (!U(CreateBitmap)) return NULL;
    return U(CreateBitmap)(w, h, 1, 1, NULL); /* 1 plane, 1 bpp: byte-size is trivial to predict */
}

DWORD kp_fixture_bitmap_bits_size(HBITMAP bmp) {
    /* GetBitmapBits(bmp, 0, NULL) -> gdi32full forwards straight into NtGdiGetBitmapBits(bmp,0,0),
     * whose analysis (see kprobe_guardpage.c header) computes the real size via the bitmap-bits path
     * and returns it unconditionally when the output pointer argument is 0/NULL - this is the
     * documented "query the size first" protocol, not a guess. */
    if (!U(GetBitmapBits)) return 0;
    return U(GetBitmapBits)(bmp, 0, NULL);
}

/* ---- sanity self-tests for this file's own helpers, same spirit as core_k_helpers ---- */
TEST(kprobe_user_pointers_self_guard_alloc_layout) {
    struct kp_guard g;
    if (!kp_guard_alloc(16, &g)) { t_skip("VirtualAlloc(2 pages) failed"); return; }
    kp_guard_fill(&g, 0xAB);
    CHECK(kp_guard_untouched(&g, 0, 16, 0xAB));
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T n = VirtualQuery((char *)g.buf + g.usable, &mbi, sizeof mbi);
    CHECKF(n != 0 && mbi.State == MEM_RESERVE,
           "byte right after the usable region must be on the unmapped (MEM_RESERVE, no commit) "
           "guard page, got state=0x%lx", n ? mbi.State : 0);
    kp_guard_free(&g);
}
TEST(kprobe_user_pointers_self_veh_counts_only_during_probe) {
    kp_veh_reset();
    CHECK(kp_veh_hits() == 0);
    /* No fault triggered here - just confirms reset/read round-trip before other files trust it. */
    CHECK(kp_veh_hits() == 0);
}
