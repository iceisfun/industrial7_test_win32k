/* Class table and helpers for the NtQueryInformationProcess protocol tests (see qp.h).
 * All rows were taken from behaviour observed on Windows 10 22H2 x64 (build 19045); labels are neutral
 * descriptions, never the community names of the classes. */
#include "qp.h"

qp_fn qp_call(void) {
    static qp_fn f;
    if (!f) f = (qp_fn)t_proc_in("ntdll.dll", "NtQueryInformationProcess");
    return f;
}
unsigned char *qp_buf(void) {
    static unsigned char *b;
    if (!b) b = VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (b) memset(b, 0, 0x4000);
    return b;
}

#define C8 QS_BAD_HANDLE
#define C4 QS_LEN_MISMATCH
#define C22 QS_ACCESS_DENIED
#define F8 C8, C4, C4         /* length is checked before the handle */
#define H8 C8, C8, C8         /* handle is looked at first */
/* id, label, kind, a, b, c, oklen, okst, rl_ok, rlp, rl_bad, need, null-handle statuses */
const qp_cls qp_classes[] = {
 {0,  "basic",            K_SET, 0x30, 0x40, 0,   0x30,  QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {1,  "quota",            K_SET, 0x30, 0x58, 0,   0x30,  QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {2,  "io counters",      K_SET, 0x30, 0, 0,      0x30,  QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {3,  "vm counters",      K_SET, 0x58, 0x60, 0x70,0x58,  QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {4,  "times",            K_SET, 0x20, 0, 0,      0x20,  QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {7,  "debug port",       K_SET, 8, 0, 0,         8,     QS_OK, RL_LEN, 0, RL_UNT, 'Q', F8},
 {10, "ldt",              K_ALL, 0, 0, 0,         0x10,  QS_NOT_IMPL, RL_UNT, 0, RL_UNT, 'Z', H8},
 {12, "error mode",       K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {14, "pooled usage",     K_SET, 0x48, 0, 0,      0x48,  QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {15, "ws watch",         K_ALL, 0, 0, 0,         0x10,  QS_UNSUCCESSFUL, RL_UNT, 0, RL_UNT, 'Q', H8},
 {18, "priority class",   K_SET, 2, 0, 0,         2,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {19, "wow64 flag",       K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'Q', F8},
 {20, "handle count",     K_SET, 4, 8, 0,         4,     QS_OK, RL_MINL, 4, RL_UNT, 'L', F8},
 {21, "affinity",         K_SET, 8, 0x10, 0,      8,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {22, "boost disabled",   K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {23, "device map",       K_SET, 0x24, 0x30, 0,   0x24,  QS_OK, RL_LEN, 0, RL_UNT, 'Q', F8},
 {24, "session",          K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {26, "wow64 info",       K_SET, 8, 0, 0,         8,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {27, "image path nt",    K_VAR, 0, 0, 0,         0x400, QS_OK, RL_ANY, 0, RL_ANY, 'L', H8},
 {28, "luid maps",        K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'K', QS_OK, C4, C4},
 {29, "break on term",    K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {30, "debug object",     K_SET, 8, 0, 0,         8,     QS_PORT_NOT_SET, RL_LEN, 0, RL_UNT, 'Q', F8},
 {31, "debug flags",      K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'Q', F8},
 {32, "handle tracing",   K_MIN, 0x10, 0, 0,      0x10,  QS_BAD_PARAM, RL_UNT, 0, RL_UNT, 'Q', F8},
 {33, "io priority",      K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {34, "execute flags",    K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'Q', F8},
 {36, "cookie",           K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'Z', F8},
 {37, "image info",       K_SET, 0x40, 0, 0,      0x40,  QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {38, "cycle time",       K_SET, 0x10, 0, 0,      0x10,  QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {39, "page priority",    K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {42, "ws watch ex",      K_MULT, 0x20, 0, 0,     0x20,  QS_UNSUCCESSFUL, RL_UNT, 0, RL_UNT, 'Q', C8, C4, C8},
 {43, "image path dos",   K_VAR, 0, 0, 0,         0x400, QS_OK, RL_ANY, 0, RL_ANY, 'L', H8},
 {44, "image mapping",    K_SET, 8, 0, 0,         8,     QS_BAD_HANDLE, RL_UNT, 0, RL_UNT, 'Q', F8},
 {45, "affinity update",  K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {46, "alloc mode",       K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {47, "group info",       K_47,  0, 0, 0,         2,     QS_OK, RL_FIX, 2, RL_UNT, 'L', C8, QS_BAD_PARAM, C8},
 {49, "console host",     K_SET, 8, 0, 0,         8,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {50, "window info",      K_VAR, 0, 0, 0,         0x400, QS_OK, RL_ANY, 0, RL_ANY, 'L', H8},
 {51, "handle snapshot",  K_VAR, 0, 0, 0,         0x10,  QS_LEN_MISMATCH, RL_ANY, 0, RL_ANY, 'Q', F8},
 {52, "mitigation",       K_SET, 8, 0, 0,         8,     QS_NOT_SUPPORTED, RL_UNT, 0, RL_UNT, 'L', F8},
 {54, "handle check",     K_SET, 4, 0, 0,         4,     QS_OK, RL_UNT, 0, RL_UNT, 'Q', F8},
 {55, "keep alive",       K_SET, 8, 0, 0,         8,     QS_OK, RL_UNT, 0, RL_UNT, 'Q', F8},
 {58, "handle table",     K_MIN, 4, 0, 0,         0x40,  QS_OK, RL_ANY, 4, RL_UNT, 'Z', F8},
 {59, "stack extents",    K_SET, 4, 0, 0,         4,     QS_OK, RL_UNT, 0, RL_UNT, 'Q', F8},
 {60, "command line",     K_VAR, 0, 0, 0,         0x400, QS_OK, RL_ANY, 0, RL_ANY, 'L', H8},
 {61, "protection",       K_MIN, 1, 0, 0,         1,     QS_OK, RL_FIX, 1, RL_UNT, 'L', H8},
 {64, "telemetry id",     K_VAR, 0, 0, 0,         0x400, QS_OK, RL_ANY, 0, RL_ANY, 'L', H8},
 {65, "commit release",   K_SET, 0x20, 0, 0,      0x20,  QS_REV_MISMATCH, RL_UNT, 0, RL_UNT, 'K', QS_REV_MISMATCH, C4, C4},
 {66, "default cpu sets", K_MULT, 8, 0xa0, 0,     8,     QS_OK, RL_ANY, 8, RL_UNT, 'L', C8, C4, C8},
 {67, "allowed cpu sets", K_MULT, 8, 0xa0, 0,     8,     QS_OK, RL_ANY, 8, RL_UNT, 'L', C8, C4, C8},
 {69, "job memory",       K_SET, 0x28, 0, 0,      0x28,  QS_NOT_IN_JOB, RL_LEN, 0, RL_UNT, 'L', F8},
 {70, "in-private",       K_SET, 1, 0, 0,         1,     QS_OK, RL_UNT, 0, RL_UNT, 'L', F8},
 {71, "raise on close",   K_SET, 4, 0, 0,         4,     QS_OK, RL_UNT, 0, RL_UNT, 'L', F8},
 {72, "secure challenge", K_ALL, 0, 0, 0,         0x10,  (NTSTATUS)0xC000009D, RL_UNT, 0, RL_UNT, 'L', H8},
 {73, "child process",    K_SET, 3, 0, 0,         3,     QS_OK, RL_LEN, 0, RL_UNT, 'Q', C8, C8, C4},
 {74, "high gfx prio",    K_SET, 1, 0, 0,         1,     QS_OK, RL_UNT, 0, RL_UNT, 'L', F8},
 {75, "subsystem info",   K_SET, 4, 0, 0,         4,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {76, "energy values",    K_ALL, 0, 0, 0,         0x1b0, QS_OK, RL_FIX, 0x1b0, RL_UNT, 'L', H8},
 {77, "throttle state",   K_SET, 0xc, 0, 0,       0xc,   QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {79, "win32k filter",    K_SET, 8, 0, 0,         8,     QS_OK, RL_LEN, 0, RL_UNT, 'Q', F8},
 {81, "wake info",        K_ALL, 0, 0, 0,         0x30,  QS_ACCESS_DENIED, RL_UNT, 0, RL_UNT, 'K', C22, C22, C22},
 {82, "energy tracking",  K_MIN, 8, 0, 0,         8,     QS_OK, RL_MINL, 0x90, RL_UNT, 'L', F8},
 {84, "live dump",        K_SET, 4, 0, 0,         4,     QS_ACCESS_DENIED, RL_UNT, 0, RL_UNT, 'Q', F8},
 {85, "telemetry cover",  K_VAR, 0, 0, 0,         0x400, QS_ANY, RL_ANY, 0, RL_ANY, 'L', H8},
 {87, "rw logging",       K_MIN, 1, 0, 0,         1,     QS_OK, RL_UNT, 0, RL_UNT, 'L', C8, C8, C4},
 {88, "freeze stats",     K_ALL, 0, 0, 0,         0x38,  QS_OK, RL_MINL, 0x38, RL_UNT, 'L', H8},
 {89, "image section",    K_SET, 8, 0, 0,         8,     QS_OK, RL_LEN, 0, RL_UNT, 'P', C22, C4, C4},
 {92, "sequence number",  K_SET, 8, 0, 0,         8,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {94, "security domain",  K_SET, 8, 0, 0,         8,     QS_OK, RL_LEN, 0, RL_UNT, 'L', F8},
 {96, "enable logging",   K_MIN, 4, 0, 0,         4,     QS_OK, RL_UNT, 0, RL_UNT, 'L', F8},
 {97, "leap second",      K_SET, 8, 0, 0,         8,     QS_OK, RL_LEN, 8, RL_FIX, 'P', QS_BAD_PARAM, C4, C4},
};
const int qp_nclasses = sizeof qp_classes / sizeof qp_classes[0];

const ULONG qp_gap_ids[] = {
    5, 6, 8, 9, 11, 13, 16, 17, 25, 35, 40, 41, 48, 53, 56, 57, 62, 63, 68, 78, 80, 83, 86, 90, 91, 93, 95, 98, 99,
    100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122,
    123, 124, 125, 126, 127, 0x100, 0xFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
const int qp_ngap = sizeof qp_gap_ids / sizeof qp_gap_ids[0];

const qp_cls *qp_find(ULONG id) {
    for (int i = 0; i < qp_nclasses; i++) if (qp_classes[i].id == id) return &qp_classes[i];
    return NULL;
}
int qp_match(NTSTATUS want, NTSTATUS got) { return want == QS_ANY || want == got; }
int qp_is_var(const qp_cls *c) { return c->kind == K_VAR; }

static int in_job(HANDLE h) {
    BOOL b = FALSE;
    if (!IsProcessInJob(h == QP_PSEUDO_SELF ? GetCurrentProcess() : h, NULL, &b)) return -1;
    return b ? 1 : 0;
}
NTSTATUS qp_okst(const qp_cls *c, HANDLE h) {
    if (c->id == 69) { int j = in_job(h); return j == 1 ? QS_OK : QS_NOT_IN_JOB; }
    return c->okst;
}
NTSTATUS qp_exp_len(const qp_cls *c, ULONG len, HANDLE h) {
    NTSTATUS ok = qp_okst(c, h);
    switch (c->kind) {
    case K_SET:  return (len && (len == c->a || len == c->b || len == c->c)) ? ok : QS_LEN_MISMATCH;
    case K_MIN:  return len >= c->a ? ok : QS_LEN_MISMATCH;
    case K_ALL:  return ok;
    case K_MULT: return (len % c->a == 0 && (c->b == 0 || len <= c->b)) ? ok : QS_LEN_MISMATCH;
    case K_47:   return len == 0 ? QS_BUF_TOO_SMALL : (len & 1) ? QS_BAD_PARAM : QS_OK;
    }
    return QS_ANY;
}
long qp_exp_rl(const qp_cls *c, ULONG len, NTSTATUS st, ULONG *mod) {
    long code;
    *mod = c->rlp;
    if (c->id == 47) { if (st == QS_OK || st == QS_BUF_TOO_SMALL) { return 2; } return -1; }
    if (st == QS_LEN_MISMATCH && c->okst != QS_LEN_MISMATCH) code = c->rl_bad;
    else if (c->id == 69 && st != QS_OK) code = RL_UNT;
    else if (st == qp_okst(c, QP_PSEUDO_SELF) || (c->id == 69 && st == QS_OK)) code = c->rl_ok;
    else code = RL_UNT;
    switch (code) {
    case RL_UNT: return -1;
    case RL_LEN: return (long)len;
    case RL_MINL: return (long)(len < c->rlp ? len : c->rlp);
    case RL_FIX: return (long)c->rlp;
    case RL_ANY: return -3;
    }
    return -1;
}

NTSTATUS qp_q(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl, int *written) {
    ULONG r = QP_SENT;
    NTSTATUS s = QIP(h, cls, buf, len, &r);
    if (written) *written = (r != QP_SENT);
    if (rl) *rl = r;
    return s;
}
void qp_cleanup_out(const qp_cls *c, NTSTATUS st, void *buf) {
    if (c->id == 89 && st == QS_OK && *(HANDLE *)buf) CloseHandle(*(HANDLE *)buf);
    if (c->id == 30 && st == QS_OK && *(HANDLE *)buf) CloseHandle(*(HANDLE *)buf);
}

/* ---- handle kinds ----------------------------------------------------------------------------------- */
const char *qp_hname[H_NKINDS] = { "null", "thread-pseudo", "minus3", "closed", "event", "thread", "far", "self-sync",
    "self-zero-rights", "self-limited", "self-query", "current", "child-limited", "child-query", "child-sync", "child-zero-rights" };
HANDLE qp_hmake(int kind, HANDLE child) {
    HANDLE h = NULL;
    switch (kind) {
    case H_NULL: return NULL;
    case H_THR: return QP_PSEUDO_THREAD;
    case H_M3: return (HANDLE)(LONG_PTR)-3;
    case H_CLOSED: h = CreateEventA(NULL, TRUE, FALSE, NULL); if (h) CloseHandle(h); return h;
    case H_EVENT: return CreateEventA(NULL, TRUE, FALSE, NULL);
    case H_THREAD: return OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    case H_FAR: return (HANDLE)(ULONG_PTR)0x1FFFC;
    case H_SYNC: return OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
    case H_ZERO: DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &h, 0, FALSE, 0); return h;
    case H_LIM: return OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    case H_QRY: return OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    case H_CUR: return QP_PSEUDO_SELF;
    case H_CH_LIM: case H_CH_QRY: case H_CH_SYNC: {
        DWORD pid = GetProcessId(child), acc = kind == H_CH_LIM ? PROCESS_QUERY_LIMITED_INFORMATION : kind == H_CH_QRY ? PROCESS_QUERY_INFORMATION : SYNCHRONIZE;
        return OpenProcess(acc, FALSE, pid);
    }
    case H_CH_ZERO: DuplicateHandle(GetCurrentProcess(), child, GetCurrentProcess(), &h, 0, FALSE, 0); return h;
    }
    return NULL;
}
void qp_hrelease(int kind, HANDLE h) {
    if (!h) return;
    switch (kind) {
    case H_NULL: case H_THR: case H_M3: case H_CLOSED: case H_FAR: case H_CUR: return;
    }
    CloseHandle(h);
}
static int is_bad_kind(int k) { return k <= H_FAR; }
NTSTATUS qp_exp_handle(const qp_cls *c, int kind, ULONG len, HANDLE h) {
    if (is_bad_kind(kind) || kind == H_SYNC || kind == H_ZERO || kind == H_CH_SYNC || kind == H_CH_ZERO) {
        NTSTATUS x;
        if (len == c->oklen) x = c->n_ok; else if (len == 3) x = c->n_3; else x = c->n_0;
        if (x == QS_BAD_HANDLE) {
            switch (kind) {
            case H_THR: case H_EVENT: case H_THREAD: return QS_TYPE_MISMATCH;
            case H_SYNC: case H_ZERO: case H_CH_SYNC: case H_CH_ZERO: return QS_ACCESS_DENIED;
            }
            return QS_BAD_HANDLE;
        }
        return x;
    }
    /* a real process handle with the query rights named by the kind */
    NTSTATUS st = qp_exp_len(c, len, h);
    if (c->kind == K_VAR) st = len == c->oklen ? qp_okst(c, h) : QS_ANY;
    int lim_only = (kind == H_LIM || kind == H_CH_LIM);
    int qry = (kind == H_QRY || kind == H_CH_QRY);
    if (c->id == 23 && (kind == H_CH_QRY) && len == c->oklen) return QS_BAD_PARAM;   /* the 0x24 form is refused for another process */
    if (c->need == 'K') return st;
    if (c->need == 'P') return kind == H_CUR ? st : c->n_ok;
    if (c->need == 'Q' && lim_only) return len == c->oklen || st != QS_LEN_MISMATCH ? QS_ACCESS_DENIED : st;
    if (c->need == 'Z' && (lim_only || qry)) return len == c->oklen || st != QS_LEN_MISMATCH ? QS_ACCESS_DENIED : st;
    return st;
}

/* ---- helper processes -------------------------------------------------------------------------------- */
CHILD(ntqip5_idle) {
    HANDLE ev = (HANDLE)(ULONG_PTR)strtoull(argc > 1 ? argv[1] : "0", NULL, 0);
    if (argc > 2 && argv[2][0] == 'f') {
        typedef NTSTATUS (NTAPI *sip_t)(HANDLE, ULONG, PVOID, ULONG);
        sip_t f = (sip_t)t_proc_in("ntdll.dll", "NtSetInformationProcess");
        ULONG v = 0;
        if (f) f(QP_PSEUDO_SELF, 31, &v, 4);
    }
    if (ev) SetEvent(ev);
    Sleep(60000);
    return 0;
}
HANDLE qp_spawn(char mode, PROCESS_INFORMATION *pi) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE ev = CreateEventA(&sa, TRUE, FALSE, NULL);
    char a[96];
    if (!ev) return NULL;
    snprintf(a, sizeof a, "%llu %c", (unsigned long long)(ULONG_PTR)ev, mode);
    HANDLE h = t_spawn_child("ntqip5_idle", a, 0, NULL, NULL, NULL, TRUE, NULL, pi);
    if (!h) { CloseHandle(ev); return NULL; }
    DWORD w = WaitForSingleObject(ev, 15000);
    CloseHandle(ev);
    if (w != WAIT_OBJECT_0) { TerminateProcess(h, 1); CloseHandle(pi->hThread); CloseHandle(h); return NULL; }
    return h;
}
void qp_kill(HANDLE h, PROCESS_INFORMATION *pi) {
    if (!h) return;
    TerminateProcess(h, 0);
    WaitForSingleObject(h, 5000);
    CloseHandle(pi->hThread);
    CloseHandle(h);
}

/* Child that runs one query against -1 and returns the NTSTATUS as its exit code. */
CHILD(ntqip5_qipx) {
    static unsigned char buf[0x1000] __attribute__((aligned(16)));
    ULONG cls = strtoul(argc > 1 ? argv[1] : "0", NULL, 0), len = strtoul(argc > 2 ? argv[2] : "0", NULL, 0);
    ULONG rl = 0;
    NTSTATUS s = QIP(QP_PSEUDO_SELF, cls, buf, len, &rl);
    return (int)s;
}
int qp_run_il(DWORD rid, ULONG cls, ULONG len, NTSTATUS *st) {
    HANDLE tok = NULL, dup = NULL;
    BYTE lab[64];
    SID_IDENTIFIER_AUTHORITY ia = SECURITY_MANDATORY_LABEL_AUTHORITY;
    PSID sid = NULL;
    int ok = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT, &tok)) return 0;
    if (!DuplicateTokenEx(tok, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &dup)) { CloseHandle(tok); return 0; }
    if (AllocateAndInitializeSid(&ia, 1, rid, 0, 0, 0, 0, 0, 0, 0, &sid)) {
        TOKEN_MANDATORY_LABEL l; l.Label.Sid = sid; l.Label.Attributes = SE_GROUP_INTEGRITY;
        (void)lab;
        if (SetTokenInformation(dup, TokenIntegrityLevel, &l, sizeof l + GetLengthSid(sid))) {
            char cmd[700]; STARTUPINFOA si; PROCESS_INFORMATION pi;
            memset(&si, 0, sizeof si); si.cb = sizeof si;
            snprintf(cmd, sizeof cmd, "\"%s\" --child ntqip5_qipx %lu %lu", t_self_path(), cls, len);
            if (CreateProcessAsUserA(dup, NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                DWORD code = 0xDEAD0000;
                if (WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code)) { *st = (NTSTATUS)code; ok = 1; }
                else TerminateProcess(pi.hProcess, 1);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            }
        }
        FreeSid(sid);
    }
    CloseHandle(dup); CloseHandle(tok);
    return ok;
}
int qp_type_is(HANDLE h, const wchar_t *name) {
    typedef NTSTATUS (NTAPI *qo_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    qo_t f = (qo_t)t_proc_in("ntdll.dll", "NtQueryObject");
    BYTE tb[512]; ULONG n = 0;
    if (!f || f(h, 2, tb, sizeof tb, &n) != 0) return 0;
    UNICODE_STRING *u = (UNICODE_STRING *)tb;
    return u->Length / 2 == wcslen(name) && !wcsncmp(u->Buffer, name, u->Length / 2);
}
int qp_integrity(DWORD *rid) {
    HANDLE tok; BYTE b[256]; DWORD n = 0; int ok = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return 0;
    if (GetTokenInformation(tok, TokenIntegrityLevel, b, sizeof b, &n)) {
        TOKEN_MANDATORY_LABEL *l = (TOKEN_MANDATORY_LABEL *)b;
        *rid = *GetSidSubAuthority(l->Label.Sid, *GetSidSubAuthorityCount(l->Label.Sid) - 1); ok = 1;
    }
    CloseHandle(tok);
    return ok;
}
