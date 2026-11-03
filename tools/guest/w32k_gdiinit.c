/* GDI initialisation-order fixture.
 * Imports kernel32 and user32 only: gdi32 is never named in the import table, so it is brought in only
 * through user32's own dependency. The program samples the process's GDI/USER client state at fixed
 * boundaries (entry, after creating GDI objects on the initial thread, inside a worker thread before and
 * after it creates its own objects, after the worker is joined) and prints one STAGE line per sample.
 * Only small numbers and 0/1 flags are printed (never addresses or handles); the pointer relations are
 * computed here and printed as flags. Exit code is always 0; the verdict is made by the area that runs it. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>

#define PEB_KCB 0x058
#define PEB_GDITAB 0x0F8
#define PEB_DCATTR 0x108
#define TEB_W32T 0x078
#define TEB_CPID 0x7F0
#define TEB_CTID 0x7F4

static void *rdp(const unsigned char *base, unsigned off) { return *(void *const *)(base + off); }
static void *g_tab0, *g_kcb0;

static void stage(const char *name, int fresh_thread) {
    const unsigned char *teb = (const unsigned char *)__readgsqword(0x30);
    const unsigned char *peb = (const unsigned char *)__readgsqword(0x60);
    void *tab = rdp(peb, PEB_GDITAB), *kcb = rdp(peb, PEB_KCB), *w32t = rdp(teb, TEB_W32T);
    unsigned dca = *(const unsigned *)(peb + PEB_DCATTR);
    unsigned cpid = *(const unsigned *)(teb + TEB_CPID), ctid = *(const unsigned *)(teb + TEB_CTID);
    HMODULE g32 = GetModuleHandleA("gdi32.dll"), g32f = GetModuleHandleA("gdi32full.dll");
    DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), usr = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    if (!g_tab0 && tab) g_tab0 = tab;
    printf("STAGE %s tab=%d same_tab=%d kcb=%d same_kcb=%d w32t=%d dca=%u cpid_self=%d ctid_self=%d gdi32=%d gdi32full=%d gdi=%lu user=%lu\n",
           name, tab != NULL, tab != NULL && tab == g_tab0, kcb != NULL, kcb != NULL && kcb == g_kcb0, w32t != NULL, dca,
           cpid == GetCurrentProcessId(), ctid == GetCurrentThreadId(), g32 != NULL, g32f != NULL, gdi, usr);
    (void)fresh_thread;
}

static HDC g_wdc; static HBITMAP g_wbm;
static DWORD WINAPI worker(void *a) {
    stage("worker_start", 1);
    g_wdc = GetDC(NULL);
    g_wbm = LoadBitmapA(NULL, MAKEINTRESOURCEA(32754)); /* OBM_CLOSE */
    stage("worker_objects", 1);
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    {
        const unsigned char *peb = (const unsigned char *)__readgsqword(0x60);
        g_tab0 = rdp(peb, PEB_GDITAB); g_kcb0 = rdp(peb, PEB_KCB);
    }
    stage("entry", 0);
    HDC dc = GetDC(NULL);
    HBITMAP bm = LoadBitmapA(NULL, MAKEINTRESOURCEA(32754));
    printf("OBJ dc=%d bm=%d\n", dc != NULL, bm != NULL);
    stage("main_objects", 0);
    HANDLE th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    DWORD w = th ? WaitForSingleObject(th, 20000) : WAIT_FAILED;
    printf("JOIN %d\n", w == WAIT_OBJECT_0);
    stage("after_join", 0);
    printf("WORKER dc=%d bm=%d\n", g_wdc != NULL, g_wbm != NULL);
    if (g_wdc) ReleaseDC(NULL, g_wdc);
    stage("after_cleanup", 0);
    if (dc) ReleaseDC(NULL, dc);
    printf("DONE\n");
    return 0;
}
