/* Shared helpers for the layered attribute / UpdateLayeredWindow / child / cross-thread / cross-process area.
 * Everything is resolved by name at run time; user32/win32u/gdi32 are never linked. */
#ifndef XL_H
#define XL_H
#include "t.h"
#include "k.h"
#include <wingdi.h>

#define XL_ERR_SENT 0x7e57u
#define XL_EXL 0x00080000          /* WS_EX_LAYERED */
#ifndef ERROR_INVALID_WINDOW_HANDLE
#define ERROR_INVALID_WINDOW_HANDLE 1400
#endif
#ifndef ERROR_ACCESS_DENIED
#define ERROR_ACCESS_DENIED 5
#endif
#ifndef ULW_EX_NORESIZE
#define ULW_EX_NORESIZE 8
#endif

UDECL(BOOL, NtUserSetLayeredWindowAttributes, (HWND, COLORREF, BYTE, DWORD));
UDECL(BOOL, NtUserGetLayeredWindowAttributes, (HWND, COLORREF *, BYTE *, DWORD *));
UDECL(BOOL, NtUserUpdateLayeredWindow, (HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF, BLENDFUNCTION *, DWORD, RECT *));

static BOOL xl_set(HWND w, COLORREF k, BYTE a, DWORD f) { return UN(NtUserSetLayeredWindowAttributes)(w, k, a, f); }
static BOOL xl_get(HWND w, COLORREF *k, BYTE *a, DWORD *f) { return UN(NtUserGetLayeredWindowAttributes)(w, k, a, f); }
#define XL_NEED() do { if (!UN(NtUserSetLayeredWindowAttributes) || !UN(NtUserGetLayeredWindowAttributes) || !UN(NtUserUpdateLayeredWindow) \
    || !U(CreateWindowExA) || !U(SetWindowLongA) || !U(UpdateLayeredWindow) || !U(CreateDIBSection)) { t_skip("missing export"); return; } } while (0)

/* ---- window class with a tiny message log ------------------------------------------------------ */
enum { XM_STYLECHANGING, XM_STYLECHANGED, XM_POSCHANGING, XM_POSCHANGED, XM_SIZE, XM_MOVE, XM_SHOWWINDOW, XM_PAINT, XM_ERASE, XM_NCPAINT, XM_NCCALC, XM_MINMAX, XM_N };
extern volatile LONG xl_msgs[XM_N];   /* defined once, in ulw.c: every translation unit's window procedure counts into the same array */
static void xl_msgs_reset(void) { for (int i = 0; i < XM_N; i++) xl_msgs[i] = 0; }
static LRESULT CALLBACK xl_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    int i = -1;
    switch (m) {
    case WM_STYLECHANGING: i = XM_STYLECHANGING; break; case WM_STYLECHANGED: i = XM_STYLECHANGED; break;
    case WM_WINDOWPOSCHANGING: i = XM_POSCHANGING; break; case WM_WINDOWPOSCHANGED: i = XM_POSCHANGED; break;
    case WM_SIZE: i = XM_SIZE; break; case WM_MOVE: i = XM_MOVE; break; case WM_SHOWWINDOW: i = XM_SHOWWINDOW; break;
    case WM_PAINT: i = XM_PAINT; break; case WM_ERASEBKGND: i = XM_ERASE; break; case WM_NCPAINT: i = XM_NCPAINT; break;
    case WM_NCCALCSIZE: i = XM_NCCALC; break; case WM_GETMINMAXINFO: i = XM_MINMAX; break;
    }
    if (i >= 0) InterlockedIncrement(&xl_msgs[i]);
    return U(DefWindowProcA)(h, m, w, l);
}
static ATOM xl_class(void) {
    static ATOM a;
    if (a) return a;
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = xl_wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "xlLayWnd";
    a = U(RegisterClassA)(&wc);
    if (!a && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) a = 1;
    return a;
}
/* Hidden, off-screen window of the private class. */
static HWND xl_mk_at(DWORD style, DWORD ex, HWND parent, int x, int y, int cx, int cy) {
    if (!xl_class()) return NULL;
    return U(CreateWindowExA)(ex, "xlLayWnd", "xl", style, x, y, cx, cy, parent, NULL, GetModuleHandleA(NULL), NULL);
}
static HWND xl_mk(DWORD style, DWORD ex, HWND parent) {
    return xl_mk_at(style, ex | (parent ? 0 : WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE), parent, parent ? 2 : -32000, parent ? 2 : -32000, 16, 16);
}
static HWND xl_top(DWORD ex) { return xl_mk(WS_POPUP, ex, NULL); }       /* popup with extra ex style */
static HWND xl_lay(void) { return xl_top(XL_EXL); }
static HWND xl_plain(void) { return xl_top(0); }
static void xl_kill(HWND w) { if (w) U(DestroyWindow)(w); }
static LONG xl_exstyle(HWND w) { return U(GetWindowLongA)(w, GWL_EXSTYLE); }
static BOOL xl_is_lay(HWND w) { return (xl_exstyle(w) & XL_EXL) != 0; }
static HWND xl_bogus(void) { return (HWND)(ULONG_PTR)0x7fff7fff0ull; }

