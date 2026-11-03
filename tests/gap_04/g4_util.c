#include "gap4.h"

static ATOM g_atom;
static ATOM g4_class(void) {
    if (g_atom) return g_atom;
    if (!U(RegisterClassExW) || !U(DefWindowProcW)) return 0;
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"gap4_wnd";
    g_atom = U(RegisterClassExW)(&wc);
    return g_atom;
}
HWND g4_window(DWORD style, DWORD exstyle, int x, int y, int w, int h, HWND parent) {
    if (!g4_class() || !U(CreateWindowExW)) return NULL;
    return U(CreateWindowExW)(exstyle | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"gap4_wnd", L"gap4", style,
                              x, y, w, h, parent, NULL, GetModuleHandleW(NULL), NULL);
}
void g4_destroy(HWND h) { if (h && U(DestroyWindow)) U(DestroyWindow)(h); }

HDC g4_memdc(HBITMAP *bmp_out, HBITMAP *old_out, void **bits_out) {
    if (!U(CreateCompatibleDC) || !U(CreateDIBSection) || !U(SelectObject)) return NULL;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 32; bi.bmiHeader.biHeight = -32;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP b = U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (!b || !dc) { if (b) U(DeleteObject)(b); if (dc) U(DeleteDC)(dc); return NULL; }
    *old_out = (HBITMAP)U(SelectObject)(dc, b); *bmp_out = b; if (bits_out) *bits_out = bits;
    return dc;
}
void g4_memdc_free(HDC dc, HBITMAP bmp, HBITMAP old) {
    if (!dc) return;
    U(SelectObject)(dc, old); U(DeleteDC)(dc); if (bmp) U(DeleteObject)(bmp);
}

UINT g4_real_dpi(void) {
    typedef DPI_AWARENESS_CONTEXT (WINAPI *set_t)(DPI_AWARENESS_CONTEXT);
    typedef UINT (WINAPI *get_t)(void);
    HMODULE u = LoadLibraryA("user32.dll");
    set_t set = (set_t)GetProcAddress(u, "SetThreadDpiAwarenessContext");
    get_t get = (get_t)GetProcAddress(u, "GetDpiForSystem");
    if (!set || !get) return 96;
    DPI_AWARENESS_CONTEXT old = set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    UINT d = get();
    if (old) set(old);
    return d ? d : 96;
}

static const char *const k_wine_unimpl[] = {
    "NtUserPhysicalToLogicalPoint",
    "NtUserGetAutoRotationState",
    "NtUserRemoveInjectionDevice",
    "NtUserSetDisplayConfig",
    "NtUserDisplayConfigSetDeviceInfo",
    "NtUserSetSystemCursor",
    "NtUserSetWindowFeedbackSetting",
    "NtUserGetWindowFeedbackSetting",
    "NtUserShutdownBlockReasonCreate",
    "NtUserDrawAnimatedRects",
    "NtUserDrawCaption",
    "NtUserGetListBoxInfo",
    "NtUserEnableNonClientDpiScaling",
    "NtUserCalculatePopupWindowPosition",
    "NtUserLogicalToPhysicalPoint",
    NULL
};
int g4_wine_skip(const char *name) {
    if (!t_is_wine()) return 0;
    for (int i = 0; k_wine_unimpl[i]; i++) if (!strcmp(k_wine_unimpl[i], name)) return 1;
    return 0;
}
