#include "sib.h"

static LRESULT CALLBACK sib_proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

const wchar_t *sib_class(void) {
    static wchar_t name[48];
    static int done;
    if (!done) {
        WNDCLASSEXW wc = { sizeof wc };
        swprintf(name, 48, L"win32kt_sib_%lu", GetCurrentProcessId());
        wc.lpfnWndProc = sib_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = name;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        if (U(RegisterClassExW)(&wc)) done = 1;
    }
    return done ? name : NULL;
}

HWND sib_mk(DWORD style, DWORD ex, int w, int h, int vis) {
    if (!U(CreateWindowExW) || !sib_class()) return NULL;
    HWND hw = U(CreateWindowExW)(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, sib_class(), L"win32kt_sib",
                                 style & ~WS_VISIBLE, -30000, -30000, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (hw && vis) U(ShowWindow)(hw, SW_SHOWNOACTIVATE);
    return hw;
}

HWND sib_child(HWND parent, const wchar_t *cls, DWORD style, int x, int y, int w, int h) {
    if (!U(CreateWindowExW)) return NULL;
    return U(CreateWindowExW)(0, cls, L"", style | WS_CHILD, x, y, w, h, parent, NULL, GetModuleHandleW(NULL), NULL);
}

int sib_read_sbinfo(HWND hwnd, struct sb_info *out, int *present) {
    if (present) *present = 0;
    if (t_is_wine()) return 0;
    char *w = (char *)k_wnd_ptr(hwnd);
    if (!w) return 0;
    void *self = NULL, *sb = NULL;
    if (!k_try_read_ptr(w + SIB_W_PSELF, &self) || !k_try_read_ptr(w + SIB_W_SBINFO, &sb)) return 0;
    if (!sb) return 1;               /* readable window, no SBINFO yet */
    char *c = w + ((ULONG_PTR)sb - (ULONG_PTR)self);
    if (!k_try_read(c, out, sizeof *out)) return 0;
    if (present) *present = 1;
    return 1;
}

FARPROC sib_stub(const char *name) {
    if (t_is_wine()) return NULL;
    return t_proc_in("win32u.dll", name);
}
