/* sc_stub_layout: userland routes that reach a win32u stub - the user32/gdi32full code around the
 * IAT call is decoded and must bind to the exact stub, and the TEB.Win32ThreadInfo gate that
 * user32 tests before each win32k call is verified. */
#include "sc_common.h"

/* Finds (FF 15 / FF 25) inside the first `span` bytes of fn and
 * returns the IAT slot values it reads; fills up to max targets. */
static int iat_targets(const unsigned char *fn, unsigned span, void **out, int max) {
    unsigned char b[0x200];
    if (span > sizeof b || !fn || !k_try_read(fn, b, span)) return 0;
    int n = 0;
    for (unsigned i = 0; i + 6 <= span && n < max; i++) {
        if (b[i] != 0xFF || (b[i + 1] != 0x15 && b[i + 1] != 0x25)) continue;
        int rel; memcpy(&rel, b + i + 2, 4);
        void *t = NULL;
        if (k_try_read_ptr(fn + i + 6 + rel, &t) && t) out[n++] = t;
    }
    return n;
}
static int calls_stub(const unsigned char *fn, unsigned span, const char *nt) {
    void *t[16]; int n = iat_targets(fn, span, t, 16);
    for (int i = 0; i < n; i++) if (t[i] == (void *)sc_stub(nt)) return 1;
    return 0;
}

TEST(sc_stub_layout_route_user32_GetKeyState_calls_NtUserGetKeyState) {
    /* user32!GetKeyState reaches the kernel through an IAT call for most keys. */
    if (sc_skip_wine("GetKeyState route")) return;
    const unsigned char *f = (const unsigned char *)t_proc_in("user32.dll", "GetKeyState"); REQUIRE(f);
    unsigned char b[0x40]; REQUIRE(k_try_read(f, b, sizeof b));
    int gate = 0;
    for (unsigned i = 0; i + 5 <= sizeof b; i++)
        if (!memcmp(b + i, "\x48\x83\x78\x78\x00", 5)) gate = 1;   /* cmp qword ptr [rax+0x78], 0 == TEB.Win32ThreadInfo */
    CHECK19(gate);
    CHECK19(calls_stub(f, 0x40, "NtUserGetKeyState"));
    CHECK(TEB_Win32ThreadInfo == 0x78);
}
TEST(sc_stub_layout_route_gdi32full_BitBlt_reaches_NtGdiBitBlt) {
    if (sc_skip_wine("BitBlt route")) return;
    const unsigned char *f = (const unsigned char *)t_proc_in("gdi32full.dll", "BitBlt");
    if (!f) { t_skip("gdi32full BitBlt unavailable"); return; }
    /* the entry chunk is 0xc8 bytes; the NtGdiBitBlt call is inside it on 19041 */
    int hit = calls_stub(f, 0x120, "NtGdiBitBlt");
    CHECK19(hit);
    if (!hit) t_note("no direct IAT call to NtGdiBitBlt in the first 0x120 bytes of gdi32full!BitBlt");
}
TEST(sc_stub_layout_route_gdi32_BitBlt_forwards_to_gdi32full) {
    /* gdi32!BitBlt is not itself a syscall caller: it never references NtGdiBitBlt directly */
    if (sc_skip_wine("gdi32 BitBlt")) return;
    const unsigned char *f = (const unsigned char *)t_proc_in("gdi32.dll", "BitBlt"); REQUIRE(f);
    CHECK(!calls_stub(f, 0x40, "NtGdiBitBlt"));
}
TEST(sc_stub_layout_route_user32_calls_stubs_only_by_iat) {
    /* no user32 export embeds a SYSCALL instruction itself: syscalls exist only inside win32u/ntdll. */
    if (sc_skip_wine("user32 syscall scan")) return;
    static const char *const fn[] = { "GetKeyState", "GetMessageExtraInfo", "GetDoubleClickTime", "GetForegroundWindow", "GetCapture" };
    for (unsigned i = 0; i < sizeof fn / sizeof fn[0]; i++) {
        const unsigned char *f = (const unsigned char *)t_proc_in("user32.dll", fn[i]);
        if (!f) continue;
        unsigned char b[0x40]; if (!k_try_read(f, b, sizeof b)) continue;
        for (unsigned j = 0; j + 1 < sizeof b; j++) CHECKF(!(b[j] == 0x0F && b[j + 1] == 0x05 && j < 0x20 && b[0] == 0x4C), "%s embeds a syscall", fn[i]);
    }
}
