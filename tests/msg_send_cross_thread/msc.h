/* msg_send_cross_thread: shared fixtures.
 *
 * The send family reaches the kernel in three shapes (all analysis-verified):
 *   - same-thread SendMessageW/A to a window of the calling thread never enters win32k: user32!
 *     SendMessageWorker calls the window procedure directly (it compares the window's thread
 *     pointer with the caller's).
 *   - cross-thread / cross-process: the message goes through win32u!NtUserMessageCall (win32kfull!
 *     NtUserMessageCall -> per-message NtUserfn* handler for msg < 0x400, otherwise the fnid table)
 *     and then the kernel send path, which queues the message on the
 *     receiver and sleeps the sender until the receiver replies.
 *   - SendMessageTimeout / Callback / Notify pass fnid 0x2B3 / 0x2B8 / 0x2B7 (user32 call sites).
 *
 * Every user32 API used here is resolved through U() on first use (msc_api()), so the exe has no
 * link-time dependency on the module under test.
 */
#ifndef MSC_H
#define MSC_H
#include "t.h"
#include "k.h"

#ifndef ISMEX_NOSEND
#define ISMEX_NOSEND   0x00000000
#define ISMEX_SEND     0x00000001
#define ISMEX_NOTIFY   0x00000002
#define ISMEX_CALLBACK 0x00000004
#define ISMEX_REPLIED  0x00000008
#endif

/* fnid values the real user32 callers pass as NtUserMessageCall's 6th argument. */
#define MSC_FNID_SENDMESSAGE        0x2B1  /* user32!SendMessageW/A, HWND_BROADCAST path (0x2B2 is a different send variant: it answers 1 without dispatching) */
#define MSC_FNID_SENDMESSAGETIMEOUT 0x2B3  /* user32!SendMessageTimeoutW/A */
#define MSC_FNID_SENDNOTIFYMESSAGE  0x2B7  /* user32!SendNotifyMessageW/A */
#define MSC_FNID_SENDMESSAGECALLBACK 0x2B8 /* user32!SendMessageCallbackW/A */

