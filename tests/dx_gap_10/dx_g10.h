/* gap_10: shared declarations and fixtures. Syscall signatures below are derived from the win32kfull/win32kbase
 * analysis (parameter use and probes) and from the gdi32full/user32 call sites. */
#ifndef DXG10_H
#define DXG10_H
#include "k.h"
#include <wchar.h>

/* EXPECT_EQ evaluates its arguments twice; syscall calls with side effects need a single evaluation. */
#define G10_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#define G10_GDI_ERROR 0xFFFFFFFFu
#define G10_TAG(a, b, c, d) ((DWORD)(unsigned char)(a) | ((DWORD)(unsigned char)(b) << 8) | ((DWORD)(unsigned char)(c) << 16) | ((DWORD)(unsigned char)(d) << 24))

UDECL(DWORD, NtGdiGetFontData, (HDC, DWORD, DWORD, PVOID, DWORD));
UDECL(DWORD, NtGdiGetKerningPairs, (HDC, DWORD, KERNINGPAIR *));
UDECL(int,   NtGdiGetTextFaceW, (HDC, int, WCHAR *, BOOL));
UDECL(HFONT, NtGdiHfontCreate, (const void *, ULONG, ULONG, ULONG, PVOID));
UDECL(UINT,  NtGdiSetBoundsRect, (HDC, const RECT *, UINT));
UDECL(HBRUSH, NtGdiCreateDIBBrush, (const void *, UINT, UINT, BOOL, BOOL, const void *));
UDECL(BOOL,  NtUserGetPointerType, (UINT, DWORD *));
UDECL(BOOL,  NtUserGetPointerProprietaryId, (UINT, GUID *));
UDECL(BOOL,  NtUserGetPointerDeviceOrientation, (HANDLE, DWORD *));
UDECL(BOOL,  NtUserGetPointerInputTransform, (UINT, UINT, float *));
UDECL(BOOL,  NtUserGetPointerFrameTimes, (UINT, UINT, void *));
UDECL(INT_PTR, NtUserRegisterPointerDeviceNotifications, (HWND, BOOL));
UDECL(BOOL,  NtUserGetGestureExtArgs, (HANDLE, UINT, BYTE *));
UDECL(BOOL,  NtUserGetClipboardAccessToken, (HANDLE *, ACCESS_MASK));
UDECL(ULONG_PTR, NtUserQueryInputContext, (HANDLE, int));
UDECL(BOOL,  NtUserImpersonateDdeClientWindow, (HWND, HWND));
UDECL(BOOL,  NtUserSetWindowDisplayAffinity, (HWND, DWORD));
UDECL(BOOL,  NtUserTranslateMessage, (const MSG *, UINT));
UDECL(HHOOK, NtUserSetWindowsHookEx, (HINSTANCE, void *, DWORD, int, HOOKPROC, DWORD));
UDECL(BOOL,  NtUserSetCursorPos, (int, int));
UDECL(HWND,  NtUserSetClipboardViewer, (HWND));
UDECL(BOOL,  NtUserRegisterHotKey, (HWND, int, UINT, UINT));

/* USER handle table type tags (HMValidateHandle type arguments in the kernel analysis) */
#define G10_TYPE_HOOK      5
#define G10_TYPE_IMC       0x11
#define G10_TYPE_POINTERDEV 0x13
#define G10_TYPE_GESTURE   0x15

/* A font selected into a private memory DC. */
typedef struct { HDC dc; HFONT f, old; WCHAR face[LF_FACESIZE]; TEXTMETRICW tm; BOOL tt; } G10F;
static inline BOOL g10_font_open(G10F *x, const WCHAR *face, int h, int cs) {
    memset(x, 0, sizeof *x);
    x->dc = U(CreateCompatibleDC)(NULL);
    if (!x->dc) return FALSE;
    x->f = U(CreateFontW)(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, cs, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    if (!x->f) { U(DeleteDC)(x->dc); x->dc = NULL; return FALSE; }
    x->old = (HFONT)U(SelectObject)(x->dc, x->f);
    U(GetTextFaceW)(x->dc, LF_FACESIZE, x->face);
    U(GetTextMetricsW)(x->dc, &x->tm);
    x->tt = (x->tm.tmPitchAndFamily & TMPF_TRUETYPE) != 0;
    return TRUE;
}
static inline void g10_font_close(G10F *x) {
    if (x->dc) { U(SelectObject)(x->dc, x->old); U(DeleteObject)(x->f); U(DeleteDC)(x->dc); }
    x->dc = NULL;
}
/* Opens `face` and requires that GDI really picked it as a TrueType font; otherwise the caller skips. */
static inline BOOL g10_font_open_tt(G10F *x, const WCHAR *face, int h, int cs) {
    if (!g10_font_open(x, face, h, cs)) return FALSE;
    if (!x->tt || lstrcmpiW(x->face, face) != 0) { g10_font_close(x); return FALSE; }
    return TRUE;
}
#define G10_NEED_ARIAL(x, h) G10F x; \
    if (!g10_font_open_tt(&x, L"Arial", (h), DEFAULT_CHARSET)) { t_skip("Arial TrueType not available"); return; }

/* A window living on its own thread of this process (cross-thread rules). */
typedef struct { HANDLE th, ready, stop; HWND hwnd; DWORD tid; } G10W;
BOOL g10_wthread_start(G10W *w);
void g10_wthread_stop(G10W *w);

HDC g10_mem_dc(void);   /* 1x1-bitmap compatible DC, caller deletes with g10_mem_dc_free */
void g10_mem_dc_free(HDC dc);
#endif
