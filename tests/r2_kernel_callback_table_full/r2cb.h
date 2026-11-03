/* r2_kernel_callback_table_full: shared infrastructure.
 *
 * What the kernel does: it never calls
 * user mode directly. It builds an input buffer, calls KeUserModeCallback(ApiNumber, buf, len,
 * &out, &outlen) and ntdll!KiUserCallbackDispatcher then runs
 *     PEB->KernelCallbackTable[ApiNumber](buf, len, ...)
 * The user32 thunk finishes with NtCallbackReturn(&result, 0x18); the kernel accepts the result
 * only when status >= 0 and outlen == 0x18, and then uses the first qword as the return value.
 *
 * This area observes and steers that channel from a CHILD process: it copies the table into a
 * private page, puts a recording stub in every slot, patches PEB+0x58 and removes everything again
 * on the way out (uninstall + atexit). The kernel re-reads PEB->KernelCallbackTable on every
 * callback, so the copy is what it calls. Each scenario runs in its own child so no
 * process-global state (the patched PEB pointer, hooks, threads) leaks between tests.
 *
 * The exe must not import user32/gdi32: every user32 name used here is rewritten to a lazy U()
 * lookup (same trick as tests/kcb_kernel_callbacks/kcb_api.h).
 */
#ifndef R2CB_H
#define R2CB_H
#include "t.h"
#include "k.h"

#define SendMessageW            U(SendMessageW)
#define SendMessageA            U(SendMessageA)
#define SendMessageTimeoutW     U(SendMessageTimeoutW)
#define PostMessageW            U(PostMessageW)
#define RegisterClassW          U(RegisterClassW)
#define RegisterClassExW        U(RegisterClassExW)
#define UnregisterClassW        U(UnregisterClassW)
#define CreateWindowExW         U(CreateWindowExW)
#define DestroyWindow           U(DestroyWindow)
#define DefWindowProcW          U(DefWindowProcW)
#define PeekMessageW            U(PeekMessageW)
#define DispatchMessageW        U(DispatchMessageW)
#define SetWindowsHookExW       U(SetWindowsHookExW)
#define UnhookWindowsHookEx     U(UnhookWindowsHookEx)
#define CallNextHookEx          U(CallNextHookEx)
#define SetWinEventHook         U(SetWinEventHook)
#define UnhookWinEvent          U(UnhookWinEvent)
#define NotifyWinEvent          U(NotifyWinEvent)
#define MsgWaitForMultipleObjectsEx U(MsgWaitForMultipleObjectsEx)
#define GetMenu                 U(GetMenu)
#define CreateMenu              U(CreateMenu)
#define DestroyMenu             U(DestroyMenu)
#define IsMenu                  U(IsMenu)
#define IsWindow                U(IsWindow)
#define SetWindowPos            U(SetWindowPos)
#define SetWindowTextW          U(SetWindowTextW)
#define GetWindowTextW          U(GetWindowTextW)
#define GetWindowThreadProcessId U(GetWindowThreadProcessId)
#define SetWindowLongPtrW       U(SetWindowLongPtrW)
#define GetWindowLongPtrW       U(GetWindowLongPtrW)
#define RegisterWindowMessageW  U(RegisterWindowMessageW)
#define ShowWindow              U(ShowWindow)

#define R2T(n) TEST(r2_kernel_callback_table_full_ ## n)

/* ---- child-side assertions ---------------------------------------------------------------- */
extern int r2_kfail;
#define K(c)  do { if (!(c)) { r2_kfail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define KF(c, ...) do { if (!(c)) { r2_kfail++; printf("  FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); } } while (0)
#define KEQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    if (a_ != b_) { r2_kfail++; printf("  FAIL %s:%d: %s == %s (got 0x%llx want 0x%llx)\n", __FILE__, __LINE__, #a, #b, a_, b_); } } while (0)
#define KNOTE(...) do { printf("  NOTE "); printf(__VA_ARGS__); printf("\n"); } while (0)
/* Windows-only expectation: hard on a real Windows 10 user32, a NOTE under an emulator with its own callback ABI. */
#define KW(c) do { if (r2_family()) K(c); else if (!(c)) printf("  NOTE (not Windows 10) %s\n", #c); } while (0)
#define KWEQ(a, b) do { if (r2_family()) KEQ(a, b); } while (0)
#define R2_SKIP 77
#define R2_NEED(c, why) do { if (!(c)) { printf("  SKIP %s\n", why); return R2_SKIP; } } while (0)

