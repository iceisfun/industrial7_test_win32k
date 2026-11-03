/* gap_09: depth tests for thinly covered win32u syscalls (clipboard listeners, window station open,
 * layered attributes, DPI queries, gesture/touch negatives, GDI Rectangle/LineTo/ScaleViewportExtEx/
 * ColorAdjustment/GlyphOutline/OutlineTextMetrics, composition capture negatives).
 * Syscall signatures are derived from the win32kfull/win32kbase analysis (see per-file comments). */
#ifndef G9_H
#define G9_H
#include "t.h"
#include "k.h"

#ifndef G9_WINE_ABORT_EXTRA
#define G9_WINE_ABORT_EXTRA
#endif
/* Wine's win32u exports every Nt* name but aborts the whole process when an unimplemented one is called;
 * UN() therefore yields NULL (-> REQUIRE skips the test) for the names listed here when running on Wine. */
static inline FARPROC g9_proc(const char *name) {
    static const char *const wine_abort[] = { "NtUserGetImeHotKey", "NtUserGetCurrentDpiInfoForWindow", "NtUserGetExtendedPointerDeviceProperty",
        "NtUserGetPrecisionTouchPadConfiguration", "NtUserGetTouchInputInfo", "NtUserGetCurrentInputMessageSource", "NtUserSetGestureConfig", "NtUserGetGestureInfo", "NtDesktopCaptureBits", "NtVisualCaptureBits",
        "NtDuplicateCompositionInputSink", G9_WINE_ABORT_EXTRA NULL };
    if (t_is_wine()) for (int i = 0; wine_abort[i]; i++) if (!strcmp(wine_abort[i], name)) return NULL;
    return t_proc(name);
}
#undef UN
#define UN(name) ((name##_pfn)g9_proc(#name))
#define G9_N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* A never-shown tool window owned by the calling thread.  A private class is registered once
 * (the system STATIC class is only guaranteed after user32's own initialisation). */
static inline LRESULT CALLBACK g9_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return U(DefWindowProcA)(h, m, w, l); }
static inline HWND g9_window(DWORD exstyle) {
    static char cls[48];
    if (!U(CreateWindowExA) || !U(RegisterClassA) || !U(DefWindowProcA)) return NULL;
    if (!cls[0]) {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wsprintfA(cls, "win32kt_gap9_%lu", GetCurrentProcessId());
        wc.lpfnWndProc = g9_wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = cls;
        /* the header is included by several translation units, each with its own cls[]: tolerate "class exists" */
        if (!U(RegisterClassA)(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { t_note("RegisterClassA failed, error %lu", GetLastError()); cls[0] = 0; return NULL; }
    }
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | exstyle, cls, "win32kt_gap9",
                                WS_POPUP, 100, 100, 50, 50, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!h) t_note("CreateWindowExA(ex=0x%lx) failed, error %lu", exstyle, GetLastError());
    return h;
}
static inline void g9_destroy(HWND h) { if (h && U(DestroyWindow)) U(DestroyWindow)(h); }

/* Runs fn(arg) on a plain worker thread (no message loop) and waits up to 5 s. */
struct g9_thr { DWORD (*fn)(void *); void *arg; DWORD ret; };
static inline DWORD WINAPI g9_thr_main(void *p) { struct g9_thr *t = (struct g9_thr *)p; t->ret = t->fn(t->arg); return 0; }
static inline DWORD g9_on_thread(DWORD (*fn)(void *), void *arg) {
    struct g9_thr t = { fn, arg, 0xDEAD0002 };
    HANDLE h = CreateThread(NULL, 0, g9_thr_main, &t, 0, NULL);
    if (!h) return 0xDEAD0000;
    if (WaitForSingleObject(h, 5000) != WAIT_OBJECT_0) { TerminateThread(h, 1); CloseHandle(h); return 0xDEAD0001; }
    CloseHandle(h);
    return t.ret;
}
/* 32bpp top-down DIB section selected into a memory DC; pixels preset to white. */
struct g9_dib { HDC dc; HBITMAP bmp, old; unsigned *bits; int w, h; };
static inline int g9_dib_new(struct g9_dib *d, int w, int h) {
    memset(d, 0, sizeof *d);
    if (!U(CreateCompatibleDC) || !U(CreateDIBSection) || !U(SelectObject)) return 0;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return 0;
    void *bits = NULL;
    d->bmp = U(CreateDIBSection)(d->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!d->bmp || !bits) return 0;
    d->old = (HBITMAP)U(SelectObject)(d->dc, d->bmp);
    d->bits = (unsigned *)bits; d->w = w; d->h = h;
    for (int i = 0; i < w * h; i++) d->bits[i] = 0x00FFFFFF;
    return 1;
}
static inline void g9_dib_free(struct g9_dib *d) {
    if (d->dc && d->old) U(SelectObject)(d->dc, d->old);
    if (d->bmp) U(DeleteObject)(d->bmp);
    if (d->dc) U(DeleteDC)(d->dc);
    memset(d, 0, sizeof *d);
}
static inline void g9_dib_clear(struct g9_dib *d) { for (int i = 0; i < d->w * d->h; i++) d->bits[i] = 0x00FFFFFF; }
/* RGB (low 24 bits of the BGRA pixel) at x,y; 0xFFFFFFFF if outside. */
static inline unsigned g9_px(struct g9_dib *d, int x, int y) {
    if (x < 0 || y < 0 || x >= d->w || y >= d->h) return 0xFFFFFFFFu;
    return d->bits[y * d->w + x] & 0x00FFFFFFu;
}
#define G9_BGR(r, g, b) ((unsigned)(((r) << 16) | ((g) << 8) | (b)))
#endif
