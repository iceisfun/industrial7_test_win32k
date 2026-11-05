/* Shared declarations for the NtQueryInformationProcess protocol / debug-class / invalid-id area.
 * Everything here is resolved at run time through ntdll (LoadLibrary -> GetProcAddress); the table in
 * qp_tab.c describes, per information class that exists on Windows 10 22H2 x64, which lengths are
 * accepted, what a -1 (current process) query answers and in which order length / handle are checked. */
#ifndef QP_H
#define QP_H
#include "t.h"
#include "k.h"

#define QS_OK            ((NTSTATUS)0x00000000)
#define QS_NOT_IMPL      ((NTSTATUS)0xC0000002)
#define QS_BAD_CLASS     ((NTSTATUS)0xC0000003)
#define QS_LEN_MISMATCH  ((NTSTATUS)0xC0000004)
#define QS_ACCESS_VIOL   ((NTSTATUS)0xC0000005)
#define QS_BAD_HANDLE    ((NTSTATUS)0xC0000008)
#define QS_BAD_PARAM     ((NTSTATUS)0xC000000D)
#define QS_UNSUCCESSFUL  ((NTSTATUS)0xC0000001)
#define QS_ACCESS_DENIED ((NTSTATUS)0xC0000022)
#define QS_BUF_TOO_SMALL ((NTSTATUS)0xC0000023)
#define QS_TYPE_MISMATCH ((NTSTATUS)0xC0000024)
#define QS_PORT_NOT_SET  ((NTSTATUS)0xC0000353)
#define QS_MISALIGNED    ((NTSTATUS)0x80000002)
#define QS_REV_MISMATCH  ((NTSTATUS)0xC0000059)
#define QS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define QS_NOT_IN_JOB    ((NTSTATUS)0xC00001AE)
#define QS_OVERFLOW      ((NTSTATUS)0x80000005)
#define QS_ANY           ((NTSTATUS)0x7FFFFFFF)   /* "not checked" */

#define QP_SENT 0xDEAD1234u   /* ReturnLength sentinel */
#define QP_PSEUDO_SELF ((HANDLE)(LONG_PTR)-1)
#define QP_PSEUDO_THREAD ((HANDLE)(LONG_PTR)-2)

typedef NTSTATUS (NTAPI *qp_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
qp_fn qp_call(void);
#define QIP (qp_call())

/* ---- class table ------------------------------------------------------------------------------------ */
enum { K_SET, K_MIN, K_ALL, K_MULT, K_47, K_VAR };
#define RL_UNT  (-1L)   /* ReturnLength left alone */
#define RL_LEN  (-2L)   /* ReturnLength == requested length */
#define RL_ANY  (-3L)   /* written, value not fixed (relation checked elsewhere) */
#define RL_MINL (-4L)   /* min(len, rlp) */
#define RL_FIX  (-5L)   /* exactly rlp */
typedef struct qp_cls {
    ULONG id; const char *name;
    int kind; ULONG a, b, c;           /* K_SET: accepted lengths a,b,c (0 = unused); K_MIN: min length a; K_MULT: multiple a, max b */
    ULONG oklen; NTSTATUS okst;         /* a typical accepted length and the status a -1 query gives there */
    long rl_ok; ULONG rlp; long rl_bad; /* ReturnLength after okst / after a length mismatch */
    char need;                          /* L limited right, Q query right, Z neither self-handle right (needs more), K handle ignored, P pseudo handle only */
    NTSTATUS n_ok, n_3, n_0;            /* status for an invalid handle at oklen / length 3 / length 0 (c0000008 = handle lookup failed) */
} qp_cls;
extern const qp_cls qp_classes[];
extern const int qp_nclasses;
extern const ULONG qp_gap_ids[];
extern const int qp_ngap;
const qp_cls *qp_find(ULONG id);
NTSTATUS qp_okst(const qp_cls *c, HANDLE h);                /* status at oklen for handle h (job-dependent for one class) */
NTSTATUS qp_exp_len(const qp_cls *c, ULONG len, HANDLE h);   /* expected status for a handle that passes, any length */
long     qp_exp_rl(const qp_cls *c, ULONG len, NTSTATUS st, ULONG *mod);  /* -1 untouched, -3 written (value free, *mod = required modulus), else exact value */
int      qp_is_var(const qp_cls *c);

/* page-aligned scratch buffer, 0x4000 bytes, zeroed by qp_buf() */
unsigned char *qp_buf(void);
/* one query with a sentinel return length; *rl_state: 0 untouched, 1 written */
NTSTATUS qp_q(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl, int *rl_written);
void qp_cleanup_out(const qp_cls *c, NTSTATUS st, void *buf);   /* closes handles a successful query returned */

/* ---- handle kinds ----------------------------------------------------------------------------------- */
enum { H_NULL, H_THR, H_M3, H_CLOSED, H_EVENT, H_THREAD, H_FAR, H_SYNC, H_ZERO, H_LIM, H_QRY, H_CUR,
       H_CH_LIM, H_CH_QRY, H_CH_SYNC, H_CH_ZERO, H_NKINDS };
extern const char *qp_hname[H_NKINDS];
/* Returns a handle of the kind (caller releases it with qp_hrelease). Child kinds need a live child. */
HANDLE qp_hmake(int kind, HANDLE child);
void qp_hrelease(int kind, HANDLE h);
NTSTATUS qp_exp_handle(const qp_cls *c, int kind, ULONG len, HANDLE h);

/* ---- helper processes -------------------------------------------------------------------------------- */
/* idle child (mode 'n' plain, 'f' sets its own debug-flags to NoDebugInherit first); waits until ready. NULL on failure. */
HANDLE qp_spawn(char mode, PROCESS_INFORMATION *pi);
void qp_kill(HANDLE h, PROCESS_INFORMATION *pi);
/* run the query for class/len with -1 inside a child whose token has the given mandatory integrity RID */
int qp_run_il(DWORD rid, ULONG cls, ULONG len, NTSTATUS *st);
int qp_type_is(HANDLE h, const wchar_t *name);   /* NtQueryObject type name compare */
int qp_integrity(DWORD *rid);
int qp_match(NTSTATUS want, NTSTATUS got);
#define T5(n) TEST(x_ntqip_qip_debug_protocol_invalid_##n)
void qp_run_var(const qp_cls *c);   /* variable-size classes (qp_var.c) */
#define QP_NEED(c) const qp_cls *c = qp_find(CID); if (!QIP) { t_skip("NtQueryInformationProcess missing"); return; } if (!c) { CHECKF(0, "class missing from table"); return; }
#endif
