/* Shared helpers for the kbd_layouts_state area.
 *
 * Signatures below are taken from the win32kbase/win32kfull bodies (what each parameter is used
 * for) and from the user32 callers (what the real caller passes):
 *   NtUserGetKeyboardLayoutList(int nBuff, HKL *list)            -> count
 *   NtUserGetKeyboardLayout(DWORD idThread)                       -> HKL
 *   NtUserGetKeyboardLayoutName(UNICODE_STRING *)                 -> BOOL (Buffer written, Length not)
 *   NtUserActivateKeyboardLayout(HKL, UINT KLF_*)                 -> previous HKL
 *   NtUserLoadKeyboardLayoutEx(hFile, off, x, kbdDesc, hklUnload, UNICODE_STRING *name, hklValue, flags)
 *   NtUserUnloadKeyboardLayout(HKL)                               -> BOOL
 *   NtUserSetThreadLayoutHandles(HKL newHkl, HKL curHkl)          -> BOOL
 *   NtUserMapVirtualKeyEx(code, type, HKL, fUseHkl)               -> UINT
 *   NtUserVkKeyScanEx(WCHAR, HKL, fUseHkl)                        -> SHORT (sign extended)
 *   NtUserToUnicodeEx(vk, sc, BYTE state[256], WCHAR *buf, cch, flags, HKL)
 *   NtUserGetKeyNameText(lParam, WCHAR *buf, cch)
 *   NtUserGetKeyState(vk) / NtUserGetAsyncKeyState(vk) / NtUserGetKeyboardState(BYTE[256]) / NtUserSetKeyboardState(BYTE[256])
 */
#ifndef KLS_H
#define KLS_H
#include "t.h"
#include "k.h"
#include <stdbool.h>
#include <wchar.h>

UDECL(int,  NtUserGetKeyboardLayoutList, (UINT, HKL *));
UDECL(HKL,  NtUserGetKeyboardLayout, (DWORD));
UDECL(int,  NtUserGetKeyboardLayoutName, (UNICODE_STRING *));
UDECL(HKL,  NtUserActivateKeyboardLayout, (HKL, UINT));
UDECL(HKL,  NtUserLoadKeyboardLayoutEx, (HANDLE, UINT, UINT, PVOID, HKL, UNICODE_STRING *, UINT, UINT));
UDECL(int,  NtUserUnloadKeyboardLayout, (HKL));
UDECL(int,  NtUserSetThreadLayoutHandles, (HKL, HKL));
UDECL(UINT, NtUserMapVirtualKeyEx, (UINT, UINT, HKL, int));
UDECL(LONG_PTR, NtUserVkKeyScanEx, (WCHAR, HKL, int));
UDECL(int,  NtUserToUnicodeEx, (UINT, UINT, const BYTE *, WCHAR *, int, UINT, HKL));
UDECL(int,  NtUserGetKeyNameText, (LONG, WCHAR *, int));
UDECL(SHORT, NtUserGetKeyState, (int));
UDECL(SHORT, NtUserGetAsyncKeyState, (int));
UDECL(int,  NtUserGetKeyboardState, (BYTE *));
UDECL(int,  NtUserSetKeyboardState, (BYTE *));
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR, DWORD));

/* _CLIENTINFO lives at TEB+0x800. Offsets read from the kernel writers:
 *  +0x70 u32  key-state cache generation   = shared-info counter   (NtUserGetKeyState epilogue)
 *  +0x74 u64  key-state cache, 2 bits/key for vk 0..31        = first 8 bytes of pq->afKeyState
 *  +0x7c u32  async cache generation       = shared-info counter   (NtUserGetAsyncKeyState)
 *  +0x80 u64  async down bits (2 bits/key) = first 8 bytes of gafAsyncKeyState
 *  +0x88 u64  async recent-down bits (1 bit/key)
 *  +0x90 HKL  active layout handle         
 *  +0x98 u16  code page of the active layout
 * user32!GetKeyState / GetAsyncKeyState read the same offsets for their no-syscall fast path. */
#define KLS_CI_KS_GEN     0x70
#define KLS_CI_KS_BITS    0x74
#define KLS_CI_AS_GEN     0x7c
#define KLS_CI_AS_BITS    0x80
#define KLS_CI_AS_RECENT  0x88
#define KLS_CI_HKL        0x90
#define KLS_CI_CODEPAGE   0x98
#define KLS_PSI_KS_GEN    0x1b48
#define KLS_PSI_AS_GEN    0x1b4c

#define KLS_KLF_ACTIVATE      0x1
#define KLS_KLF_SUBSTITUTE_OK 0x2
#define KLS_KLF_REORDER       0x8
#define KLS_KLF_REPLACELANG   0x10
#define KLS_KLF_NOTELLSHELL   0x80
#define KLS_KLF_SETFORPROCESS 0x100
#define KLS_KLF_INITTIME      0x200
#define KLS_HKL_PREV ((HKL)0)
#define KLS_HKL_NEXT ((HKL)1)

/* 1 when every export used by the keyboard area resolved (user32 + win32u). */
static inline int kls_have_core(void) {
    return UN(NtUserGetKeyboardLayout) && UN(NtUserGetKeyboardLayoutList) && U(GetKeyboardLayout) &&
           U(LoadKeyboardLayoutW) && U(ActivateKeyboardLayout);
}
/* A process without user32 has a NULL PEB.KernelCallbackTable, and on Windows 10 its first win32k syscall then kills it
 * (0xC000041D): the thread conversion calls back into user mode. The pristine children install a table of do-nothing
 * handlers first, which lets the conversion complete without loading any user-mode DLL. */
