/* dx_input_inject shared helpers: environment gating, state snapshot/restore, INPUT builders, LL hook recorder,
 * and a tiny visible foreground window that receives the injected input. Everything that touches global input
 * state (cursor position, clip rect, mouse-button swap, toggle keys, held keys, BlockInput) is snapshotted by
 * inj_arm() and put back by inj_restore(), which also runs from an atexit backstop. */
#ifndef INJ_H
#define INJ_H
#include "t.h"
#include "k.h"

#define INJ_ENV() do { if (!inj_env()) return; } while (0)

/* 1 when the process may inject into the interactive default desktop; otherwise t_skip + 0. */
int inj_env(void);
void inj_arm(void);       /* snapshot cursor/clip/swap/toggles, install atexit backstop (idempotent) */
void inj_restore(void);   /* put everything back (also releases keys we may have left down, unblocks input) */
ULONG_PTR inj_tag(void);  /* unique dwExtraInfo tag */
void inj_pump(unsigned ms);             /* Peek/Translate/Dispatch for ms */

void inj_key(INPUT *in, WORD vk, WORD scan, DWORD flags, ULONG_PTR extra);
void inj_mouse(INPUT *in, LONG dx, LONG dy, DWORD data, DWORD flags, ULONG_PTR extra);
UINT inj_send(UINT n, INPUT *in);       /* user32!SendInput with sizeof(INPUT) */
UINT inj_keydown(WORD vk, DWORD flags, ULONG_PTR extra);
UINT inj_keyup(WORD vk, DWORD flags, ULONG_PTR extra);
UINT inj_tap(WORD vk, DWORD flags, ULONG_PTR extra);   /* down then up, returns total inserted */
UINT inj_mouse1(LONG dx, LONG dy, DWORD data, DWORD flags, ULONG_PTR extra);
LONG inj_abs(int px, int total);        /* pixel -> 0..65535 normalized so the kernel maps it back to px */
int  inj_near(LONG a, LONG b, LONG tol);
void inj_screen(RECT *prim, RECT *virt);

/* low level hook recorder */
struct inj_ev { int mouse; WPARAM wp; DWORD vk, scan, flags, mdata, time; LONG x, y; ULONG_PTR extra; };
HHOOK inj_hook_install(int id, HOOKPROC p);
void inj_hook_remove(HHOOK h);
int  inj_hook_begin(int kb, int mouse);  /* installs the recording hooks; returns 1 when all requested installed */
void inj_hook_end(void);
void inj_ev_clear(void);
int  inj_ev_n(void);
struct inj_ev *inj_ev(int i);
int  inj_ev_find(int mouse, ULONG_PTR extra, int from);        /* by dwExtraInfo tag */
int  inj_ev_find_vk(DWORD vk, int from);                       /* keyboard by vkCode and INJECTED flag */
extern DWORD inj_block_vk;       /* kb hook swallows injected events with this vkCode */
extern WPARAM inj_block_mouse_msg; /* mouse hook swallows injected events with this wParam */

/* test window */
struct inj_msg { UINT msg; WPARAM wp; LPARAM lp; LPARAM extra; DWORD time; };
HWND inj_win_create(int x, int y, int w, int h);
int  inj_win_focus(HWND h);
void inj_win_destroy(HWND h);
HWND inj_fg_begin(void);        /* window + focus, or t_skip and NULL */
void inj_fg_end(HWND h);
void inj_msg_clear(void);
int  inj_msg_n(void);
struct inj_msg *inj_msg(int i);
int  inj_msg_find(UINT msg, int from);
int  inj_msg_count(UINT msg);
void inj_win_center(HWND h, POINT *pt);   /* screen coords of window center */
int  inj_cursor_into(HWND h);             /* SetCursorPos to center; 1 when WindowFromPoint == h */

#define VK_PACKET_ 0xE7
#endif
