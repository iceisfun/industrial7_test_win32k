#include "cim.h"

static char *s_ahe;
static int s_ready = -1;

int cim_gui(void) {
    static int ok = -1;
    if (ok >= 0) return ok;
    ok = 0;
    if (!U(IsWindow) || !U(GetDesktopWindow) || !U(CreateWindowExW) || !U(CreateMenu)) return 0;
    U(GetDesktopWindow)();
    ok = 1;
    return 1;
}
int cim_ready(void) {
    if (s_ready >= 0) return s_ready;
    s_ready = 0;
    if (!U(IsWindow) || !U(GetDesktopWindow) || !U(CreateWindowExW) || !U(CreateMenu)) return 0;
    U(GetDesktopWindow)(); /* first win32k syscall converts the thread to a GUI thread */
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi || !si.aheList) { t_note("gSharedInfo not readable"); return 0; }
    if (si.heEntrySize != CIM_ESZ) { t_note("HeEntrySize=0x%lx (expected 0x20)", si.heEntrySize); return 0; }
    unsigned long long n = 0;
    if (!k_try_read((char *)si.psi + 8, &n, 8) || n == 0 || n > 0xfffe) { t_note("cHandleEntries=%llu not plausible", n); return 0; }
    cim_he e;
    if (!k_try_read(si.aheList, &e, 0x20)) return 0;
    s_ahe = (char *)si.aheList;
    s_ready = 1;
    return 1;
}
unsigned long long cim_nentries(void) {
    struct k_user_shared_info si; unsigned long long n = 0;
    if (!k_shared_info(&si) || !si.psi) return 0;
    k_try_read((char *)si.psi + 8, &n, 8);
    return n;
}
bool cim_he_read(HANDLE h, cim_he *e) {
    unsigned i = CIM_IDX(h);
    unsigned char raw[0x20];
    if (!s_ahe || i >= cim_nentries()) return false;
    if (!k_try_read(s_ahe + (size_t)i * CIM_ESZ, raw, sizeof raw)) return false;
    memcpy(&e->delta, raw, 8); memcpy(&e->owner, raw + 8, 8); memcpy(&e->f10, raw + 0x10, 8);
    e->type = raw[0x18]; e->flags = raw[0x19]; memcpy(&e->uniq, raw + 0x1a, 2);
    return true;
}
bool cim_entry_is(HANDLE h, int type) {
    cim_he e;
    return cim_he_read(h, &e) && e.type == type && e.uniq == CIM_UNIQ(h) && !(e.flags & 1);
}
unsigned cim_count_owned(int type) {
    unsigned long long n = cim_nentries();
    unsigned cnt = 0;
    for (unsigned long long i = 0; i < n; i++) {
        cim_he e;
        if (!cim_he_read((HANDLE)(ULONG_PTR)i, &e)) continue;
        if (e.type == type && !(e.flags & 1) && e.owner == GetCurrentProcessId()) cnt++;
    }
    return cnt;
}
DWORD cim_gr(DWORD flag) { return U(GetGuiResources)(GetCurrentProcess(), flag); }
HANDLE cim_forge(HANDLE h, unsigned hi) { return (HANDLE)(ULONG_PTR)(CIM_IDX(h) | ((ULONG_PTR)(hi & 0xffff) << 16)); }
unsigned short cim_next_uniq(unsigned short u) { unsigned short n = (unsigned short)(u + 1); return n == 0xffff ? 1 : n; } /* handle allocator reuse rule */