static LONG NTAPI kls_cb_nothing(void *b, ULONG n) { (void)b; (void)n; return 0; }
static inline void kls_install_stub_callbacks(void) {
    static void *tab[0x100];
    for (int i = 0; i < 0x100; i++) tab[i] = (void *)kls_cb_nothing;
    *(void *volatile *)((char *)k_peb() + PEB_KernelCallbackTable) = tab;
}
#define KLS_NEED() do { if (!kls_have_core()) { t_skip("keyboard layout exports missing"); return; } } while (0)

/* ---- client info / SERVERINFO ------------------------------------------------------------- */
static inline bool kls_ci_u32(unsigned off, unsigned long *v) {
    return k_try_read_u32((char *)k_teb() + TEB_Win32ClientInfo + off, v);
}
static inline bool kls_ci_u64(unsigned off, unsigned long long *v) {
    return k_try_read((char *)k_teb() + TEB_Win32ClientInfo + off, v, 8);
}
static inline bool kls_psi_u32(unsigned off, unsigned long *v) {
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) return false;
    return k_try_read_u32((char *)si.psi + off, v);
}

/* ---- layout bookkeeping --------------------------------------------------------------------- */
#define KLS_MAXL 64
static inline int kls_list(HKL *out, int max) {
    return UN(NtUserGetKeyboardLayoutList) ? UN(NtUserGetKeyboardLayoutList)(max, out) : 0;
}
static inline bool kls_in_list(HKL h) {
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    for (int i = 0; i < n && i < KLS_MAXL; i++) if (l[i] == h) return true;
    return false;
}
static inline HKL kls_cur(void) { return UN(NtUserGetKeyboardLayout)(0); }

/* Layouts this test run added to the window station list (so they can be unloaded again). */
extern HKL kls_added[16];
extern int kls_nadded;

/* Loads KLID without activating it. Records it for kls_cleanup() only when it was not already
 * in the list. Returns NULL when the layout is not available. */
static inline HKL kls_load(const WCHAR *klid) {
    HKL before[KLS_MAXL]; int nb = kls_list(before, KLS_MAXL);
    HKL h = U(LoadKeyboardLayoutW)(klid, 0);
    if (!h) return NULL;
    bool had = false;
    for (int i = 0; i < nb && i < KLS_MAXL; i++) if (before[i] == h) had = true;
    if (!had && kls_nadded < 16) kls_added[kls_nadded++] = h;
    return h;
}
static inline bool kls_was_added(HKL h) {
    for (int i = 0; i < kls_nadded; i++) if (kls_added[i] == h) return true;
    return false;
}
/* Unloads everything kls_load() added. Never touches layouts that were present before. */
static inline void kls_cleanup(void) {
    if (t_is_wine()) { kls_nadded = 0; return; }   /* win32u.NtUserUnloadKeyboardLayout aborts under Wine */
    while (kls_nadded > 0) { HKL h = kls_added[--kls_nadded]; if (U(UnloadKeyboardLayout)) U(UnloadKeyboardLayout)(h); }
}
/* A layout different from `not_this`: first from the list, else load a stock one. */
static inline HKL kls_other(HKL not_this) {
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    for (int i = 0; i < n && i < KLS_MAXL; i++) if (l[i] != not_this && ((ULONG_PTR)l[i] >> 28 & 0xf) != 0xe) return l[i];
    static const WCHAR *c[] = { L"00000407", L"00000809", L"0000040c", L"00000410", L"00000409", NULL };
    for (int i = 0; c[i]; i++) { HKL h = kls_load(c[i]); if (h && h != not_this) return h; }
    return NULL;
}
static inline HKL kls_us(void) { return kls_load(L"00000409"); }

/* Runs fn on a fresh GUI thread (own win32k THREADINFO, own queue, own layout slot). */
static inline DWORD kls_thread(k_thread_fn fn, void *arg) { return k_run_gui_thread(fn, arg); }

static inline int kls_isdead_ime(HKL h) { return (((ULONG_PTR)h >> 16) & 0xf000) == 0xe000; }

/* Key-state helpers: 256-byte tables, bit 7 = down, bit 0 = toggle. */
#define KS_DOWN(b)   (((b) & 0x80) != 0)
#define KS_TOGGLE(b) (((b) & 0x01) != 0)
/* Keys whose GetKeyState/GetKeyboardState answer does not depend on foreground accessibility:
 * NtUserGetKeyState skips the access gate for vk < 0x20 and for the 14-entry table at
 * win32kbase handles them separately (0x14 0x15 0x90 0x24 0x91 0xF0..0xF6 0xFA 0xFB), 0x5B, 0x5C and
 * 0xA0..0xA5; IsKeyStateCached (used by NtUserGetKeyboardState) has the same domain. */
static inline int kls_ungated(unsigned vk) {
    static const unsigned char sp[14] = { 0x14,0x15,0x90,0x24,0x91,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xFA,0xFB };
    if (vk < 0x20) return 1;
    for (int i = 0; i < 14; i++) if (sp[i] == vk) return 1;
    if (vk == 0x5b || vk == 0x5c) return 1;
    if (vk >= 0xa0 && vk <= 0xa5) return 1;
    return 0;
}
#endif
