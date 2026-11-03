/* Shared fixtures for the thread-message-queue area (NtUserPostMessage / PostThreadMessage /
 * PeekMessage / GetMessage / GetQueueStatus / MsgWaitForMultipleObjectsEx / WaitMessage ...).
 *
 * Most tests run their body on a FRESH thread (QTEST): a thread's win32k queue is created by its
 * first win32k call and is private to it (THREADINFO + its own QMSG list, +0x328/+0x330 in
 * win32kfull!_PostTransformableMessageExtended), so a fresh thread gives every test an empty queue
 * without cross-test state. The body must therefore never use t_skip (longjmp across threads).
 */
#ifndef MQ_H
#define MQ_H
#include "k.h"

#ifndef QS_KEY
#define QS_KEY 0x0001
#define QS_MOUSEMOVE 0x0002
#define QS_MOUSEBUTTON 0x0004
#define QS_POSTMESSAGE 0x0008
#define QS_TIMER 0x0010
#define QS_PAINT 0x0020
#define QS_SENDMESSAGE 0x0040
#define QS_HOTKEY 0x0080
#define QS_ALLPOSTMESSAGE 0x0100
#endif
#ifndef QS_RAWINPUT
#define QS_RAWINPUT 0x0400
#endif
#ifndef QS_TOUCH
#define QS_TOUCH 0x0800
#define QS_POINTER 0x1000
#endif
#ifndef PM_QS_INPUT
#define PM_QS_INPUT (QS_INPUT << 16)
#define PM_QS_POSTMESSAGE ((QS_POSTMESSAGE | QS_HOTKEY | QS_TIMER) << 16)
#define PM_QS_PAINT (QS_PAINT << 16)
#define PM_QS_SENDMESSAGE (QS_SENDMESSAGE << 16)
#endif
#ifndef PM_NOYIELD
#define PM_NOYIELD 0x0002
#endif
#ifndef MWMO_WAITALL
#define MWMO_WAITALL 0x0001
#define MWMO_ALERTABLE 0x0002
#define MWMO_INPUTAVAILABLE 0x0004
#endif
#ifndef ISMEX_NOSEND
#define ISMEX_NOSEND 0
#define ISMEX_SEND 1
#define ISMEX_NOTIFY 2
#define ISMEX_CALLBACK 4
#define ISMEX_REPLIED 8
#endif

#define MQ_ERR_INVALID_PARAMETER 87
#define MQ_ERR_INVALID_FLAGS 1004
#define MQ_ERR_INVALID_WINDOW_HANDLE 1400
#define MQ_ERR_INVALID_THREAD_ID 1444
#define MQ_ERR_NOT_ENOUGH_QUOTA 1816
#define MQ_ERR_MESSAGE_SYNC_ONLY 1159

/* win32u.dll stubs (no SDK declaration). Argument roles from win32kfull!NtUser* analysis. */
UDECL(LONG, NtUserPostMessage, (HWND, UINT, WPARAM, LPARAM));
UDECL(LONG, NtUserPostThreadMessage, (DWORD, UINT, WPARAM, LPARAM));
UDECL(LONG, NtUserPeekMessage, (MSG *, HWND, UINT, UINT, UINT, UINT));
UDECL(LONG, NtUserGetMessage, (MSG *, HWND, UINT, UINT));
UDECL(LONG, NtUserWaitMessage, (void));
UDECL(DWORD, NtUserGetQueueStatus, (UINT));
UDECL(ULONG_PTR, NtUserGetThreadState, (LONG));
UDECL(LONG, NtUserTranslateMessage, (const MSG *, UINT));
UDECL(LRESULT, NtUserDispatchMessage, (const MSG *));
UDECL(LONG, NtUserAttachThreadInput, (DWORD, DWORD, LONG));
UDECL(DWORD, NtUserMsgWaitForMultipleObjectsEx, (DWORD, const HANDLE *, DWORD, DWORD, DWORD));
UDECL(LONG, NtUserQuerySendMessage, (MSG *));

/* thread-state selectors of NtUserGetThreadState (win32kfull!NtUserGetThreadState switch) */
#define TS_ACTIVE 0
#define TS_FOCUS 1
#define TS_CAPTURE 2
#define TS_INPUTSTATE 5
#define TS_CHANGEBITS 6
#define TS_EXTRAINFO 7
#define TS_INSENDMSG 8
#define TS_MSGTIME 9

