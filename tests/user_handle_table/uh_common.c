#include "uh.h"

static char *s_ahe;
static int s_ready = -1;

int uh_ready(void) {
    if (s_ready >= 0) return s_ready;
    s_ready = 0;
    if (!U(IsWindow) || !U(GetDesktopWindow) || !U(CreateWindowExW)) return 0;
    U(GetDesktopWindow)(); /* first win32k syscall converts the thread to a GUI thread */
    struct k_user_shared_info si;
    if (!k_shared_info(&si)) { t_note("user32!gSharedInfo not readable"); return 0; }
    if (!si.psi || !si.aheList) { t_note("gSharedInfo psi/aheList NULL"); return 0; }
    if (si.heEntrySize != UH_ESZ) { t_note("HeEntrySize=0x%lx (expected 0x20 on 19041)", si.heEntrySize); return 0; }
    unsigned long long n = 0;
    if (!k_try_read((char *)si.psi + 8, &n, 8) || n == 0 || n > 0xfffe) { t_note("cHandleEntries=%llu not plausible", n); return 0; }
    uh_entry_t e;
    if (!k_try_read(si.aheList, &e, sizeof e)) return 0;
    s_ahe = (char *)si.aheList;
    s_ready = 1;
    return 1;
}
unsigned long long uh_n(void) {
    struct k_user_shared_info si; unsigned long long n = 0;
    if (!k_shared_info(&si) || !si.psi) return 0;
    k_try_read((char *)si.psi + 8, &n, 8);
    return n;
}
bool uh_read(unsigned idx, uh_entry_t *e) {
    if (!s_ahe || idx >= uh_n()) return false;
    return k_try_read(s_ahe + (size_t)idx * UH_ESZ, e, sizeof *e);
}
bool uh_read_h(HANDLE h, uh_entry_t *e) { return uh_read(UH_IDX(h), e); }
uh_entry_t *uh_snap(unsigned *n_out) {
    unsigned n = (unsigned)uh_n();
    if (!s_ahe || !n) return NULL;
    uh_entry_t *buf = malloc((size_t)n * UH_ESZ);
    if (!buf) return NULL;
    for (unsigned i = 0; i < n; i += 1024) {
        unsigned c = n - i < 1024 ? n - i : 1024;
        if (!k_try_read(s_ahe + (size_t)i * UH_ESZ, buf + i, (size_t)c * UH_ESZ)) { free(buf); return NULL; }
    }
    if (n_out) *n_out = n;
    return buf;
}
unsigned short uh_next_uniq(unsigned short u) {
 /* on free: uniq + 1; stored value is 1 if == -1, else the old value */
    unsigned short n = (unsigned short)(u + 1);
    return n == 0xffff ? 1 : n;
}
HANDLE uh_forge(HANDLE h, unsigned hi) { return (HANDLE)(ULONG_PTR)((UH_IDX(h)) | ((ULONG_PTR)(hi & 0xffff) << 16)); }

