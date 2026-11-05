/* x_ntqip_qip_mitigation_policy_job_energy: shared helpers.
 *
 * Every call goes through ntdll!NtQueryInformationProcess resolved by name. Statuses are handled as ULONG so the
 * constants below compare directly. A call that fails is expected to leave the output buffer and (for the classes
 * covered here, except 64) the ReturnLength variable untouched; the helpers detect that with two different fill
 * patterns, so a coincidental data byte never hides a write.
 */
#ifndef QH_H
#define QH_H
#include "t.h"
#include "k.h"

#define ST_OK                0x00000000UL
#define ST_MISALIGN          0x80000002UL
#define ST_BUF_OVERFLOW      0x80000005UL
#define ST_LEN               0xC0000004UL
#define ST_INV_HANDLE        0xC0000008UL
#define ST_INV_PARAM         0xC000000DUL
#define ST_DENIED            0xC0000022UL
#define ST_TYPE_MISMATCH     0xC0000024UL
#define ST_REV_MISMATCH      0xC0000059UL
#define ST_DEV_NOT_CONNECTED 0xC000009DUL
#define ST_NOT_SUPPORTED     0xC00000BBUL
#define ST_NOT_IN_JOB        0xC00001AEUL

#define RL_UNSET   0xA5A5A5A5UL
#define FILL_A     0xCC
#define FILL_B     0x33
#define QH_MAXLEN  0x9000
#define SELF       ((HANDLE)(LONG_PTR)-1)
#define THREAD_PSEUDO ((HANDLE)(LONG_PTR)-2)

/* class numbers (neutral labels: 52 mitigation policy, 69 job memory, 70 in-private, 72 challenge response,
 * 73 child process policy, 75 subsystem, 76 energy values, 77 throttle state, 79 win32k filter, 81 wake information,
 * 82 energy tracking, 84 live dump, 85 telemetry coverage, 64 telemetry id, 65 commit release, 88 uptime statistics) */

/* one raw call; if rl != NULL it is preset to RL_UNSET and passed, else NULL is passed */
ULONG qh(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl);

struct qh_res {
    ULONG st;
    ULONG rl;                 /* RL_UNSET when the kernel did not write it */
    ULONG touched_end;        /* 1 + index of the last byte (at or after skip) written by the kernel, 0 if none */
    ULONG touched_cnt;        /* bytes in [skip,len) the kernel wrote */
    unsigned char out[64];    /* first bytes of the first run's buffer */
};
/* Runs the query twice with different fills (the first initlen bytes of both buffers are set to `in`).
 * Bytes before `skip` are ignored for the touched computation. len <= QH_MAXLEN. */
struct qh_res qh_probe(HANDLE h, ULONG cls, ULONG len, const void *in, ULONG inlen, ULONG skip);

/* expects status `want`, ReturnLength untouched (unless rl_may_be_set) and no output byte at or after skip written */
int qh_err(const char *tag, HANDLE h, ULONG cls, ULONG len, ULONG want, const void *in, ULONG inlen, ULONG skip);

/* handles */
HANDLE qh_open(DWORD rights, DWORD pid);          /* NULL on failure */
HANDLE qh_closed_handle(void);                    /* a process handle value that was closed again */

/* child that reports readiness and sleeps */
struct qh_child { HANDLE h; DWORD pid; };
enum { QC_PLAIN = 0, QC_CHILD_RESTRICTED = 1, QC_WIN32K_DISABLE = 2 };
int  qh_child_start(struct qh_child *c, int variant, HANDLE job);   /* 1 ok */
void qh_child_stop(struct qh_child *c);

/* table of the classes in scope and the exact length each answers to (0 when any length is accepted) */
struct qh_cls { ULONG cls; ULONG len; const char *name; };
extern const struct qh_cls qh_classes[];
extern const int qh_nclasses;
int qh_in_job(HANDLE h);   /* IsProcessInJob(h, NULL): 1/0/-1 */
#endif