/* ---- scenarios: one child process each ---------------------------------------------------- */
typedef int (*r2_scn_fn)(void);
void r2_register_scn(const char *name, r2_scn_fn fn);
void r2_run_scn_test(const char *name);          /* parent side: run the scenario in a child */
#define R2_SCN(name) \
    static int scn_##name(void); \
    static void __attribute__((constructor)) rs_##name(void) { r2_register_scn(#name, scn_##name); } \
    R2T(name) { r2_run_scn_test(#name); } \
    static int scn_##name(void)

/* ---- the reference table (generated data in r2cb_data.h) ------------------------------------ */
int  r2_ref_revision(void);       /* 19041.2965, the revision the reference RVAs were read from */
int  r2_family(void);             /* real Windows 10 2004..22H2 (apfnDispatch ABI shared by 19041..19045) */
HMODULE r2_user32(void);
int  r2_user32_text(ULONG_PTR *lo, ULONG_PTR *hi);   /* executable range of user32 .text (first exec section) */

/* ---- hooking ---------------------------------------------------------------------------------- */
#define R2_NSLOT 160
struct r2_ev {
    LONG seq;
    DWORD tid;
    unsigned idx;
    ULONG len;
    void *buf;
    ULONG_PTR r8, r9;       /* registers as KiUserCallbackDispatcher left them: r8 = ApiNumber, r9 = table */
    ULONG ncopy;
    unsigned char data[0x240];
};
#define R2_NEV 8192
extern struct r2_ev r2_ev_log[R2_NEV];
extern volatile LONG r2_ev_n;
extern volatile LONG r2_hit[R2_NSLOT];
extern unsigned r2_nlive;               /* live slots of the real table (129 on 19041) */
typedef void (*r2_ovr_fn)(void *buf, ULONG len, unsigned idx);
extern r2_ovr_fn r2_ovr[R2_NSLOT];      /* optional per-slot steering; returning chains to the original */
extern void *r2_orig[R2_NSLOT];

int  r2_install(void);                  /* 1 on success; needs the table (user32 loaded and connected) */
void r2_uninstall(void);
/* Notes a recorded callback buffer: slot, length, buffer address and the first bytes (diagnostics for layouts that differ between builds). */
void r2_dump(const char *tag, const struct r2_ev *e, unsigned nbytes);
int  r2_installed(void);
void *r2_active_table(void);            /* our private copy while installed */
void *r2_saved_table(void);             /* the real user32!apfnDispatch */
LONG r2_mark(void);
struct r2_ev *r2_find(unsigned idx, LONG from_seq, int nth);   /* nth (0-based) event of slot idx with seq >= from_seq */
int  r2_since(unsigned idx, LONG from_seq);                    /* number of such events */
/* Finish a callback by hand: result {retval, cbOut, pOut} of 0x18 bytes handed to NtCallbackReturn. */
void r2_cbreturn(ULONG_PTR retval, ULONG outlen_field, void *pout, ULONG ret_len, long status) __attribute__((noreturn));
/* True once both qword at offset off of the recorded buffer are readable. */
static inline ULONG_PTR r2_q(const struct r2_ev *e, unsigned off) {
    ULONG_PTR v = 0; if (off + 8 <= e->ncopy) memcpy(&v, e->data + off, 8); return v;
}
static inline ULONG r2_d(const struct r2_ev *e, unsigned off) {
    ULONG v = 0; if (off + 4 <= e->ncopy) memcpy(&v, e->data + off, 4); return v;
}
static inline unsigned short r2_w(const struct r2_ev *e, unsigned off) {
    unsigned short v = 0; if (off + 2 <= e->ncopy) memcpy(&v, e->data + off, 2); return v;
}

/* ---- a GUI worker thread with one window ------------------------------------------------------ */
struct r2_wk;
typedef LRESULT (*r2_wk_proc)(struct r2_wk *w, HWND h, UINT m, WPARAM wp, LPARAM lp, int *handled);
struct r2_wk {
    HANDLE thread, ready;
    volatile LONG stop;
    HWND hwnd;
    DWORD tid;
    int top_level;          /* 0: message-only window, 1: hidden off-screen popup */
    r2_wk_proc proc;        /* optional test-specific behaviour (runs on the worker thread) */
    WCHAR cls[64];
    /* recorded by the default window procedure */
    volatile LONG nuser;                 /* deliveries of messages >= WM_USER (and < 0x8000) */
    UINT last_msg; WPARAM last_wp; LPARAM last_lp; DWORD last_tid;
    ULONG_PTR ci_hwnd, ci_pwnd, ci_actctx;   /* CLIENTINFO.CallbackWnd seen inside the last user message */
    ULONG ci_dde;                        /* CLIENTINFO.cInDDEMLCallback seen inside the last user message */
};
int  r2_wk_start(struct r2_wk *w, const char *tag, int top_level);   /* 1 ok; 0 window could not be created */
void r2_wk_stop(struct r2_wk *w);
/* SendMessageTimeout wrapper: returns 1 when delivered, result in *res. Bounded (5 s). */
void *r2_wk_proc_addr(void);          /* the window procedure of every worker window */
int  r2_send(HWND h, UINT m, WPARAM wp, LPARAM lp, LRESULT *res);

/* _CLIENTINFO in the TEB (offsets verified against SfnDWORD's writes into pti->pClientInfo) */
#define R2_CI_CallbackWnd_hwnd   (TEB_Win32ClientInfo + 0x40)
#define R2_CI_CallbackWnd_pwnd   (TEB_Win32ClientInfo + 0x48)
#define R2_CI_CallbackWnd_actctx (TEB_Win32ClientInfo + 0x50)
#define R2_CI_cInDDEMLCallback   (TEB_Win32ClientInfo + 0x5c)
static inline ULONG_PTR r2_teb_q(unsigned off) { void *v = NULL; k_try_read_ptr((char *)k_teb() + off, &v); return (ULONG_PTR)v; }
static inline ULONG r2_teb_d(unsigned off) { unsigned long v = 0; k_try_read_u32((char *)k_teb() + off, &v); return (ULONG)v; }

#define R2_WK_RET(m) ((LRESULT)(0x0123456700000000LL | (long long)(m)))   /* what the default worker proc returns for user messages */

/* Pumps the calling thread's queue for up to ms (bounded). */
void r2_pump(unsigned ms);
/* Connects the process to win32k (first user32 call) so PEB->KernelCallbackTable exists. */
int  r2_gui_connect(void);

#endif