int uh_counted_type(int t) {
    /* type flags (win32kbase type table, word at +4): bits 0|1 set => thread/process owned and
 * charged against the per-process handle quota. */
    switch (t) {
    case 1: case 2: case 3: case 4: case 5: case 7: case 8: case 9: case 10: case 11:
    case 15: case 16: case 17: case 18: case 20: case 21: case 23: return 1;
    default: return 0;
    }
}
static int my_ids(DWORD *ids, int max) {
    int n = 0;
    ids[n++] = GetCurrentProcessId();
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) { ids[n++] = GetCurrentThreadId(); return n; }
    THREADENTRY32 te; te.dwSize = sizeof te;
    if (Thread32First(s, &te)) do {
        if (te.th32OwnerProcessID == GetCurrentProcessId() && n < max) ids[n++] = te.th32ThreadID;
    } while (Thread32Next(s, &te));
    CloseHandle(s);
    return n;
}
unsigned uh_scan_owned(int type) {
    unsigned n = 0; uh_entry_t *t = uh_snap(&n);
    if (!t) return 0;
    DWORD ids[512]; int ni = my_ids(ids, 512);
    unsigned cnt = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!t[i].type) continue;
        if (type >= 0 ? t[i].type != type : !uh_counted_type(t[i].type)) continue;
        for (int k = 0; k < ni; k++) if (t[i].owner == ids[k]) { cnt++; break; }
    }
    free(t);
    return cnt;
}
DWORD uh_gr(DWORD flag) { return U(GetGuiResources)(GetCurrentProcess(), flag); }
int uh_ime_enabled(void) {
    struct k_user_shared_info si; unsigned long f = 0;
    if (!k_shared_info(&si) || !si.psi) return 0;
    k_try_read_u32(si.psi, &f);
    return (f & 4) != 0;
}
void *uh_client_ptr(HANDLE h) { return k_wnd_ptr((HWND)h); }