const WCHAR *cim_class(void) {
    static WCHAR name[64];
    if (!name[0]) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        wsprintfW(name, L"win32kt_cim_%lu", GetCurrentProcessId());
        wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = name;
        if (!U(RegisterClassExW)(&wc)) name[0] = 0;
    }
    return name[0] ? name : NULL;
}
HWND cim_make_top(void) {
    const WCHAR *c = cim_class();
    if (!c) return NULL;
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, c, L"cim", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
HWND cim_make_child(HWND parent) {
    const WCHAR *c = cim_class();
    if (!c || !parent) return NULL;
    return U(CreateWindowExW)(0, c, L"cimc", WS_CHILD, 0, 0, 1, 1, parent, NULL, GetModuleHandleW(NULL), NULL);
}

void *cim_menu_ptr(HMENU h) { return k_wnd_ptr((HWND)h); }
bool cim_menu_read(HMENU h, struct cim_menu *m) {
    void *p = cim_menu_ptr(h);
    unsigned char b[0x40];
    if (!p || !k_try_read(p, b, sizeof b)) return false;
    memcpy(&m->handle, b, 8); memcpy(&m->self_word, b + 8, 8); memcpy(&m->menudata, b + 0x10, 8);
    memcpy(&m->hbr, b + 0x18, 8); memcpy(&m->rgitems, b + 0x20, 8);
    memcpy(&m->flags, b + 0x28, 4); memcpy(&m->citems, b + 0x2c, 4);
    memcpy(&m->helpid, b + 0x30, 4); memcpy(&m->cymax, b + 0x34, 4);
    return true;
}
bool cim_item_read(HMENU h, unsigned idx, struct cim_item *it) {
    struct cim_menu m;
    void *p = cim_menu_ptr(h);
    if (!p || !cim_menu_read(h, &m) || idx >= m.citems || !m.rgitems) return false;
    unsigned char *ip = (unsigned char *)p + (long long)(m.rgitems - m.self_word) + (size_t)idx * 0x70;
    unsigned char b[0x20];
    if (!k_try_read(ip, b, sizeof b)) return false;
    memcpy(&it->type, b, 4); memcpy(&it->state, b + 4, 4); memcpy(&it->id, b + 8, 4); memcpy(&it->sub_ptr, b + 0x10, 8);
    it->sub = NULL;
    if (it->sub_ptr) {
        void *sp = (unsigned char *)p + (long long)(it->sub_ptr - m.self_word);
        void *hv = NULL;
        if (k_try_read_ptr(sp, &hv)) it->sub = (HMENU)hv;
    }
    return true;
}

HICON cim_mk_icon(int w, int h, BYTE and_fill, BYTE xor_fill) {
    BYTE a[512], x[512];
    int stride = ((w + 15) / 16) * 2, n = stride * h;
    if (n > 512) return NULL;
    memset(a, and_fill, n); memset(x, xor_fill, n);
    return U(CreateIcon)(GetModuleHandleW(NULL), w, h, 1, 1, a, x);
}
static HBITMAP mk_bmp(int w, int h, int bpp) {
    static BYTE zero[16 * 16 * 4 * 4];
    return U(CreateBitmap)(w, h, 1, bpp, zero);
}
HICON cim_mk_cursor_ii(int w, int h, int hx, int hy) {
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    ii.fIcon = FALSE; ii.xHotspot = hx; ii.yHotspot = hy;
    ii.hbmMask = mk_bmp(w, h, 1); ii.hbmColor = mk_bmp(w, h, 32);
    HICON ic = ii.hbmMask && ii.hbmColor ? U(CreateIconIndirect)(&ii) : NULL;
    if (ii.hbmMask) U(DeleteObject)(ii.hbmMask);
    if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
    return ic;
}
HICON cim_mk_icon_ii(int w, int h, int bpp) {
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE;
    ii.hbmMask = mk_bmp(w, h, 1); ii.hbmColor = mk_bmp(w, h, bpp);
    HICON ic = ii.hbmMask && ii.hbmColor ? U(CreateIconIndirect)(&ii) : NULL;
    if (ii.hbmMask) U(DeleteObject)(ii.hbmMask);
    if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
    return ic;
}
bool cim_gdi_bitmap_ok(HBITMAP b) {
    if (!b) return false;
    if (U(GetObjectType)(b) != OBJ_BITMAP) return false;
    unsigned owner = 0;
    void *base = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &base) || !base) return true; /* no shared table here (emulator): type check only */
    struct k_gdicell c;
    if (!k_gdi_cell(b, &c)) return false;
    if (!k_try_read_u32((char *)base + (size_t)((ULONG_PTR)b & 0xffff) * GDI_CELL_SIZE + 8, (unsigned long *)&owner)) return false;
    return (owner & 0xfffffffe) == GetCurrentProcessId();
}
