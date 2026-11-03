#include "x.h"

jmp_buf xc_jb; volatile int xc_armed; volatile unsigned long xc_exc; static int veh_on;
static LONG CALLBACK xc_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (xc_armed && ((c & 0xC0000000) == 0xC0000000 || c == 0x80000002 || c == 0x80000100) && c != 0xC0000194 && c != 0xC000013A) {
        xc_armed = 0; xc_exc = c; longjmp(xc_jb, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void xc_run_guarded(void (*f)(void)) {
    /* a win32k service issued before user32 is loaded ends the process: the thread cannot become a GUI thread */
    t_module("user32.dll"); t_module("gdi32.dll");
    if (!veh_on) { AddVectoredExceptionHandler(1, xc_veh); veh_on = 1; }
    if (!setjmp(xc_jb)) { xc_armed = 1; f(); xc_armed = 0; }
    else if (t_is_wine()) t_skip("emulator fault 0x%lx", xc_exc);
    else t_check(0, __FILE__, __LINE__, "no exception", "exception 0x%lx escaped a syscall", xc_exc);
}

void xc_fail(long long ret, DWORD gle, DWORD want, const char *file, int line, const char *what) {
    t_check((ret & 0xFFFFFFFF) == 0, file, line, what, "call unexpectedly returned 0x%llx", (unsigned long long)ret);
    t_check(gle == want, file, line, what, "last error %lu want %lu", gle, want);
}

HCURSOR xc_cursor(int w, int h, int hx, int hy, int fill) {
    BYTE andp[512], xorp[512];
    size_t n = (size_t)(w / 8) * h;
    if (n > sizeof andp) return NULL;
    memset(andp, 0xff, sizeof andp); memset(xorp, fill, sizeof xorp);
    return U(CreateCursor)(GetModuleHandleA(NULL), hx, hy, w, h, andp, xorp);
}
HCURSOR xc_stock(int idc) { return U(LoadCursorW)(NULL, MAKEINTRESOURCEW(idc)); }
HWND xc_hidden_window(void) {
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW, L"STATIC", L"x", WS_POPUP, 10, 10, 100, 80, NULL, NULL, NULL, NULL);
}
int xc_mask_dims(HCURSOR c, int *w, int *h, int *hx, int *hy) {
    ICONINFO ii; BITMAP bm;
    memset(&ii, 0, sizeof ii); memset(&bm, 0, sizeof bm);
    if (!U(GetIconInfo)(c, &ii)) return 0;
    int ok = U(GetObjectW)(ii.hbmMask, sizeof bm, &bm) != 0;
    if (w) *w = bm.bmWidth;
    if (h) *h = bm.bmHeight;
    if (hx) *hx = (int)ii.xHotspot;
    if (hy) *hy = (int)ii.yHotspot;
    if (ii.hbmMask) U(DeleteObject)(ii.hbmMask);
    if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
    return ok;
}

struct thr { DWORD (WINAPI *f)(LPVOID); void *arg; };
static DWORD WINAPI thr_main(LPVOID p) { struct thr *t = p; return t->f(t->arg); }
int xc_on_thread(DWORD (WINAPI *f)(LPVOID), void *arg) {
    struct thr t = { f, arg };
    HANDLE h = CreateThread(NULL, 0, thr_main, &t, 0, NULL);
    if (!h) return 0;
    DWORD w = WaitForSingleObject(h, 10000);
    CloseHandle(h);
    return w == WAIT_OBJECT_0;
}

BOOL CALLBACK xc_enum_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp) {
    struct xc_enum *e = (struct xc_enum *)lp;
    e->total++; e->tid = GetCurrentThreadId();
    if (e->n < XC_MAXMON) { e->m[e->n] = m; e->dc[e->n] = dc; if (rc) e->rc[e->n] = *rc; else memset(&e->rc[e->n], 0, sizeof(RECT)); e->n++; }
    if (e->stop_after && e->total >= e->stop_after) return FALSE;
    return TRUE;
}
int xc_ccd(UINT32 *np_active) {
    UINT32 p = 0, m = 0;
    if (!U(GetDisplayConfigBufferSizes)) return 0;
    if (U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, &m) != ERROR_SUCCESS) return 0;
    if (np_active) *np_active = p;
    return 1;
}
