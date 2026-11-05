/* x_ntqip_qip_basic_quota_handles: shared helpers (see qb.c). Not a test file. */
#ifndef QB_H
#define QB_H
#include "t.h"
#include "k.h"

#define QB_SENT 0x5a5a5a5aUL
#define QB_FILL 0xcc
#define R_LIM  0x1000UL
#define R_QRY  0x0400UL
#define R_VMR  0x0010UL
#define R_DUP  0x0040UL
#define R_SYNC 0x100000UL

#define ST_OK        0L
#define ST_LENMIS    ((LONG)0xC0000004)
#define ST_UNSUCC    ((LONG)0xC0000001)
#define ST_BADHANDLE ((LONG)0xC0000008)
#define ST_ACCESS    ((LONG)0xC0000022)
#define ST_TYPEMIS   ((LONG)0xC0000024)
#define ST_SMALL     ((LONG)0xC0000023)
#define ST_NOMORE    ((LONG)0x8000001A)
#define ST_TERMING   ((LONG)0xC000010A)
#define ST_BADCLASS  ((LONG)0xC0000003)

#define PSEUDO_SELF ((HANDLE)(LONG_PTR)-1)
#define PSEUDO_THREAD ((HANDLE)(LONG_PTR)-2)

/* arena: 0x3000 bytes, 64-byte aligned, refilled with QB_FILL before every qb_call */
unsigned char *qb_arena(void);
int  qb_have(void);
/* one NtQueryInformationProcess call into arena+off. nobuf: NULL buffer, norl: NULL ReturnLength.
 * *rl_out receives the ReturnLength variable (pre-set to QB_SENT). */
LONG qb_call(HANDLE h, ULONG cls, ULONG off, ULONG len, int nobuf, int norl, ULONG *rl_out);
LONG qb_q(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl);
LONG qb_set(HANDLE h, ULONG cls, void *buf, ULONG len);
/* are arena bytes [from,to) all still the fill byte */
int  qb_untouched(ULONG from, ULONG to);
ULONGLONG qb_u64(ULONG off);
ULONG qb_u32(ULONG off);

HANDLE qb_child(void);            /* long-lived idle child (full access handle) */
DWORD  qb_child_pid(void);
HANDLE qb_fresh_child(DWORD *pid);/* a new idle child, caller terminates and closes it */
HANDLE qb_dead(void);             /* an exited child (full access handle, exit code 7) */
HANDLE qb_child_open(DWORD mask); /* OpenProcess(mask) on qb_child, NULL on failure */
HANDLE qb_closed_handle(void);    /* a numerically valid-looking handle that was closed */
int    qb_is_admin_mode(void);

/* a static big buffer for the variable length classes */
unsigned char *qb_big(void);
#define QB_BIG_SIZE 0x20000
#endif
