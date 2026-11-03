#include "d.h"
#include <stdlib.h>

dk_fn1 dk_nt(const char *name) {
    /* Wine only implements a subset; unimplemented win32u exports abort the process, so hide them there */
    if (t_is_wine()) {
        int ok = 0;
        for (int i = 0; i < dk_wine_ok_cnt; i++) if (!strcmp(dk_wine_ok_tab[i], name)) { ok = 1; break; }
        if (!ok) return NULL;
    }
    return (dk_fn1)t_proc_in("win32u.dll", name);
}

LONG dk_close(ULONG h) {
    dk_fn1 f = dk_nt("NtGdiDdDDICloseAdapter");
    dk_closeargs c = { h };
    return f ? f(&c) : 0x7FFFFFFF;
}
void dk_close_ok(ULONG h) { LONG st = dk_close(h); CHECKF(st == ST_SUCCESS, "CloseAdapter(0x%lx) = 0x%lx", (unsigned long)h, (unsigned long)st); }

int dk_open_screen(dk_openhdc *o, HDC *hdc_out) {
    dk_fn1 f = dk_nt("NtGdiDdDDIOpenAdapterFromHdc");
    if (!U(GetDC) || !f) return 0;
    HDC dc = U(GetDC)(NULL);
    if (!dc) return 0;
    memset(o, 0, sizeof *o);
    o->hDc = dc;
    LONG st = f(o);
    if (!NTOK(st) || !o->hAdapter) {
        t_note("OpenAdapterFromHdc(screen DC) = 0x%lx, no WDDM adapter in this session", (unsigned long)st);
        U(ReleaseDC)(NULL, dc);
        return 0;
    }
    if (hdc_out) *hdc_out = dc; else U(ReleaseDC)(NULL, dc);
    return 1;
}

int dk_enum(dk_adapterinfo **list) {
    *list = NULL;
    dk_fn1 f = dk_nt("NtGdiDdDDIEnumAdapters2");
    if (!f) return -1;
    dk_enum2 e = { 0, NULL };
    if (!NTOK(f(&e))) return -1;
    if (e.NumAdapters == 0) return 0;
    ULONG n = e.NumAdapters;
    dk_adapterinfo *a = calloc(n + 1, sizeof *a);
    e.NumAdapters = n; e.pAdapters = a;
    if (!NTOK(f(&e))) { free(a); return -1; }
    *list = a;
    return (int)e.NumAdapters;
}

unsigned dk_gui_count(unsigned which) {
    return U(GetGuiResources) ? U(GetGuiResources)(GetCurrentProcess(), which) : 0;
}

void dk_enum_close(dk_adapterinfo *a, int n) {
    for (int i = 0; i < n; i++) if (a[i].hAdapter) dk_close(a[i].hAdapter);
    free(a);
}
int dk_luid_eq(LUID a, LUID b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }
int dk_luid_zero(LUID a) { return a.LowPart == 0 && a.HighPart == 0; }
const char *dk_stn(LONG st) { return k_status_name((K_NTSTATUS)st); }