/* ---- API table ------------------------------------------------------------------------- */
struct msc_api_t {
    __typeof__(&RegisterClassExW) RegisterClassExW;
    __typeof__(&RegisterClassExA) RegisterClassExA;
    __typeof__(&CreateWindowExW) CreateWindowExW;
    __typeof__(&CreateWindowExA) CreateWindowExA;
    __typeof__(&DestroyWindow) DestroyWindow;
    __typeof__(&SendMessageW) SendMessageW;
    __typeof__(&SendMessageA) SendMessageA;
    __typeof__(&SendMessageTimeoutW) SendMessageTimeoutW;
    __typeof__(&SendMessageTimeoutA) SendMessageTimeoutA;
    __typeof__(&SendNotifyMessageW) SendNotifyMessageW;
    __typeof__(&SendNotifyMessageA) SendNotifyMessageA;
    __typeof__(&SendMessageCallbackW) SendMessageCallbackW;
    __typeof__(&SendMessageCallbackA) SendMessageCallbackA;
    __typeof__(&ReplyMessage) ReplyMessage;
    __typeof__(&InSendMessage) InSendMessage;
    __typeof__(&InSendMessageEx) InSendMessageEx;
    __typeof__(&PostMessageW) PostMessageW;
    __typeof__(&PostThreadMessageW) PostThreadMessageW;
    __typeof__(&PeekMessageW) PeekMessageW;
    __typeof__(&DispatchMessageW) DispatchMessageW;
    __typeof__(&DefWindowProcW) DefWindowProcW;
    __typeof__(&DefWindowProcA) DefWindowProcA;
    __typeof__(&GetWindowTextW) GetWindowTextW;
    __typeof__(&SetWindowTextW) SetWindowTextW;
    __typeof__(&IsWindow) IsWindow;
    __typeof__(&IsWindowUnicode) IsWindowUnicode;
    __typeof__(&GetWindowLongPtrW) GetWindowLongPtrW;
    __typeof__(&SetWindowLongPtrW) SetWindowLongPtrW;
    __typeof__(&RegisterWindowMessageW) RegisterWindowMessageW;
    __typeof__(&GetQueueStatus) GetQueueStatus;
    __typeof__(&IsHungAppWindow) IsHungAppWindow;
    __typeof__(&FindWindowW) FindWindowW;
    __typeof__(&GetWindowThreadProcessId) GetWindowThreadProcessId;
    __typeof__(&ChangeWindowMessageFilterEx) ChangeWindowMessageFilterEx;
    __typeof__(&MsgWaitForMultipleObjectsEx) MsgWaitForMultipleObjectsEx;
    __typeof__(&GetDesktopWindow) GetDesktopWindow;
    __typeof__(&SetWindowPos) SetWindowPos;
    __typeof__(&GetWindowRect) GetWindowRect;
    __typeof__(&GetClientRect) GetClientRect;
    __typeof__(&UnregisterClassW) UnregisterClassW;
    __typeof__(&GetClassNameW) GetClassNameW;
    BOOL (WINAPI *QuerySendMessage)(MSG *);
    ULONG_PTR (WINAPI *NtUserGetThreadState)(DWORD);
    int ok;
};
struct msc_api_t *msc_api(void);
#ifndef MSC_NO_MACROS
#define RegisterClassExW (msc_api()->RegisterClassExW)
#define RegisterClassExA (msc_api()->RegisterClassExA)
#define CreateWindowExW (msc_api()->CreateWindowExW)
#define CreateWindowExA (msc_api()->CreateWindowExA)
#define DestroyWindow (msc_api()->DestroyWindow)
#define SendMessageW (msc_api()->SendMessageW)
#define SendMessageA (msc_api()->SendMessageA)
#define SendMessageTimeoutW (msc_api()->SendMessageTimeoutW)
#define SendMessageTimeoutA (msc_api()->SendMessageTimeoutA)
#define SendNotifyMessageW (msc_api()->SendNotifyMessageW)
#define SendNotifyMessageA (msc_api()->SendNotifyMessageA)
#define SendMessageCallbackW (msc_api()->SendMessageCallbackW)
#define SendMessageCallbackA (msc_api()->SendMessageCallbackA)
#define ReplyMessage (msc_api()->ReplyMessage)
#define InSendMessage (msc_api()->InSendMessage)
#define InSendMessageEx (msc_api()->InSendMessageEx)
#define PostMessageW (msc_api()->PostMessageW)
#define PostThreadMessageW (msc_api()->PostThreadMessageW)
#define PeekMessageW (msc_api()->PeekMessageW)
#define DispatchMessageW (msc_api()->DispatchMessageW)
#define DefWindowProcW (msc_api()->DefWindowProcW)
#define DefWindowProcA (msc_api()->DefWindowProcA)
#define GetWindowTextW (msc_api()->GetWindowTextW)
#define SetWindowTextW (msc_api()->SetWindowTextW)
#define IsWindow (msc_api()->IsWindow)
#define IsWindowUnicode (msc_api()->IsWindowUnicode)
#define GetWindowLongPtrW (msc_api()->GetWindowLongPtrW)
#define SetWindowLongPtrW (msc_api()->SetWindowLongPtrW)
#define RegisterWindowMessageW (msc_api()->RegisterWindowMessageW)
#define GetQueueStatus (msc_api()->GetQueueStatus)
#define IsHungAppWindow (msc_api()->IsHungAppWindow)
#define FindWindowW (msc_api()->FindWindowW)
#define GetWindowThreadProcessId (msc_api()->GetWindowThreadProcessId)
#define ChangeWindowMessageFilterEx (msc_api()->ChangeWindowMessageFilterEx)
#define MsgWaitForMultipleObjectsEx (msc_api()->MsgWaitForMultipleObjectsEx)
#define GetDesktopWindow (msc_api()->GetDesktopWindow)
#define SetWindowPos (msc_api()->SetWindowPos)
#define GetWindowRect (msc_api()->GetWindowRect)
#define GetClientRect (msc_api()->GetClientRect)
#define UnregisterClassW (msc_api()->UnregisterClassW)
#define GetClassNameW (msc_api()->GetClassNameW)
#endif

/* Direct win32u syscall: NtUserMessageCall(hwnd, msg, wParam, lParam, xParam, fnid, ansi).
 * Signature from win32kfull!NtUserMessageCall (7 args; the 5th is a pointer-sized xParam, the
 * 6th the fnid, the 7th the ANSI flag normalised to bool before the handler call). */
UDECL(LRESULT, NtUserMessageCall, (HWND, UINT, WPARAM, LPARAM, ULONG_PTR, DWORD, DWORD));
/* SendMessageTimeout's xParam block as built by user32!SendMessageTimeoutW (rsp+0x40..0x57) and
 * consumed by the kernel send path. */
struct msc_smto { UINT flags; UINT timeout; ULONG_PTR ret; ULONG_PTR result; };
/* SendMessageCallback's xParam block read by the kernel callback-send path. */
struct msc_smcb { SENDASYNCPROC cb; ULONG_PTR data; };

