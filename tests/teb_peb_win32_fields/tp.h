/* Local helpers for the teb_peb_win32_fields area: typed, fault-safe readers for the current
 * thread's TEB / the process PEB, address-class predicates and a few win32u syscall prototypes.
 * All offsets come from common/k.h (TEB_* / PEB_* are the 19041.2965 ntdll PDB layouts). */
#ifndef TP_H
#define TP_H
#include "t.h"
#include "k.h"

#define TP_USER_MAX   0x00007FFFFFFE0000ULL   /* highest user-mode VA is 0x7FFFFFFEFFFF */
#define TP_KERNEL_MIN 0xFFFF800000000000ULL

static inline int tp_is_user_va(unsigned long long p) { return p >= 0x10000ULL && p < TP_USER_MAX; }
static inline int tp_is_kernel_va(unsigned long long p) { return p >= TP_KERNEL_MIN; }

/* TEB readers (current thread). A failed read yields 0 and sets *ok to 0 when supplied. */
static inline unsigned long long tp_teb64(unsigned off) {
    unsigned long long v = 0; k_try_read((char *)k_teb() + off, &v, sizeof v); return v; }
static inline unsigned long tp_teb32(unsigned off) {
    unsigned long v = 0; k_try_read_u32((char *)k_teb() + off, &v); return v; }
static inline unsigned short tp_teb16(unsigned off) {
    unsigned short v = 0; k_try_read_u16((char *)k_teb() + off, &v); return v; }
static inline unsigned char tp_teb8(unsigned off) {
    unsigned char v = 0; k_try_read_u8((char *)k_teb() + off, &v); return v; }
static inline void tp_teb_write32(unsigned off, unsigned long v) {
    /* the TEB is ordinary writable user memory; used only for sentinel values in fields the
     * test owns (LastStatusValue) and always restored by the caller */
    *(volatile unsigned long *)((char *)k_teb() + off) = v; }

static inline unsigned long long tp_peb64(unsigned off) {
    unsigned long long v = 0; k_try_read((char *)k_peb() + off, &v, sizeof v); return v; }
static inline unsigned long tp_peb32(unsigned off) {
    unsigned long v = 0; k_try_read_u32((char *)k_peb() + off, &v); return v; }
static inline unsigned short tp_peb16(unsigned off) {
    unsigned short v = 0; k_try_read_u16((char *)k_peb() + off, &v); return v; }
static inline unsigned char tp_peb8(unsigned off) {
    unsigned char v = 0; k_try_read_u8((char *)k_peb() + off, &v); return v; }

/* True when [p, p+n) lies inside the image of module m. */
static inline int tp_in_module(HMODULE m, unsigned long long p, unsigned long long n) {
    if (!m) return 0;
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt;
    if (!k_try_read(m, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if (!k_try_read((char *)m + dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return 0;
    unsigned long long base = (unsigned long long)(ULONG_PTR)m, size = nt.OptionalHeader.SizeOfImage;
    return p >= base && p + n <= base + size;
}

/* NtUserGetThreadState(0xE): kernel returns 1 for any thread (win32kfull!NtUserGetThreadState,
 * index 0xE), and as a win32k service it converts the calling thread into a GUI thread first. */
UDECL(ULONG_PTR, NtUserGetThreadState, (int));
UDECL(LONG, NtGdiFlush, (void));
UDECL(LONG, NtGdiInit, (void));
UDECL(UINT, NtUserRegisterWindowMessage, (UNICODE_STRING *));
UDECL(LONG, NtUserGetDpiForMonitor, (HANDLE, int, UINT *, UINT *));

/* Brings user32 + win32k up on the calling thread through the ordinary path and returns the
 * desktop window (0 if user32 is unavailable). */
static inline HWND tp_gui(void) {
    HWND (WINAPI *f)(void) = (HWND (WINAPI *)(void))t_proc("GetDesktopWindow");
    return f ? f() : NULL;
}

/* Runs fn(arg) on a fresh thread, waits at most ms, returns its exit code (0xDEAD0001 on timeout). */
typedef DWORD (*tp_thread_fn)(void *);
struct tp_thr { tp_thread_fn fn; void *arg; };
static DWORD WINAPI tp_thr_main(void *p) { struct tp_thr *t = (struct tp_thr *)p; return t->fn(t->arg); }
static inline DWORD tp_run_thread(tp_thread_fn fn, void *arg, DWORD ms) {
    struct tp_thr *t = (struct tp_thr *)HeapAlloc(GetProcessHeap(), 0, sizeof *t);   /* outlives us on timeout */
    if (!t) return 0xDEAD0000;
    t->fn = fn; t->arg = arg;
    HANDLE h = CreateThread(NULL, 0, tp_thr_main, t, 0, NULL);
    if (!h) { HeapFree(GetProcessHeap(), 0, t); return 0xDEAD0000; }
    DWORD code = 0xDEAD0001;
    if (WaitForSingleObject(h, ms) == WAIT_OBJECT_0) { GetExitCodeThread(h, &code); HeapFree(GetProcessHeap(), 0, t); }
    CloseHandle(h);
    return code;
}
#endif
