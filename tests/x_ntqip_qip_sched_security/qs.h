/* x_ntqip_qip_sched_security shared helpers (included by every file of this area).
 *
 * Area scope: NtQueryInformationProcess for the scheduling, priority, security and per-process flag
 * information classes 10, 12, 18, 22, 29, 33, 34, 36, 39, 45, 46, 59, 61, 74, 87, 92, 94, 96 (plus the
 * front-door protocol shared by all classes: alignment, length, handle and access-right order).
 * Class numbers are used as plain numbers; the descriptions in comments are observed behaviour. */
#ifndef QS_H
#define QS_H
#include "t.h"
#include "k.h"

#define QS_OK        ((LONG)0)
#define QS_NOTIMPL   ((LONG)0xC0000002)
#define QS_BADCLASS  ((LONG)0xC0000003)
#define QS_LENMISM   ((LONG)0xC0000004)
#define QS_BADHANDLE ((LONG)0xC0000008)
#define QS_DENIED    ((LONG)0xC0000022)
#define QS_TYPEMISM  ((LONG)0xC0000024)
#define QS_MISALIGN  ((LONG)0x80000002)
#define QS_SENT      0xAAAAAAAAu
#define QS_FILL      0xCC

#define R_LIM 0x1000
#define R_QRY 0x400
#define R_VMR 0x10
#define R_VMW 0x20
#define R_VMO 0x08
#define R_SYNC 0x100000

enum { M_EXACT, M_MIN1, M_MIN4 };
enum { K_LIM, K_QRY, K_VMW };
struct qs_cls {
    ULONG id; int mode; ULONG len; int right; ULONG align;
    int rl_written;      /* success writes ReturnLength: 0 = left untouched */
    ULONG rl_val;        /* value written when rl_written */
    ULONG nwritten;      /* bytes of the buffer that a success changes at most (guard check) */
};
const struct qs_cls *qs_find(ULONG id);

/* Raw call. buf offset `off` bytes into a 64-byte aligned, 0xCC-filled arena (NULL buffer when nobuf).
 * *rl_out receives ReturnLength (QS_SENT when untouched). */
LONG qs_call(HANDLE h, ULONG cls, ULONG off, ULONG len, int nobuf, int norl, ULONG *rl_out);
unsigned char *qs_arena(void);
LONG qs_q(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl);   /* plain call, caller's buffer */
int  qs_have(void);                                                 /* export resolved */

/* Child process: one lazily started long-lived child (exits when the test exe does). */
HANDLE qs_child(void);            /* full-access handle from CreateProcess */
DWORD  qs_child_pid(void);
HANDLE qs_dead(void);             /* full-access handle of a child that has already exited (code 7) */
HANDLE qs_dup(HANDLE full, DWORD mask);   /* exact-mask duplicate of a full-access handle (caller closes) */
HANDLE qs_self_open(DWORD mask);  /* OpenProcess(self, mask) */
int    qs_mask_ok(int right, DWORD mask);
int    qs_len_ok(const struct qs_cls *c, ULONG len);
ULONG  qs_good_len(const struct qs_cls *c);
DWORD  qs_find_pid(const char *exe);
int    qs_is_admin_mode(void);
#endif
