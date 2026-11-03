/* Minimal conformance-test harness.
 *
 * The DLL under test is never linked. Every call goes through
 *   LoadLibrary -> GetProcAddress -> invoke
 * U(Name) gives a function pointer typed from the SDK header declaration of Name
 * (via __typeof__), resolved by searching the modules listed in modules.h in order.
 * The harness itself only uses trivial kernel32 services (printf, exceptions, temp dir).
 */
#ifndef T_H
#define T_H
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>

/* ---- module lookup ---------------------------------------------------- */
HMODULE t_module(const char *dll);                 /* LoadLibrary (cached) */
FARPROC t_proc(const char *name);                  /* search modules.h list in order */
FARPROC t_proc_in(const char *dll, const char *name); /* one specific module (forwarders resolve) */
FARPROC t_proc_ord(const char *dll, unsigned ord);

/* U(CreateFileW)(...)       pointer typed from the SDK declaration, any listed module
 * UIN("x.dll", Name)(...)   same, pinned to one module (use this to test forwarders) */
#define U(name)         ((__typeof__(&name))t_proc(#name))
#define UIN(dll, name)  ((__typeof__(&name))t_proc_in(dll, #name))
/* Functions missing from the SDK headers: declare a typedef, then UN(name). */
#define UDECL(ret, name, args) typedef ret (WINAPI *name##_pfn) args
#define UN(name)        ((name##_pfn)t_proc(#name))

/* ---- tests ------------------------------------------------------------- */
typedef void (*t_fn)(void);
void t_register(const char *name, t_fn fn);
#define TEST(n) \
    static void test_##n(void); \
    static void __attribute__((constructor)) reg_##n(void) { t_register(#n, test_##n); } \
    static void test_##n(void)

/* Child processes: the exe re-launches itself as  exe --child <name> [args]  */
typedef int (*t_child_fn)(int argc, char **argv);
void t_register_child(const char *name, t_child_fn fn);
#define CHILD(n) \
    static int child_##n(int argc, char **argv); \
    static void __attribute__((constructor)) regc_##n(void) { t_register_child(#n, child_##n); } \
    static int child_##n(int argc, char **argv)
HANDLE t_spawn_child(const char *name, const char *extra_args, DWORD creation_flags, HANDLE hin, HANDLE hout, HANDLE herr, BOOL inherit, const char *env_or_null, PROCESS_INFORMATION *pi_out);
DWORD  t_run_child(const char *name, const char *extra_args, DWORD timeout_ms); /* exit code; 0xDEAD0000 spawn failed, 0xDEAD0001 timeout (killed) */
const char *t_self_path(void);

/* Scratch directory unique to this process, removed at exit (trailing backslash). */
const char *t_tmpdir(void);
void t_tmpfile(char *out, size_t cap, const char *leaf);

/* ---- assertions (non-fatal, counted) ----------------------------------- */
void t_check(int ok, const char *file, int line, const char *expr, const char *fmt, ...);
#define CHECK(c)          t_check(!!(c), __FILE__, __LINE__, #c, NULL)
#define CHECKF(c, ...)    t_check(!!(c), __FILE__, __LINE__, #c, __VA_ARGS__)
/* Operands are evaluated exactly once: tests pass calls with side effects (SetFocus, CheckMenuItem, ShowCursor). */
#define EXPECT_EQ(a, b)   ({ ULONG_PTR a__ = (ULONG_PTR)(a), b__ = (ULONG_PTR)(b); \
    t_check(a__ == b__, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", (unsigned long long)a__, (unsigned long long)b__); })
#define EXPECT_NE(a, b)   ({ ULONG_PTR a__ = (ULONG_PTR)(a), b__ = (ULONG_PTR)(b); \
    t_check(a__ != b__, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", (unsigned long long)a__); })
#define EXPECT_STR(a, b)  ({ const char *a__ = (a), *b__ = (b); \
    t_check(!strcmp(a__, b__), __FILE__, __LINE__, #a " == " #b, "got \"%s\" want \"%s\"", a__, b__); })
#define EXPECT_WSTR(a, b) ({ const WCHAR *a__ = (a), *b__ = (b); \
    t_check(!lstrcmpW(a__, b__), __FILE__, __LINE__, #a " == " #b, "wide string mismatch"); })
/* Check GetLastError right after a call that must fail. */
#define EXPECT_ERR(err)   do { DWORD e_ = GetLastError(); \
    t_check(e_ == (DWORD)(err), __FILE__, __LINE__, #err, "GetLastError got %lu want %lu", e_, (DWORD)(err)); } while (0)
#define SET_ERR(e)        SetLastError(e)
/* A call that must FAIL with a specific error: r is the call's failure sentinel test. */
#define EXPECT_FAIL(is_failure, err) do { t_check(!!(is_failure), __FILE__, __LINE__, #is_failure, "call unexpectedly succeeded"); EXPECT_ERR(err); } while (0)

void t_scrub(char *s);               /* replace machine-varying hex values in a line by <hex> (W32K_RAW=1 disables) */
void t_info(const char *fmt, ...);   /* machine-varying diagnostics (handles, ids, pids, times): printed only when W32K_INFO is set, so a run's output stays identical across runs */
int t_info_on(void);
void t_note(const char *fmt, ...);   /* observed/version-dependent behaviour: logged, never fails */
void t_skip(const char *fmt, ...);   /* skip the rest of the current test */
#define REQUIRE(ptr) do { if (!(ptr)) { t_skip("missing export: " #ptr); return; } } while (0)
int  t_is_wine(void);                /* true under an emulator exposing wine_get_version */
#endif