/* ---- 32-bpp memory DC for UpdateLayeredWindow --------------------------------------------------- */
struct xl_img { HDC dc; HBITMAP bmp, old; int w, h; };
static int xl_img_make(struct xl_img *im, int w, int h) {
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    memset(im, 0, sizeof *im); im->w = w; im->h = h;
    im->dc = U(CreateCompatibleDC)(NULL);
    if (!im->dc) return 0;
    im->bmp = U(CreateDIBSection)(im->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!im->bmp || !bits) return 0;
    for (int i = 0; i < w * h; i++) ((DWORD *)bits)[i] = 0xff304050;
    im->old = (HBITMAP)U(SelectObject)(im->dc, im->bmp);
    return 1;
}
static void xl_img_free(struct xl_img *im) {
    if (im->dc && im->old) U(SelectObject)(im->dc, im->old);
    if (im->bmp) U(DeleteObject)(im->bmp);
    if (im->dc) U(DeleteDC)(im->dc);
    memset(im, 0, sizeof *im);
}
static BLENDFUNCTION xl_blend(BYTE a) { BLENDFUNCTION b; b.BlendOp = AC_SRC_OVER; b.BlendFlags = 0; b.SourceConstantAlpha = a; b.AlphaFormat = 0; return b; }
/* UpdateLayeredWindow with a complete, valid argument set and the given flags; dst NULL (position unchanged), size = the image size. */
static BOOL xl_ulw(HWND w, struct xl_img *im, DWORD flags) {
    BLENDFUNCTION b = xl_blend(255); POINT src = { 0, 0 }; SIZE sz = { im->w, im->h };
    return U(UpdateLayeredWindow)(w, NULL, NULL, &sz, im->dc, &src, 0, &b, flags);
}
/* Get with all three out pointers; returns the BOOL, results in *k,*a,*f (untouched on failure). */
struct xl_attr { COLORREF key; BYTE alpha; DWORD flags; };
static BOOL xl_getall(HWND w, struct xl_attr *o) { return xl_get(w, &o->key, &o->alpha, &o->flags); }
static void xl_attr_clear(struct xl_attr *o) { o->key = 0xdeadbeef; o->alpha = 0xa5; o->flags = 0xfeedf00d; }

/* ---- child processes with a command channel ----------------------------------------------------- */
struct xl_proc { HANDLE hproc, hthread, rd, wr; DWORD pid; char buf[512]; int blen; };
/* Spawn "exe --child name args" with stdout piped to us and stdin piped from us.
 * rid != 0: the child runs with a copy of our token lowered to that mandatory integrity level (for example 0x1000 low, 0x2000 medium). */
