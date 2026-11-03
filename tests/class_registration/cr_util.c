#include "cr.h"

volatile LONG cr_hits;

HINSTANCE cr_hinst(void) { return GetModuleHandleW(NULL); }

BOOL cr_user(void) {
    return U(RegisterClassExW) && U(RegisterClassExA) && U(UnregisterClassW) && U(UnregisterClassA) &&
           U(GetClassInfoExW) && U(GetClassInfoExA) && U(CreateWindowExW) && U(DestroyWindow) &&
           U(DefWindowProcW) && U(DefWindowProcA) && U(GetClassNameW) && U(GetClassLongW) &&
           U(GetClassLongPtrW) && U(SetClassLongPtrW) && U(GetClassWord) && U(SetClassWord) && U(IsWindow);
}
BOOL cr_nt(void) {
    return UN(NtUserRegisterClassExWOW) && UN(NtUserUnregisterClass) && UN(NtUserGetClassInfoEx) &&
           UN(NtUserGetClassName) && UN(NtUserGetAtomName) && UN(NtUserSetClassLong) &&
           UN(NtUserSetClassLongPtr) && UN(NtUserSetClassWord);
}

const WCHAR *cr_name(void) {
    static WCHAR ring[32][48];
    static unsigned n;
    WCHAR *b = ring[n++ & 31];
    swprintf(b, 48, L"win32kt_cr_%lu_%u", GetCurrentProcessId(), n);
    return b;
}

LRESULT CALLBACK cr_proc(HWND h, UINT m, WPARAM w, LPARAM l) { InterlockedIncrement(&cr_hits); return U(DefWindowProcW)(h, m, w, l); }
LRESULT CALLBACK cr_proc2(HWND h, UINT m, WPARAM w, LPARAM l) { InterlockedIncrement(&cr_hits); return U(DefWindowProcW)(h, m, w, l); }
LRESULT CALLBACK cr_procA(HWND h, UINT m, WPARAM w, LPARAM l) { InterlockedIncrement(&cr_hits); return U(DefWindowProcA)(h, m, w, l); }

void cr_wc(WNDCLASSEXW *wc, const WCHAR *name, UINT style, int cbCls, int cbWnd) {
    memset(wc, 0, sizeof *wc);
    wc->cbSize = sizeof *wc;
    wc->style = style;
    wc->lpfnWndProc = cr_proc;
    wc->cbClsExtra = cbCls;
    wc->cbWndExtra = cbWnd;
    wc->hInstance = cr_hinst();
    wc->lpszClassName = name;
}
ATOM cr_reg(const WCHAR *name, UINT style, int cbCls, int cbWnd) {
    WNDCLASSEXW wc; cr_wc(&wc, name, style, cbCls, cbWnd);
    return U(RegisterClassExW)(&wc);
}
BOOL cr_unreg(const WCHAR *name) { return U(UnregisterClassW)(name, cr_hinst()); }
HWND cr_win(const WCHAR *cls) {
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"", WS_POPUP, -32000, -32000, 10, 10,
                              NULL, NULL, cr_hinst(), NULL);
}
void cr_us(UNICODE_STRING *us, const WCHAR *s) {
    size_t n = wcslen(s) * 2;
    us->Length = (USHORT)n; us->MaximumLength = (USHORT)(n + 2); us->Buffer = (PWSTR)s;
}
ATOM cr_nt_reg(const WNDCLASSEXW *wc, const WCHAR *name, DWORD flags) {
    UNICODE_STRING n, v, m = { 0, 0, NULL };
    CR_MENU menu = { NULL, NULL, &m };
    WNDCLASSEXW c = *wc;
    cr_us(&n, name);
    v = n;      /* user32 passes the class name again as the version-qualified name; an empty one is refused */
    c.lpszClassName = name;
    return UN(NtUserRegisterClassExWOW)(&c, &n, &v, &menu, 0, flags, NULL);
}
BOOL cr_nt_unreg(const WCHAR *name, HINSTANCE inst) {
    UNICODE_STRING n; CR_MENU out[2]; memset(out, 0, sizeof out);
    cr_us(&n, name);
    return UN(NtUserUnregisterClass)(&n, inst, out) != 0;
}
ATOM cr_nt_info(HINSTANCE inst, const WCHAR *name, WNDCLASSEXW *wc, BOOL ansi) {
    UNICODE_STRING n; PVOID menu[4] = { (PVOID)(ULONG_PTR)0xdeadbeef };  /* Windows writes one pointer; sized for any layout */
    cr_us(&n, name);
    memset(wc, 0, sizeof *wc); wc->cbSize = sizeof *wc;
    return UN(NtUserGetClassInfoEx)(inst, &n, wc, menu, ansi);
}
int cr_nt_getname(HWND h, BOOL real, WCHAR *buf, int cch) {
    UNICODE_STRING o; o.Length = 0; o.MaximumLength = (USHORT)(cch * 2); o.Buffer = buf;
    return UN(NtUserGetClassName)(h, real, &o);
}
BOOL cr_rd(const void *p, void *out, size_t n) { return k_try_read(p, out, n); }

ULONG_PTR cr_expwinver(void) {
    /* user32 derives the "expected Windows version" of a module from OptionalHeader.Major/MinorSubsystemVersion */
    BYTE *b = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt;
    if (!cr_rd(b, &dos, sizeof dos) || !cr_rd(b + dos.e_lfanew, &nt, sizeof nt)) return 0;
    return ((ULONG_PTR)nt.OptionalHeader.MajorSubsystemVersion << 8) | nt.OptionalHeader.MinorSubsystemVersion;
}
BOOL cr_modern(void) { return cr_expwinver() > 0x30a; }

void *cr_cls_client(HWND hwnd) {
    /* user32!GetClassWord: CLS = pwnd + (*(pwnd+0x80) - *(pwnd+8)) */
    BYTE *pwnd = (BYTE *)k_wnd_ptr(hwnd);
    ULONG_PTR a = 0, b = 0;
    if (!pwnd || !cr_rd(pwnd + 0x80, &a, 8) || !cr_rd(pwnd + 8, &b, 8)) return NULL;
    BYTE *cls = pwnd + (a - b);
    unsigned short atom = 0;
    if (!cr_rd(cls + 2, &atom, 2)) return NULL;
    /* sanity gate: the atom field must equal what the kernel reports for GCW_ATOM */
    if (atom != (unsigned short)U(GetClassWord)(hwnd, CR_GCW_ATOM)) return NULL;
    return cls;
}