/* ---- message vocabulary understood by msc_core() --------------------------------------- */
#define MSC_ECHO        (WM_USER + 1)   /* returns msc_echo_ret(w,l), logs */
#define MSC_REPLY       (WM_USER + 2)   /* ReplyMessage(0x1234..) twice, then sleeps wParam ms */
#define MSC_SLEEP       (WM_USER + 3)   /* Sleep(wParam ms), returns 7 */
#define MSC_NEST        (WM_USER + 4)   /* SendMessage(beh->peer, MSC_ECHO, wParam, 0) inside the proc */
#define MSC_PROBE       (WM_USER + 5)   /* SendMessageTimeout(beh->peer, MSC_ECHO, 0,0, flags=wParam, timeout=lParam) inside the proc */
#define MSC_SELFDESTROY (WM_USER + 6)   /* DestroyWindow(self) inside the proc, returns 5 */
#define MSC_EXITTHREAD  (WM_USER + 7)   /* ExitThread(0) inside the proc */
#define MSC_QUERY       (WM_USER + 8)   /* returns number of logged MSC_ECHO so far */
#define MSC_QSM         (WM_USER + 14)  /* QuerySendMessage + NtUserGetThreadState(8) from inside the procedure */
#define MSC_COUNT       (WM_USER + 9)   /* returns msc_log_count(wParam) of the receiving process */
#define MSC_POSTED      (WM_USER + 10)
#define MSC_SENT        (WM_USER + 11)
#define MSC_PREPOST     (WM_USER + 12)  /* PostMessage(self, MSC_POSTED), signals, waits for MSC_SENT to queue */
#define MSC_DEPTH       (WM_USER + 13)  /* nested ping-pong: wParam = remaining depth, lParam = peer hwnd */
#define MSC_QUIT        (WM_USER + 0x77) /* cross-process server: ends its loop */
#define MSC_EXITPROC    (WM_USER + 0x66) /* cross-process server: ExitProcess(3) inside the proc */
#define MSC_ECHO_XOR    0xA5A5A5A5A5A5A5A5ULL
static inline LRESULT msc_echo_ret(WPARAM w, LPARAM l) { return (LRESULT)(((ULONGLONG)w ^ (ULONGLONG)l) ^ MSC_ECHO_XOR); }

/* ---- event log (thread-safe, filled by the window procedures) -------------------------- */
#define MSC_LOG_MAX 1024
struct msc_ev {
    DWORD ciflags; BOOL ci_ok;   /* flag word behind TEB+0x860 (CLIENTINFO), see InSendMessage */
    UINT msg; WPARAM w; LPARAM l; HWND hwnd; DWORD tid; DWORD ismex; BOOL insend; int ansi; LONG seq;
    LONGLONG qpc;
};
void msc_log_reset(void);
int  msc_log_count(UINT msg);
int  msc_log_total(void);
BOOL msc_log_find(UINT msg, int nth, struct msc_ev *out); /* nth occurrence of msg, 0-based */
BOOL msc_log_wait(UINT msg, int count, DWORD timeout_ms);  /* until count occurrences of msg logged */
LONGLONG msc_qpc(void);
double msc_ms_since(LONGLONG start);

