/* quota_limits: per-process win32k object quotas, strictly inside CHILD processes.
 *
 * Facts the expectations rest on (build 19041, observed behavior):
 *  - GDI: allocating a GDI object fails (NULL, last error untouched) once the per-process GDI handle
 *    count reaches the quota. The quota defaults to 10000 and is read from
 *    HKLM\...\CurrentVersion\Windows\GDIProcessHandleQuota; negative or oversized values are
 *    rejected. Freeing the object releases the count. Stock/public objects are not charged.
 *  - USER: allocating a process- or thread-owned USER object fails with
 *    ERROR_NO_MORE_USER_HANDLES (0x486) once the per-process USER handle count reaches its quota.
 *    GetGuiResources reports these counts (GR_GDIOBJECTS, GR_USEROBJECTS, ...).
 *  - Posted messages: PostMessage fails with ERROR_NOT_ENOUGH_QUOTA (0x718) once the receiving
 *    thread's posted-message count reaches the posted-message limit.
 */
#ifndef QL_H
#define QL_H
#include "t.h"
#include "k.h"

#define QL_MAX_OBJ 12000       /* per-test object budget */
#define QL_SLACK   300         /* how far past the quota a fill loop may run before giving up */

#ifndef ERROR_NO_MORE_USER_HANDLES
#define ERROR_NO_MORE_USER_HANDLES 1158
#endif
#ifndef ERROR_NOT_ENOUGH_QUOTA
#define ERROR_NOT_ENOUGH_QUOTA 1816
#endif
/* ACCEL entries are read by the kernel from the caller's buffer and need DWORD alignment: a plain stack ACCEL (alignment 2)
 * can be refused with ERROR_NOACCESS (998). Declare every entry array with QL_ACCEL_ALIGN. */
#define QL_ACCEL_ALIGN __attribute__((aligned(8)))

/* Windows 10 recycles deleted solid brushes and rectangular regions through a small cache that keeps their handles counted
 * (9 brushes and 7 regions were observed). ql_connect() empties both caches once, by holding QL_DRAIN_N of each for the rest
 * of the process: afterwards every create of those kinds is a fresh handle (exact deltas), and a batch of deletes leaves the
 * count above its starting value by at most QL_CACHE_MAX per cached kind. */
#define QL_DRAIN_N 24
#define QL_CACHE_MAX 16
struct ql_drain { HGDIOBJ obj[2 * QL_DRAIN_N]; int n; };
void ql_drain_take(struct ql_drain *d);     /* hold QL_DRAIN_N brushes and regions: the caches are empty afterwards */
void ql_drain_give(struct ql_drain *d);

#define GR_GDI_CUR  0
#define GR_USER_CUR 1
#define GR_GDI_PEAK 2
#define GR_USER_PEAK 4

/* ---- scenario registry: every heavy test is a scenario run inside a fresh child ---- */
typedef void (*ql_fn)(void);
void ql_register(const char *name, ql_fn fn);
void ql_run_parent(const char *name);          /* spawns  --child ql_scn <name> and checks the exit code */
extern int ql_fails;                            /* failures counted inside the current child */

#define QC(c)        do { int ok_ = !!(c); if (!ok_) ql_fails++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)
#define QCF(c, ...)  do { int ok_ = !!(c); if (!ok_) ql_fails++; t_check(ok_, __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)

#define QL_SCENARIO(NAME, CALL) \
    static void scn_##NAME(void) { CALL; } \
    static void __attribute__((constructor)) rq_##NAME(void) { ql_register(#NAME, scn_##NAME); } \
    TEST(NAME) { ql_run_parent(#NAME); }

/* ---- quota discovery ---- */
struct ql_quota {
    unsigned q;          /* expected effective limit */
    int std;             /* configuration is the stock one (value absent or == 10000): equality checks are hard */
    int in_reg;
    unsigned reg;
};
unsigned ql_reg_dword(const char *name, int *found);
struct ql_quota ql_gdi_quota(void);     /* GDIProcessHandleQuota, clamped exactly as bLoadProcessHandleQuota does */
struct ql_quota ql_user_quota(void);    /* USERProcessHandleQuota (default 10000) */
struct ql_quota ql_post_limit(void);    /* USERPostMessageLimit (default 10000) */

DWORD ql_gur(DWORD flag);               /* GetGuiResources(GetCurrentProcess(), flag) */
int   ql_connect(void);                 /* load user32/gdi32, convert thread, warm the GDI table; 0 = unavailable */
#define QL_NEED() do { if (!ql_connect()) { t_skip("win32k not reachable"); return; } } while (0)

const WCHAR *ql_class(void);
HWND ql_msg_window(void);               /* message-only window of the private class */

/* USER creators used by several files */
struct ql_ukind {
    const char *name;
    int exact;                          /* one success == exactly one USER object */
    int uses_gdi;                       /* creation also consumes GDI handles */
    void *(*mk)(int i);
    BOOL (*del)(void *h);
    void (*setup)(void);
    void (*teardown)(void);
};
extern const struct ql_ukind uk_menu, uk_accel, uk_timer, uk_window, uk_childwin, uk_hook, uk_icon, uk_winevent;

/* ---- fills ---- */
struct ql_fill {
    void *h[QL_MAX_OBJ];
    int n;
    int failed;
    DWORD err;
    DWORD gdi_at_fail, user_at_fail;
};
extern struct ql_fill ql_f;
#endif
