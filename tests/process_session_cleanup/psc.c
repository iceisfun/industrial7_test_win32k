/* process_session_cleanup shared state (one definition per exe). */
#include "psc.h"

unsigned psc_ctr_next(void) { static volatile LONG c; return (unsigned)InterlockedIncrement(&c); }

struct psc_cx *psc_x;
static struct { const char *n; psc_scen_fn f; } psc_scens[256];
static int psc_nscens;
void psc_scen_reg(const char *n, psc_scen_fn f) { if (psc_nscens < 256) { psc_scens[psc_nscens].n = n; psc_scens[psc_nscens].f = f; psc_nscens++; } }

void psc_cfail(const char *file, int line, const char *expr) {
    if (InterlockedIncrement(&psc_x->s->fails) == 1)
        snprintf(psc_x->s->msg, sizeof psc_x->s->msg, "%s:%d: %s (err %lu)", file, line, expr, GetLastError());
}

static LONG CALLBACK psc_child_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if ((c & 0xF0000000) != 0xC0000000) return EXCEPTION_CONTINUE_SEARCH;
    if (psc_x && InterlockedIncrement(&psc_x->s->fails) == 1)
        snprintf(psc_x->s->msg, sizeof psc_x->s->msg, "exception 0x%08lx at %p", c, ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;
}

CHILD(psc) {
    static struct psc_cx x;
    if (argc < 4) return 0xC0DE0010;
    x.ppid = (DWORD)strtoul(argv[1], NULL, 10);
    x.ctr = (unsigned)strtoul(argv[2], NULL, 10);
    x.extra = argc > 4 ? argv[4] : "";
    char n_m[96], n_c[96], n_p[96];
    psc_names(x.ppid, x.ctr, n_m, n_c, n_p);
    HANDLE hm = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, n_m);
    if (!hm) return 0xC0DE0011;
    x.s = (struct psc_shm *)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct psc_shm));
    x.evC = OpenEventA(EVENT_ALL_ACCESS, FALSE, n_c);
    x.evP = OpenEventA(EVENT_ALL_ACCESS, FALSE, n_p);
    if (!x.s || !x.evC || !x.evP) return 0xC0DE0012;
    psc_x = &x;
    AddVectoredExceptionHandler(1, psc_child_veh);
    for (int i = 0; i < psc_nscens; i++) {
        if (strcmp(psc_scens[i].n, argv[3])) continue;
        psc_scens[i].f(&x);
        return x.s->fails > 200 ? 200 : (int)x.s->fails;
    }
    snprintf(x.s->msg, sizeof x.s->msg, "unknown scenario %s", argv[3]);
    return 0xC0DE0013;
}


volatile LONG psc_msg_count;
HANDLE psc_kill_h;
LRESULT CALLBACK psc_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case PSC_M_EXITTHREAD: ExitThread(0); return 0;
    case PSC_M_KILLSELF: TerminateProcess(GetCurrentProcess(), 77); return 0;
    case PSC_M_TID: return (LRESULT)GetCurrentThreadId();
    case PSC_M_COUNT: InterlockedIncrement(&psc_msg_count); return 1;
    case PSC_M_RELAY: return U(SendMessageA)((HWND)l, PSC_M_KILLPEER, 0, 0);
    case PSC_M_KILLPEER: if (psc_kill_h) TerminateProcess(psc_kill_h, 99); return 0x77;
    }
    return DefWindowProcA(h, m, w, l);
}
const char *psc_class(void) {
    static char name[64];
    static int done;
    if (!done) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        snprintf(name, sizeof name, "win32kt_psc_%lu", (unsigned long)GetCurrentProcessId());
        wc.lpfnWndProc = psc_wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = name; wc.cbWndExtra = 16;
        if (U(RegisterClassA)(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) done = 1;
    }
    return done ? name : NULL;
}