/* ---- per-window behaviour block (GWLP_USERDATA, set from CREATESTRUCT.lpCreateParams) --- */
struct msc_beh {
    int logging;                 /* log every message this window gets */
    int def_text;                /* pass WM_SETTEXT/GETTEXT/GETTEXTLENGTH on to DefWindowProc (default 1) */
    HWND peer;                   /* for MSC_NEST / MSC_PROBE / MSC_DEPTH */
    HANDLE done;                 /* auto-reset event set when an MSC_REPLY/MSC_SLEEP/MSC_PREPOST handler finishes */
    HANDLE go;                   /* MSC_PREPOST: handler waits for this before returning */
    /* capture of the last string-bearing message (WM_SETTEXT, LB_ADDSTRING, ...) */
    int has_str; UINT str_msg; WCHAR wstr[256]; char astr[256]; LPARAM str_ptr;
    /* capture of the last "struct" message selected by cap_msg */
    UINT cap_msg; size_t cap_len; unsigned char cap[512]; BOOL cap_ok; LPARAM cap_ptr; WPARAM cap_w; LONG cap_hits;
    size_t mut_off; DWORD mut_val; int mut_on;   /* after capture, write mut_val at lParam+mut_off */
    LRESULT cap_ret;                              /* what the capture handler returns */
    int wr_wl; DWORD wr_w_val, wr_l_val;          /* write DWORDs through wParam / lParam pointers (EM_GETSEL style out params) */
    /* char capture (WM_CHAR family) */
    UINT chr_msg; WPARAM chr_w; LONG chr_hits;
    /* ReplyMessage test */
    BOOL rep1, rep2; DWORD rep_err; DWORD ismex_before, ismex_after; BOOL insend_after;
    /* nested / probe results */
    LRESULT nest_ret; DWORD nest_err; LONG nest_hits; LRESULT probe_ret; DWORD probe_err; ULONG_PTR probe_res;
    /* WM_COPYDATA capture */
    ULONG_PTR cd_data; DWORD cd_cb; void *cd_lp; unsigned char cd_bytes[512]; BOOL cd_ok; WPARAM cd_w; LRESULT cd_ret; int cd_scribble;
    /* NCCALCSIZE capture */
    RECT nc_rect[3]; WINDOWPOS nc_wp; BOOL nc_wp_ok; WPARAM nc_w; LONG nc_hits;
    /* MSC_QSM results */
    BOOL qsm_ret, qsm_null_ret; MSG qsm_msg; ULONG_PTR gts8; DWORD qsm_err;
    int ansi;
};
LRESULT CALLBACK msc_wndproc_w(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK msc_wndproc_a(HWND, UINT, WPARAM, LPARAM);
const WCHAR *msc_class_w(void);   /* Unicode window class name (registered with RegisterClassExW) */
const WCHAR *msc_class_a(void);   /* window class name registered with RegisterClassExA (ANSI proc) */
BOOL msc_register(void);          /* idempotent */
HWND msc_create(const struct msc_beh *init_template, struct msc_beh **out_beh, int ansi, HWND parent); /* in the calling thread */
HWND msc_create_ex(const struct msc_beh *init_template, struct msc_beh **out_beh, int ansi, HWND parent, const WCHAR *title);
extern volatile LONG msc_quit_flag;           /* set by MSC_QUIT */
ULONGLONG msc_fnv(const void *p, size_t n, ULONGLONG seed);
#define MSC_FNV_SEED 14695981039346656037ULL
struct msc_beh *msc_beh_of(HWND h);
void msc_beh_default(struct msc_beh *b);

/* ---- worker thread -------------------------------------------------------------------- */
#define MSCW_PAUSE     0x01   /* thread waits on ->go before it starts pumping */
#define MSCW_CONTROLS  0x02   /* also create LISTBOX / COMBOBOX / EDIT children of ->w */
#define MSCW_NOPUMP    0x04   /* thread sleeps ->nopump_ms (no message retrieval) before pumping */
struct msc_worker {
    HANDLE th, ready, go;
    DWORD tid; volatile LONG quit; unsigned flags; DWORD nopump_ms;
    HWND w, wa, wm, lb, cb, ed;      /* Unicode, ANSI, message-only windows, controls */
    struct msc_beh *beh, *beha, *behm;
    int created;
    volatile LONG pumping;           /* 1 once the loop is running */
    LONGLONG pump_start_qpc;
};
BOOL msc_worker_start(struct msc_worker *wk, unsigned flags, DWORD nopump_ms);
void msc_worker_stop(struct msc_worker *wk);
/* Pump the calling thread's queue for up to ms (dispatches sent + posted). */
void msc_pump(unsigned ms);
/* Pump until *flag != 0 or timeout; returns the flag. */
int  msc_pump_until(volatile LONG *flag, unsigned timeout_ms);

/* A helper thread that performs one SendMessageW and records the outcome. */
struct msc_sender {
    HANDLE th; HWND target; UINT msg; WPARAM w; LPARAM l; DWORD delay_ms;
    LRESULT ret; DWORD err; volatile LONG started, finished; LONGLONG t_enter, t_leave; int notify;
};
BOOL msc_sender_start(struct msc_sender *s);
BOOL msc_sender_join(struct msc_sender *s, DWORD ms);

/* ---- misc helpers --------------------------------------------------------------------- */
BOOL msc_have_gui(void);               /* user32 usable and a window can be created */
void msc_utf8_to_w(const char *s, WCHAR *out, int cap);
int  msc_ansi_of_w(const WCHAR *w, char *out, int cap);  /* WideCharToMultiByte(CP_ACP), returns bytes incl NUL */
int  msc_w_of_ansi(const char *a, WCHAR *out, int cap);  /* MultiByteToWideChar(CP_ACP), returns chars incl NUL */
/* EXPECT_EQ evaluates its operands twice (value and message); EQ evaluates each exactly once, which
 * matters for expressions that send messages. */
#define EQ(a, b) do { ULONG_PTR a_ = (ULONG_PTR)(a), b_ = (ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", (unsigned long long)a_, (unsigned long long)b_); } while (0)
#define MSC_NEED_GUI() do { if (!msc_have_gui()) { t_skip("no usable window station / user32"); return; } msc_log_reset(); } while (0)
#define MSC_NEED_WORKER(wk, flags, nop) do { if (!msc_worker_start(&(wk), (flags), (nop))) { t_skip("could not start a worker GUI thread"); return; } msc_log_reset(); } while (0)
/* Hard-check helper for tests that are Windows-only because Wine's SMS queue differs. */
#endif