/* ---- window + log fixture ---------------------------------------------------- */
#define MQ_LOGMAX 1024
typedef struct { HWND h; UINT m; WPARAM w; LPARAM l; DWORD tid; } mq_rec;
extern mq_rec mq_log[MQ_LOGMAX];
extern volatile LONG mq_n;
/* optional hook called from the window procedure; return nonzero and set *r to override */
typedef int (*mq_hook_fn)(HWND, UINT, WPARAM, LPARAM, LRESULT *r);
extern mq_hook_fn mq_hook;

BOOL mq_user(void);                    /* all user32 entry points the fixtures use resolved */
void mq_reset(void);
int  mq_count(UINT msg);
int  mq_find(UINT msg, int from);
HWND mq_win(void);                     /* hidden off-screen tool window of this thread */
HWND mq_win_visible(void);             /* same but shown with SW_SHOWNOACTIVATE (for WM_PAINT) */
HWND mq_msgwin(void);                  /* message-only window */
UINT mq_regmsg(const char *tag);       /* RegisterWindowMessage of a name unique to this process */
int  mq_peek(MSG *m, HWND h, UINT lo, UINT hi, UINT fl);
int  mq_drain(void);                   /* remove all posted/timer messages of this thread; returns count */
BOOL mq_post(HWND h, UINT m, WPARAM w, LPARAM l);
DWORD mq_status(UINT f);
/* GetMessage that cannot block: fails (-2) unless a matching message is already queued */
LONG mq_getmsg(MSG *m, HWND h, UINT lo, UINT hi);
int  mq_wait(DWORD n, const HANDLE *h, DWORD ms, DWORD mask, DWORD fl);

/* run body on a fresh thread; used by QTEST */
typedef void (*mq_body)(void);
void mq_run(mq_body b);
#define QTEST(name) \
    static void qb_##name(void); \
    TEST(name) { if (!mq_user()) { t_skip("user32 entry points missing"); return; } mq_run(qb_##name); } \
    static void qb_##name(void)
#define QNEED(p) do { if (!(p)) { t_note("missing export: " #p); return; } } while (0)

/* a started helper thread that owns a queue (created by its first PeekMessage) */
typedef struct mq_helper {
    HANDLE th; DWORD tid;
    HANDLE ready;   /* signaled once the helper has created its queue (and window) */
    HANDLE go;      /* auto-reset: the controller releases the helper's next step */
    HANDLE done;    /* signaled when the helper finished its step */
    HWND hwnd;      /* window of the helper */
    volatile LONG stop;
    void *arg;
    LONG result[8];
} mq_helper;
typedef DWORD (WINAPI *mq_helper_fn)(mq_helper *);
BOOL mq_helper_start(mq_helper *h, mq_helper_fn fn, void *arg);
void mq_helper_finish(mq_helper *h);   /* join with bounded wait */
/* single-evaluation versions of EXPECT_EQ/EXPECT_NE (the harness macros evaluate their arguments twice,
 * which corrupts calls such as GetQueueStatus that have side effects) */
#define EQ(a, b) do { ULONG_PTR a_ = (ULONG_PTR)(a), b_ = (ULONG_PTR)(b); t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", (unsigned long long)a_, (unsigned long long)b_); } while (0)
#define NE(a, b) do { ULONG_PTR a_ = (ULONG_PTR)(a), b_ = (ULONG_PTR)(b); t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", (unsigned long long)a_); } while (0)
/* Wine's win32u exports NtUserQuerySendMessage as an aborting stub */
#define QSM() (t_is_wine() ? (NtUserQuerySendMessage_pfn)NULL : UN(NtUserQuerySendMessage))
/* soft expectation: a mismatch is logged as NOTE (build/locale dependent), never fails */
#define SOFT_EQ(a, b, what) do { ULONG_PTR a_ = (ULONG_PTR)(a), b_ = (ULONG_PTR)(b); if (a_ != b_) t_note("%s: got 0x%llx expected 0x%llx", what, (unsigned long long)a_, (unsigned long long)b_); } while (0)
#endif
