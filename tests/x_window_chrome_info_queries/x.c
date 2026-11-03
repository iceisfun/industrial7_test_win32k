#include "x.h"

jmp_buf xw_jb; volatile int xw_armed; volatile unsigned long xw_exc; static int veh_on;
static LONG CALLBACK xw_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (xw_armed && ((c & 0xC0000000) == 0xC0000000 || c == 0x80000002 || c == 0x80000100) && c != 0xC0000194 && c != 0xC000013A) {
        xw_armed = 0; xw_exc = c; longjmp(xw_jb, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void xw_run_guarded(void (*f)(void)) {
    /* a win32k service issued before user32 is loaded ends the process: the thread cannot become a GUI thread */
    t_module("user32.dll"); t_module("gdi32.dll");
    if (!veh_on) { AddVectoredExceptionHandler(1, xw_veh); veh_on = 1; }
    if (!setjmp(xw_jb)) { xw_armed = 1; f(); xw_armed = 0; }
    else if (t_is_wine()) t_skip("emulator fault 0x%lx", xw_exc);
    else t_check(0, __FILE__, __LINE__, "no exception", "exception 0x%lx escaped a syscall", xw_exc);
}

void xw_fail(ULONG_PTR ret, DWORD gle, DWORD want, const char *file, int line, const char *what) {
    t_check((ret & 0xFFFFFFFF) == 0, file, line, what, "call unexpectedly returned 0x%llx", (unsigned long long)ret);
    t_check(gle == want, file, line, what, "last error %lu want %lu", gle, want);
}

HWND xw_win(DWORD style, DWORD ex, const WCHAR *cls) {
    return U(CreateWindowExW)(ex, cls, L"w", style & ~WS_VISIBLE, 10, 10, 200, 150, NULL, NULL, NULL, NULL);
}
HWND xw_child(HWND parent, DWORD style, const WCHAR *cls) {
    return U(CreateWindowExW)(0, cls, L"c", (style | WS_CHILD) & ~WS_VISIBLE, 0, 0, 50, 50, parent, NULL, NULL, NULL);
}
HWND xw_dead(void) {
    HWND w = xw_win(WS_POPUP, 0, L"STATIC");
    if (w) U(DestroyWindow)(w);
    return w ? w : (HWND)(ULONG_PTR)0x00FF0F0F;
}
void xw_kill(HWND w) { if (w) U(DestroyWindow)(w); }
int xw_untouched(const void *p, size_t n) {
    const unsigned char *b = p; for (size_t i = 0; i < n; i++) if (b[i] != XFILL) return 0;
    return 1;
}

static DWORD WINAPI other_main(LPVOID p) {
    struct xw_other *o = p;
    o->hwnd = xw_win(o->style, o->ex, o->cls);
    o->tid = GetCurrentThreadId();
    SetEvent(o->ready);
    while (WaitForSingleObject(o->stop, 0) == WAIT_TIMEOUT) k_pump(10);
    if (o->hwnd) U(DestroyWindow)(o->hwnd);
    return 0;
}
int xw_other_start(struct xw_other *o, DWORD style, DWORD ex, const WCHAR *cls) {
    memset(o, 0, sizeof *o);
    o->style = style; o->ex = ex; o->cls = cls;
    o->ready = CreateEventA(NULL, TRUE, FALSE, NULL); o->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    o->thread = CreateThread(NULL, 0, other_main, o, 0, NULL);
    if (!o->thread || WaitForSingleObject(o->ready, 10000) != WAIT_OBJECT_0 || !o->hwnd) { xw_other_stop(o); return 0; }
    return 1;
}
void xw_other_stop(struct xw_other *o) {
    if (o->stop) SetEvent(o->stop);
    if (o->thread) { WaitForSingleObject(o->thread, 10000); CloseHandle(o->thread); }
    if (o->ready) CloseHandle(o->ready);
    if (o->stop) CloseHandle(o->stop);
    memset(o, 0, sizeof *o);
}

int xw_mem_open(struct xw_mem *m, int w, int h) {
    memset(m, 0, sizeof *m);
    HDC scr = U(GetDC)(NULL);
    if (!scr) return 0;
    m->dc = U(CreateCompatibleDC)(scr);
    m->bm = m->dc ? U(CreateCompatibleBitmap)(scr, w, h) : NULL;
    U(ReleaseDC)(NULL, scr);
    if (!m->dc || !m->bm) { xw_mem_close(m); return 0; }
    m->old = U(SelectObject)(m->dc, m->bm);
    return 1;
}
void xw_mem_close(struct xw_mem *m) {
    if (m->dc && m->old) U(SelectObject)(m->dc, m->old);
    if (m->bm) U(DeleteObject)(m->bm);
    if (m->dc) U(DeleteDC)(m->dc);
    memset(m, 0, sizeof *m);
}
COLORREF xw_px(HDC dc, int x, int y) { return U(GetPixel)(dc, x, y); }
void xw_clear(HDC dc, int w, int h, COLORREF c) {
    HBRUSH b = U(CreateSolidBrush)(c); RECT r = { 0, 0, w, h };
    U(FillRect)(dc, &r, b); U(DeleteObject)(b);
}
