/* x_ntqip_qip_image_session_peb: shared helpers.
 * NtQueryInformationProcess is resolved from ntdll.dll by name; classes are addressed by their numeric value
 * (no community enum names). Every query goes through ip_q(), which pre-fills the ReturnLength with a sentinel so a
 * test can tell "written" from "left alone". */
#ifndef IP_H
#define IP_H
#include "t.h"
#include "k.h"

#define ST_OK            0x00000000L
#define ST_MISALIGN      ((LONG)0x80000002)
#define ST_INFO_LEN      ((LONG)0xC0000004)
#define ST_INVALID_HANDLE ((LONG)0xC0000008)
#define ST_INVALID_PARAM ((LONG)0xC000000D)
#define ST_ACCESS_DENIED ((LONG)0xC0000022)
#define ST_TYPE_MISMATCH ((LONG)0xC0000024)
#define ST_BUF_TOO_SMALL ((LONG)0xC0000023)
#define ST_UNSUCCESSFUL  ((LONG)0xC0000001)
#define ST_NOT_FOUND     ((LONG)0xC0000225)
#define ST_TERMINATING   ((LONG)0xC000010A)

#define RL_SENT   0xCCCCCCCCu
#define FILLB     0xAA
#define PROC_LIM  0x1000u    /* PROCESS_QUERY_LIMITED_INFORMATION */
#define PROC_QRY  0x0400u    /* PROCESS_QUERY_INFORMATION */

/* class numbers (decimal ids of the information class values) */
enum { C_DEVMAP = 23, C_SESSION = 24, C_WOW64 = 26, C_NTPATH = 27, C_LUIDMAPS = 28, C_WOW86FLAG = 19, C_AFFINITY = 21,
       C_IMAGEINFO = 37, C_DOSPATH = 43, C_FILEMAP = 44, C_GROUPS = 47, C_CONHOST = 49, C_WININFO = 50, C_CMDLINE = 60,
       C_DEFCPUSETS = 66, C_ALLOWCPUSETS = 67, C_SECTION = 89, C_LEAP = 97 };

int   ip_init(void);                                  /* resolve NtQueryInformationProcess; 0 if missing */
LONG  ip_q(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl);   /* sets *rl = sentinel first (rl may be NULL: passed as NULL) */
unsigned char *ip_buf(void);                          /* 64 KB, page aligned, zero at start of each call? no: caller fills */
LONG  ip_q_nullrl(HANDLE h, ULONG cls, void *buf, ULONG len);
void  ip_fill(void *p, size_t n);
int   ip_all_filler(const void *p, size_t n);        /* every byte still the fill pattern */

struct kid { HANDLE h; HANDLE th; DWORD pid; };
/* Start a copy of this exe as "--child park" (waits for this process to exit or for 120 s). */
int   kid_start(struct kid *k, const char *extra, DWORD cflags, const char *title_or_null, DWORD sflags, const char *exe_or_null);
int   kid_start_w(struct kid *k, const WCHAR *title_w, DWORD sflags);
void  kid_stop(struct kid *k);
HANDLE kid_open(const struct kid *k, DWORD rights);   /* OpenProcess; NULL on failure */
void  kid_wait_ready(const struct kid *k);            /* the child has finished initialisation (named event) */

/* generic class table */
enum { R_NONE, R_LIM, R_QRY };
struct cls {
    ULONG id; const char *nm;
    ULONG ok;          /* a length that is accepted */
    int   right;       /* R_* */
    ULONG bad[8];      /* lengths rejected with STATUS_INFO_LENGTH_MISMATCH, 0xFFFF terminated (0 is a legal entry) */
    int   fixed_in;    /* needs an input structure (no generic positive call) */
};
extern const struct cls IPC[];
extern const int IPC_N;
const struct cls *ip_cls(ULONG id);
HANDLE ip_dup_self(void);                              /* PROCESS_ALL_ACCESS handle of this process */
#endif
/* helpers implemented in ip_paths.c */
typedef struct { USHORT Length, MaximumLength; ULONG pad; WCHAR *Buffer; } IPUS;   /* UNICODE_STRING as laid out on x64 */
/* Check the UNICODE_STRING + inline characters layout the string classes produce; returns the string (inside out). */
const WCHAR *ip_check_us_layout(const unsigned char *out, ULONG rl, ULONG cls, USHORT *len_bytes);
int  ip_run_child(const char *name, const char *args, DWORD cflags, DWORD sflags, const char *title, char *out, size_t cap, DWORD *exit_code);
BOOL ip_image_path_of(HANDLE h, DWORD flags, WCHAR *out, DWORD cap);   /* QueryFullProcessImageNameW wrapper */