static HANDLE xl_il_token(DWORD rid) {
    HANDLE tok = NULL, dup = NULL; PSID sid = NULL; SID_IDENTIFIER_AUTHORITY ia = SECURITY_MANDATORY_LABEL_AUTHORITY;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, &tok)) return NULL;
    BOOL ok = DuplicateTokenEx(tok, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &dup);
    CloseHandle(tok);
    if (!ok) return NULL;
    if (!AllocateAndInitializeSid(&ia, 1, rid, 0, 0, 0, 0, 0, 0, 0, &sid)) { CloseHandle(dup); return NULL; }
    TOKEN_MANDATORY_LABEL tml; tml.Label.Sid = sid; tml.Label.Attributes = SE_GROUP_INTEGRITY;
    ok = SetTokenInformation(dup, TokenIntegrityLevel, &tml, sizeof tml + GetLengthSid(sid));
    FreeSid(sid);
    if (!ok) { CloseHandle(dup); return NULL; }
    return dup;
}
static int xl_spawn_il(struct xl_proc *p, const char *exe, const char *name, const char *args, DWORD rid) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE orf = NULL, ow = NULL, ir = NULL, iw = NULL, tok = NULL; char cmd[2048]; STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(p, 0, sizeof *p);
    if (rid && !(tok = xl_il_token(rid))) return 0;
    if (!CreatePipe(&orf, &ow, &sa, 0) || !CreatePipe(&ir, &iw, &sa, 0)) { if (tok) CloseHandle(tok); return 0; }
    SetHandleInformation(orf, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(iw, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = ir; si.hStdOutput = ow; si.hStdError = ow;
    snprintf(cmd, sizeof cmd, "\"%s\" --child %s %s", exe, name, args ? args : "");
    memset(&pi, 0, sizeof pi);
    BOOL ok = tok ? CreateProcessAsUserA(tok, NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)
                  : CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (tok) CloseHandle(tok);
    CloseHandle(ow); CloseHandle(ir);
    if (!ok) { CloseHandle(orf); CloseHandle(iw); return 0; }
    p->hproc = pi.hProcess; p->hthread = pi.hThread; p->rd = orf; p->wr = iw; p->pid = pi.dwProcessId;
    return 1;
}
static int xl_spawn(struct xl_proc *p, const char *exe, const char *name, const char *args) { return xl_spawn_il(p, exe, name, args, 0); }
/* Read one output line (without newline) into out; 0 on timeout/EOF. */
static int xl_readline(struct xl_proc *p, char *out, size_t cap, DWORD timeout_ms) {
    DWORD t0 = GetTickCount();
    for (;;) {
        for (int i = 0; i < p->blen; i++) if (p->buf[i] == '\n') {
            int n = i < (int)cap - 1 ? i : (int)cap - 1;
            memcpy(out, p->buf, n); out[n] = 0;
            if (n && out[n - 1] == '\r') out[n - 1] = 0;
            memmove(p->buf, p->buf + i + 1, p->blen - i - 1); p->blen -= i + 1;
            return 1;
        }
        DWORD avail = 0;
        if (!PeekNamedPipe(p->rd, NULL, 0, NULL, &avail, NULL)) return 0;
        if (avail && p->blen < (int)sizeof p->buf - 1) {
            DWORD got = 0, want = (DWORD)(sizeof p->buf - 1 - p->blen); if (want > avail) want = avail;
            if (!ReadFile(p->rd, p->buf + p->blen, want, &got, NULL) || !got) return 0;
            p->blen += got; continue;
        }
        if (GetTickCount() - t0 > timeout_ms) return 0;
        Sleep(5);
    }
}
/* Same, but keeps dispatching this thread's messages while waiting (the child may act on windows of this thread). */
static int xl_readline_pump(struct xl_proc *p, char *out, size_t cap, DWORD timeout_ms) {
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < timeout_ms) { k_pump(10); if (xl_readline(p, out, cap, 30)) return 1; }
    return 0;
}
static void xl_send(struct xl_proc *p, const char *line) { DWORD n; char b[256]; snprintf(b, sizeof b, "%s\n", line); WriteFile(p->wr, b, (DWORD)strlen(b), &n, NULL); }
/* Ask the child to leave, then make sure it is gone. Returns its exit code (or 0xDEAD0001 when it had to be killed). */
static DWORD xl_finish(struct xl_proc *p) {
    DWORD code = 0xDEAD0001;
    xl_send(p, "quit");
    if (WaitForSingleObject(p->hproc, 10000) == WAIT_OBJECT_0) GetExitCodeProcess(p->hproc, &code);
    else { TerminateProcess(p->hproc, 1); WaitForSingleObject(p->hproc, 10000); }
    CloseHandle(p->hproc); CloseHandle(p->hthread); CloseHandle(p->rd); CloseHandle(p->wr);
    memset(p, 0, sizeof *p);
    return code;
}
static void xl_kill_proc(struct xl_proc *p) {
    TerminateProcess(p->hproc, 0); WaitForSingleObject(p->hproc, 10000);
    CloseHandle(p->hproc); CloseHandle(p->hthread); CloseHandle(p->rd); CloseHandle(p->wr);
    memset(p, 0, sizeof *p);
}

/* Copy of the running exe in the scratch dir, optionally with an embedded application manifest that lists
 * Windows 7/8/8.1/10 as supported. Returns the path (static) or NULL. */
static const char *xl_exe_copy(int manifest) {
    static char path[2][MAX_PATH * 2];
    char *dst = path[!!manifest];
    if (dst[0]) return dst;
    t_tmpfile(dst, sizeof path[0], manifest ? "xl_manifest.exe" : "xl_plain.exe");
    if (!CopyFileA(t_self_path(), dst, FALSE)) { dst[0] = 0; return NULL; }
    SetFileAttributesA(dst, FILE_ATTRIBUTE_NORMAL);
    if (manifest) {
        static const char mf[] =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
            "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
            "<compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\"><application>\r\n"
            "<supportedOS Id=\"{35138b9a-5d96-4fbd-8e2d-a2440225f93a}\"/>\r\n"
            "<supportedOS Id=\"{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}\"/>\r\n"
            "<supportedOS Id=\"{1f676c76-80e1-4239-95bb-83d0f6d0da78}\"/>\r\n"
            "<supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>\r\n"
            "</application></compatibility></assembly>\r\n";
        HANDLE h = BeginUpdateResourceA(dst, FALSE);
        BOOL ok = h && UpdateResourceA(h, MAKEINTRESOURCEA(24), MAKEINTRESOURCEA(1), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), (LPVOID)mf, sizeof mf - 1);
        if (h && !EndUpdateResourceA(h, !ok)) ok = FALSE;
        if (!ok) { DeleteFileA(dst); dst[0] = 0; return NULL; }
    }
    return dst;
}
#endif