const WCHAR *uh_class(void) {
    static WCHAR name[64];
    if (!name[0]) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        _snwprintf(name, 64, L"win32kt_uh_%lu", GetCurrentProcessId());
        wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = name;
        if (!U(RegisterClassExW)(&wc)) name[0] = 0;
    }
    return name[0] ? name : NULL;
}
HWND uh_make_top(void) {
    const WCHAR *c = uh_class();
    if (!c) return NULL;
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, c, L"uh", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
HWND uh_make_child(HWND parent) {
    const WCHAR *c = uh_class();
    if (!c || !parent) return NULL;
    return U(CreateWindowExW)(0, c, L"uhc", WS_CHILD, 0, 0, 1, 1, parent, NULL, GetModuleHandleW(NULL), NULL);
}
HWND uh_parent;
void uh_setup_parent(void) { uh_parent = uh_make_top(); }
void uh_teardown_parent(void) { if (uh_parent) U(DestroyWindow)(uh_parent); uh_parent = NULL; }

/* ---- creators / destroyers ---- */
static HANDLE c_wtop(void) { return uh_make_top(); }
static HANDLE c_wchild(void) { return uh_make_child(uh_parent); }
static BOOL d_window(HANDLE h) { return U(DestroyWindow)((HWND)h); }
static BOOL v_window(HANDLE h) { return U(IsWindow)((HWND)h); }
static HANDLE c_menu(void) { return U(CreateMenu)(); }
static HANDLE c_popup(void) { return U(CreatePopupMenu)(); }
static BOOL d_menu(HANDLE h) { return U(DestroyMenu)((HMENU)h); }
static BOOL v_menu(HANDLE h) { return U(IsMenu)((HMENU)h); }
static HANDLE c_icon(void) {
    static BYTE andb[32], xorb[32];
    memset(andb, 0xff, sizeof andb);
    return U(CreateIcon)(GetModuleHandleW(NULL), 16, 16, 1, 1, andb, xorb);
}
static BOOL d_icon(HANDLE h) { return U(DestroyIcon)((HICON)h); }
static HANDLE c_hdwp(void) { return U(BeginDeferWindowPos)(1); }
static BOOL d_hdwp(HANDLE h) { return U(EndDeferWindowPos)((HDWP)h); }
static LRESULT CALLBACK hookproc(int c, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, c, w, l); }
static HANDLE c_hook(void) { return U(SetWindowsHookExW)(WH_GETMESSAGE, hookproc, NULL, GetCurrentThreadId()); }
static BOOL d_hook(HANDLE h) { return U(UnhookWindowsHookEx)((HHOOK)h); }
static HANDLE c_accel(void) {
    ACCEL a __attribute__((aligned(4))); memset(&a, 0, sizeof a); /* the kernel rejects a 2-aligned array */
    a.fVirt = FVIRTKEY | FCONTROL; a.key = 'A'; a.cmd = 1000;
    return U(CreateAcceleratorTableW)(&a, 1);
}
static BOOL d_accel(HANDLE h) { return U(DestroyAcceleratorTable)((HACCEL)h); }
static void CALLBACK weproc(HWINEVENTHOOK h, DWORD ev, HWND w, LONG o, LONG c, DWORD t, DWORD tm) {}
static HANDLE c_winevent(void) {
    /* out-of-context, skip our own process: no callback is ever delivered */
    return U(SetWinEventHook)(0x8012, 0x8012, NULL, weproc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}
static BOOL d_winevent(HANDLE h) { return U(UnhookWinEvent)((HWINEVENTHOOK)h); }
static HANDLE c_imc(void) {
    if (!uh_ime_enabled()) return NULL;
    HIMC (WINAPI *f)(void) = (HIMC (WINAPI *)(void))t_proc_in("imm32.dll", "ImmCreateContext");
    return f ? f() : NULL;
}
static BOOL d_imc(HANDLE h) {
    BOOL (WINAPI *f)(HIMC) = (BOOL (WINAPI *)(HIMC))t_proc_in("imm32.dll", "ImmDestroyContext");
    return f ? f((HIMC)h) : FALSE;
}

/* name,type,owner,desk,exact,forge_ok,destroy_ok,err,create,destroy,valid,setup,teardown */
const struct uh_desc uh_d_wtop     = { "window",     UT_WINDOW,        UH_OWN_THREAD,  1, 0, 1, 1, 0x578, c_wtop,     d_window,  v_window, NULL, NULL };
const struct uh_desc uh_d_wchild   = { "child",      UT_WINDOW,        UH_OWN_THREAD,  1, 1, 1, 1, 0x578, c_wchild,   d_window,  v_window, uh_setup_parent, uh_teardown_parent };
const struct uh_desc uh_d_menu     = { "menu",       UT_MENU,          UH_OWN_PROCESS, 1, 1, 1, 1, 0x579, c_menu,     d_menu,    v_menu,   NULL, NULL };
const struct uh_desc uh_d_popup    = { "popup",      UT_MENU,          UH_OWN_PROCESS, 1, 1, 1, 1, 0x579, c_popup,    d_menu,    v_menu,   NULL, NULL };
const struct uh_desc uh_d_icon     = { "icon",       UT_CURSOR,        UH_OWN_PROCESS, 0, 1, 1, 1, 0x57a, c_icon,     d_icon,    NULL,     NULL, NULL };
const struct uh_desc uh_d_hdwp     = { "hdwp",       UT_SETWINDOWPOS,  UH_OWN_THREAD,  0, 1, 1, 0, 0x57d, c_hdwp,     d_hdwp,    NULL,     NULL, NULL };
const struct uh_desc uh_d_hook     = { "hook",       UT_HOOK,          UH_OWN_THREAD,  0, 1, 1, 1, 0x57c, c_hook,     d_hook,    NULL,     NULL, NULL };
const struct uh_desc uh_d_accel    = { "accel",      UT_ACCEL,         UH_OWN_PROCESS, 0, 1, 1, 1, 0x57b, c_accel,    d_accel,   NULL,     NULL, NULL };
const struct uh_desc uh_d_winevent = { "winevent",   UT_WINEVENT,      UH_OWN_THREAD,  0, 1, 1, 1, 6,     c_winevent, d_winevent,NULL,     NULL, NULL };
const struct uh_desc uh_d_imc      = { "imc",        UT_INPUTCONTEXT,  UH_OWN_THREAD,  1, 0, 1, 0, 0,     c_imc,      d_imc,     NULL,     NULL, NULL };
const struct uh_desc *const uh_descs[] = { &uh_d_wtop, &uh_d_wchild, &uh_d_menu, &uh_d_popup, &uh_d_icon, &uh_d_hdwp, &uh_d_hook, &uh_d_accel, &uh_d_winevent, &uh_d_imc, NULL };
