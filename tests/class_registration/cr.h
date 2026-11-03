/* Shared fixtures for the class_registration area: window classes as the win32k kernel sees them.
 *
 * Syscall argument layouts (derived from win32kfull analysis and from what the user32 callers
 * push; none of these are in an SDK header):
 *
 *  NtUserRegisterClassExWOW(WNDCLASSEXW *wc, UNICODE_STRING *name, UNICODE_STRING *versionName,
 *        CLSMENUNAME *menu, WORD fnid, DWORD flags, DWORD *wow)
 *     user32!RegisterClassExWOWW pushes exactly this list (the last three on the stack). The kernel
 *     copies 0x50 bytes of wc, copies both names (buffer pointers >= 0x10000, else the "pointer" is a
 *     class atom), and reads the UNICODE_STRING that menu->pusMenuName points at unconditionally.
 *     flags: only bits 0x02 (ANSI class), 0x40 (modern application, strict icon validation) and 0x80
 *     are accepted (mask 0xffffff3d -> ERROR_INVALID_FLAGS).
 *  NtUserUnregisterClass(UNICODE_STRING *name, HINSTANCE inst, CLSMENUNAME *out)  -> 1 / 0
 *  NtUserGetClassInfoEx(HINSTANCE, UNICODE_STRING *name, WNDCLASSEXW *wc, PVOID *menuName, BOOL ansi) -> atom
 *  NtUserGetClassName(HWND, BOOL real, UNICODE_STRING *out) -> chars written (out->MaximumLength
 *     is the byte capacity, out->Buffer the destination; Length is ignored on input)
 *  NtUserGetAtomName(ATOM, UNICODE_STRING *out) -> chars written
 *  NtUserSetClassLong(HWND, int idx, DWORD v, BOOL ansi), NtUserSetClassLongPtr(HWND, int, ULONG_PTR, BOOL ansi),
 *  NtUserSetClassWord(HWND, int, WORD)
 */
#ifndef CR_H
#define CR_H
#include "t.h"
#include "k.h"
#include <wchar.h>

typedef struct { PVOID ansi; PVOID unicode; UNICODE_STRING *pus; } CR_MENU;   /* tagCLSMENUNAME */

UDECL(ATOM, NtUserRegisterClassExWOW, (const WNDCLASSEXW *wc, UNICODE_STRING *name, UNICODE_STRING *ver,
      CR_MENU *menu, WORD fnid, DWORD flags, DWORD *wow));
UDECL(LONG_PTR, NtUserUnregisterClass, (UNICODE_STRING *name, HINSTANCE inst, CR_MENU *out));
UDECL(ATOM, NtUserGetClassInfoEx, (HINSTANCE inst, UNICODE_STRING *name, WNDCLASSEXW *wc, PVOID *menuName, BOOL ansi));
UDECL(int, NtUserGetClassName, (HWND hwnd, BOOL real, UNICODE_STRING *out));
UDECL(UINT, NtUserGetAtomName, (ATOM atom, UNICODE_STRING *out));
UDECL(DWORD, NtUserSetClassLong, (HWND hwnd, int idx, DWORD v, BOOL ansi));
UDECL(ULONG_PTR, NtUserSetClassLongPtr, (HWND hwnd, int idx, ULONG_PTR v, BOOL ansi));
UDECL(WORD, NtUserSetClassWord, (HWND hwnd, int idx, WORD v));

/* GetLastError values the class code sets (winerror.h numbering) */
#define CR_ATOM(a) ((LPCWSTR)(ULONG_PTR)(a))
#define CR_ERR_INVALID_PARAMETER 87
#define CR_ERR_INVALID_FLAGS     1004
#define CR_ERR_ALREADY_EXISTS    1410
#define CR_ERR_DOES_NOT_EXIST    1411
#define CR_ERR_HAS_WINDOWS       1412
#define CR_ERR_INVALID_INDEX     1413
#define CR_ERR_ACCESS_DENIED     5
#define CR_ERR_INVALID_DATA      13

/* GCL_/GCLP_ indices (negative) not all declared by the mingw headers */
#define CR_GCL_MENUNAME   (-8)
#define CR_GCL_HBRBACKGROUND (-10)
#define CR_GCL_HCURSOR    (-12)
#define CR_GCL_HICON      (-14)
#define CR_GCL_HCURSOR_ (-12)
#define CR_GCL_HMODULE    (-16)
#define CR_GCL_CBWNDEXTRA (-18)
#define CR_GCL_CBCLSEXTRA (-20)
#define CR_GCL_WNDPROC    (-24)
#define CR_GCL_STYLE      (-26)
#define CR_GCW_ATOM       (-32)
#define CR_GCL_HICONSM    (-34)

#define CR_STYLE_VISIBLE_MASK 0x37bffu   /* win32kfull!_GetClassInfoEx / user32!_GetClassData: cls->style & 0x37bff */

/* ---- fixtures (cr_util.c) ---- */
HINSTANCE cr_hinst(void);
BOOL cr_user(void);                                   /* user32 entry points present */
BOOL cr_nt(void);                                     /* the win32u class stubs present */
const WCHAR *cr_name(void);                           /* unique class name, valid for the next 32 calls */
LRESULT CALLBACK cr_proc(HWND, UINT, WPARAM, LPARAM);        /* forwards to DefWindowProcW */
LRESULT CALLBACK cr_proc2(HWND, UINT, WPARAM, LPARAM);       /* a second, distinct procedure */
LRESULT CALLBACK cr_procA(HWND, UINT, WPARAM, LPARAM);       /* forwards to DefWindowProcA */
void cr_wc(WNDCLASSEXW *wc, const WCHAR *name, UINT style, int cbCls, int cbWnd);   /* fills a registrable struct */
ATOM cr_reg(const WCHAR *name, UINT style, int cbCls, int cbWnd);  /* RegisterClassExW with cr_proc */
BOOL cr_unreg(const WCHAR *name);
HWND cr_win(const WCHAR *cls);                        /* hidden off-screen popup of that class */
void cr_us(UNICODE_STRING *us, const WCHAR *s);       /* counted string, MaximumLength = Length + 2 */
ATOM cr_nt_reg(const WNDCLASSEXW *wc, const WCHAR *name, DWORD flags);     /* direct syscall, no menu, fnid 0 */
BOOL cr_nt_unreg(const WCHAR *name, HINSTANCE inst);
ATOM cr_nt_info(HINSTANCE inst, const WCHAR *name, WNDCLASSEXW *wc, BOOL ansi);
int  cr_nt_getname(HWND h, BOOL real, WCHAR *buf, int cch);
ULONG_PTR cr_expwinver(void);                         /* (major<<8|minor) of this image's subsystem version = pti->dwExpWinVer source */
BOOL cr_modern(void);                                 /* exp version > 0x30a, i.e. user32 passes flag 0x40 and rejects bad style bits */
void *cr_cls_client(HWND hwnd);                       /* client-side CLS image (desktop heap), or NULL */
BOOL cr_rd(const void *p, void *out, size_t n);
extern volatile LONG cr_hits;                         /* messages seen by cr_proc */
#define CR_NEED() do { if (!cr_user()) { t_skip("user32 class entry points missing"); return; } } while (0)
#define CR_NEED_NT() do { if (!cr_user() || !cr_nt()) { t_skip("win32u class stubs missing"); return; } } while (0)
#endif
