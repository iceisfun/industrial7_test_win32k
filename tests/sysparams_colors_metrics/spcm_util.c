#include "spcm.h"

struct spcm_obs g_obs;
static const wchar_t *k_cls_name(void) {
    static wchar_t n[64];
    if (!n[0]) wsprintfW(n, L"win32kt_spcm_obs_%lu", GetCurrentProcessId());
    return n;
}
static LRESULT CALLBACK obs_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SYSCOLORCHANGE) InterlockedIncrement(&g_obs.syscolor);
    else if (m == WM_SETTINGCHANGE) { InterlockedIncrement(&g_obs.setting); g_obs.last_setting_wparam = w; g_obs.last_setting_lparam = l; }
    return U(DefWindowProcW)(h, m, w, l);
}
int spcm_obs_create(void) {
    if (!U(RegisterClassExW) || !U(CreateWindowExW) || !U(DefWindowProcW)) return 0;
    if (g_obs.hwnd) return 1;
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = obs_wp; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = k_cls_name();
    U(RegisterClassExW)(&wc);
    /* top-level, never shown: WM_SETTINGCHANGE / WM_SYSCOLORCHANGE broadcasts are delivered to hidden top-level windows too */
    g_obs.hwnd = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, k_cls_name(), L"win32kt-spcm", WS_POPUP,
                                    -32000, -32000, 1, 1, NULL, NULL, wc.hInstance, NULL);
    return g_obs.hwnd != NULL;
}
void spcm_obs_destroy(void) {
    if (g_obs.hwnd) { U(DestroyWindow)(g_obs.hwnd); g_obs.hwnd = NULL; }
    if (U(UnregisterClassW)) U(UnregisterClassW)(k_cls_name(), GetModuleHandleW(NULL));
}
void spcm_obs_reset(void) { g_obs.syscolor = 0; g_obs.setting = 0; g_obs.last_setting_wparam = 0; g_obs.last_setting_lparam = 0; }
