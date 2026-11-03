/* gui_thread_conversion: shared scaffolding.
 *
 * Almost every test here observes "fresh process" state (TEB.Win32ThreadInfo before the first
 * win32k syscall, PEB.KernelCallbackTable before user32 loads, ...), so each TEST() is a thin
 * wrapper that re-launches this exe as a CHILD ("gtc") running exactly one scenario. The child
 * entry path of the harness loads no module, so the scenario starts pristine and decides itself
 * when win32u.dll / user32.dll / gdi32.dll get loaded.
 *
 * SCEN(name) declares the scenario body and its parent-side TEST gui_thread_conversion_<name>.
 * Inside a scenario use GC()/GCF()/GEQ() (counted CHECKs) instead of CHECK(): the child's exit code
 * is its failure count.
 */
#ifndef GTC_H
#define GTC_H
#include "t.h"
#include "k.h"

typedef void (*gtc_fn)(void);
void gtc_register(const char *name, gtc_fn fn);
extern int gtc_fails;
const char *gtc_arg(void);            /* text after "name:" in the scenario selector, or "" */
void gtc_parent_run(const char *selector, DWORD timeout_ms);

#define GC(c)        do { int ok_ = !!(c); if (!ok_) gtc_fails++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)
#define GCF(c, ...)  do { int ok_ = !!(c); if (!ok_) gtc_fails++; t_check(ok_, __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)
#define GEQ(a, b)    do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    if (a_ != b_) { gtc_fails++; } t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#define GNE(a, b)    do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    if (a_ == b_) { gtc_fails++; } t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", a_); } while (0)
#define GERR(err)    do { DWORD e_ = GetLastError(); if (e_ != (DWORD)(err)) { gtc_fails++; } \
    t_check(e_ == (DWORD)(err), __FILE__, __LINE__, #err, "GetLastError got %lu want %lu", e_, (DWORD)(err)); } while (0)

/* win32u-only lookup: never loads user32. On Windows 10 the first win32k syscall of a thread converts it and calls back into user
 * mode through PEB.KernelCallbackTable; with no user32 the table is NULL and the process dies (0xC000041D, or an access violation
 * on the main thread). So the lookup also installs a table of do-nothing handlers (gtc_stub_table) before the first stub can run:
 * the conversion then completes and the state it leaves behind is exactly what a user32-less process would show. */
#define W(Name) ((Name##_pfn)gtc_win32u_proc(#Name))
FARPROC gtc_win32u_proc(const char *name);
unsigned long long gtc_stub_table(void);   /* PEB.KernelCallbackTable value gtc_win32u_proc installed, 0 when none */
#define SCEN(n) \
    static void sc_##n(void); \
    static void __attribute__((constructor)) rsc_##n(void) { gtc_register(#n, sc_##n); } \
    TEST(gui_thread_conversion_##n) { gtc_parent_run(#n, 30000); } \
    static void sc_##n(void)
/* Same, with an argument string reachable through gtc_arg() (several TESTs share one body). */
#define SCEN_ARG(tname, body, arg) \
    TEST(gui_thread_conversion_##tname) { gtc_parent_run(#body ":" arg, 30000); }
#define SCEN_BODY(body) \
    static void sc_##body(void); \
    static void __attribute__((constructor)) rsc_##body(void) { gtc_register(#body, sc_##body); } \
    static void sc_##body(void)

/* ---- TEB / PEB / CLIENTINFO accessors (all safe reads) ---------------------------------- */
unsigned long long gtc_teb_u64(unsigned off);
unsigned long long gtc_peb_u64(unsigned off);
unsigned long      gtc_peb_u32(unsigned off);
unsigned long long gtc_ci_u64(unsigned off);   /* CLIENTINFO field at TEB+0x800+off */
unsigned long      gtc_ci_u32(unsigned off);
unsigned short     gtc_ci_u16(unsigned off);
#define TEB_ClientIdThread 0x48
#define CI_flags_off   0x00
#define CI_dwExpWinVer 0x10
#define CI_dwTIFlags   0x1c
#define CI_pDeskInfo   0x20
#define CI_ulClientDelta 0x28
#define CI_hWndCache   0x40
#define CI_pWndCache   0x48
#define CI_hKL         0x90
#define CI_CodePage    0x98
#define CI_pRegClasses 0xd0
#define CI_ctiA        0xe8
#define CI_ctiB        0xf0
#define CI_SIZE_QWORDS 62
#define PEB_GdiDCAttributeList_off 0x108
#define PEB_GdiHandleBuffer_off    0x140
#define KCB_ClientThreadSetup 0x54   /* KeUserModeCallback(0x54) during thread setup */

int  gtc_ci_all_zero(void);
int  gtc_teb_converted(void);                  /* Win32ThreadInfo != 0 */
int  gtc_in_module(const char *mod, unsigned long long p);   /* p inside the loaded image of mod */
int  gtc_is_kernel_range(unsigned long long p);
/* VirtualQuery wrapper: returns protect, fills type/size, 0 on failure. */
DWORD gtc_query(const void *p, DWORD *type, SIZE_T *region);

/* Spawn a pristine helper process (this exe, --child gtc <selector>); returns process handle or NULL. */
HANDLE gtc_spawn(const char *selector);
/* Run scenario `selector` in a copy of this exe whose PE Subsystem is GUI (2) instead of console. */
void gtc_parent_run_gui(const char *selector, DWORD timeout_ms);
void gtc_assert_pristine(void);                /* user32/gdi32/gdi32full not loaded, TEB.Win32ThreadInfo NULL */

/* Worker-thread helper: runs fn(arg) on a brand-new thread (fresh TEB) and joins, 10 s bound. */
typedef void (*gtc_thread_fn)(void *arg);
int  gtc_thread(gtc_thread_fn fn, void *arg);

/* win32u direct-stub declarations used by several files */
UDECL(ULONGLONG, NtUserGetThreadState, (int));
UDECL(ULONG, NtUserGetGuiResources, (HANDLE, ULONG));
UDECL(LONG, NtUserProcessConnect, (HANDLE, PVOID));
UDECL(ULONG_PTR, NtUserGetKeyboardLayout, (DWORD));
UDECL(ULONG_PTR, NtUserGetThreadDesktop, (DWORD));
UDECL(ULONG_PTR, NtUserGetForegroundWindow, (void));
UDECL(SHORT, NtUserGetKeyState, (int));
UDECL(ULONG_PTR, NtUserQueryWindow, (HWND, ULONG));
UDECL(ULONG_PTR, NtGdiFlush, (void));
UDECL(ULONG, NtUserGetQueueStatus, (ULONG));
UDECL(ULONG, NtUserGetDoubleClickTime, (void));
UDECL(HANDLE, NtUserGetProcessWindowStation, (void));
#endif
