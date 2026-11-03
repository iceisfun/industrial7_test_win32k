#define MSC_NO_MACROS
#include "msc.h"

static struct msc_api_t g_api;
static volatile LONG g_api_state; /* 0 = not resolved, 1 = resolving, 2 = done */

struct msc_api_t *msc_api(void) {
    if (g_api_state == 2) return &g_api;
    if (InterlockedCompareExchange(&g_api_state, 1, 0) == 0) {
#define R(n) g_api.n = U(n)
        R(RegisterClassExW); R(RegisterClassExA); R(CreateWindowExW); R(CreateWindowExA); R(DestroyWindow);
        R(SendMessageW); R(SendMessageA); R(SendMessageTimeoutW); R(SendMessageTimeoutA);
        R(SendNotifyMessageW); R(SendNotifyMessageA); R(SendMessageCallbackW); R(SendMessageCallbackA);
        R(ReplyMessage); R(InSendMessage); R(InSendMessageEx); R(PostMessageW); R(PostThreadMessageW);
        R(PeekMessageW); R(DispatchMessageW); R(DefWindowProcW); R(DefWindowProcA);
        R(GetWindowTextW); R(SetWindowTextW); R(IsWindow); R(IsWindowUnicode);
        R(GetWindowLongPtrW); R(SetWindowLongPtrW); R(RegisterWindowMessageW); R(GetQueueStatus);
        R(IsHungAppWindow); R(FindWindowW); R(GetWindowThreadProcessId); R(ChangeWindowMessageFilterEx);
        R(MsgWaitForMultipleObjectsEx); R(GetDesktopWindow); R(SetWindowPos); R(GetWindowRect);
        R(GetClientRect); R(UnregisterClassW); R(GetClassNameW);
#undef R
        g_api.QuerySendMessage = t_is_wine() ? NULL : (BOOL (WINAPI *)(MSG *))t_proc_in("user32.dll", "QuerySendMessage"); /* Wine: unimplemented stub that aborts */
        g_api.NtUserGetThreadState = (ULONG_PTR (WINAPI *)(DWORD))t_proc_in("win32u.dll", "NtUserGetThreadState");
        g_api.ok = g_api.SendMessageW && g_api.CreateWindowExW && g_api.RegisterClassExW && g_api.SendMessageTimeoutW &&
                   g_api.ReplyMessage && g_api.InSendMessageEx && g_api.PeekMessageW && g_api.DefWindowProcW;
        g_api_state = 2;
    } else {
        while (g_api_state != 2) Sleep(1);
    }
    return &g_api;
}
