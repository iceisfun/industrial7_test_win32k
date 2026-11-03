/* gap_05: the 25 syscalls of this area - win32u stub shape and service number (pure reads of stub bytes,
 * no kernel entry). The numbers are the 19041 win32k shadow-table indexes in tools/win32u_syscalls.tsv. */
#include "g5.h"

static void stub_check(const char *name, long want) {
    HMODULE w = t_module("win32u.dll");
    REQUIRE(w);
    FARPROC s = t_proc_in("win32u.dll", name);
    if (t_is_wine() && !s) { t_skip("Wine win32u does not export %s", name); return; }
    REQUIRE(s);
    if (t_is_wine()) { t_skip("Wine win32u stubs are not win32k syscall stubs"); return; }
    CHECKF(k_stub_valid(s), "%s: not a 19041 x64 syscall stub", name);
    long n = k_stub_number(s);
    CHECKF(n == want, "%s: stub encodes service %ld, want %ld", name, n, want);
    CHECKF(n >= 0x1000, "%s: service 0x%lx outside the win32k shadow table", name, n);
}
#define STUBT(nm, num) TEST(gap5_stub_##nm) { stub_check(#nm, num); }
STUBT(NtUserSoundSentry, 5379)
STUBT(NtUserWindowFromPhysicalPoint, 5407)
STUBT(NtGdiAddEmbFontToDC, 4477)
STUBT(NtGdiAddRemoteFontToDC, 4480)
STUBT(NtGdiRemoveMergeFont, 4857)
STUBT(NtUserGetActiveProcessesDpis, 5107)
STUBT(NtGdiClearBitmapAttributes, 4497)
STUBT(NtGdiSetBitmapAttributes, 4873)
STUBT(NtGdiEudcLoadUnloadLink, 4758)
STUBT(NtGdiGetEudcTimeStampEx, 4796)
STUBT(NtUserHwndQueryRedirectionInfo, 5203)
STUBT(NtUserMagGetContextInformation, 5246)
STUBT(NtUserMagSetContextInformation, 5247)
STUBT(NtGdiGetStringBitmapW, 4823)
STUBT(NtCreateCompositionInputSink, 4378)
STUBT(NtDCompositionCreateChannel, 4388)
STUBT(NtDCompositionDestroyChannel, 4393)
STUBT(NtGdiClearBrushAttributes, 4498)
STUBT(NtGdiSetBrushAttributes, 4874)
STUBT(NtGdiEnableEudc, 4721)
STUBT(NtGdiFontIsLinked, 4769)
STUBT(NtGdiGetCharWidthInfo, 4288)
STUBT(NtGdiGetCurrentDpiInfo, 4785)
STUBT(NtGdiGetFontFileData, 4797)
STUBT(NtGdiGetFontFileInfo, 4798)

/* The two brush-attribute syscalls and the two window-from-point syscalls are distinct service numbers
 * even though win32kfull binds each pair to one body (Set/Clear brush: xor eax,eax; ret at 0x52ed0;
 * WindowFromPhysicalPoint/WindowFromPoint: 0xf8b00). */
TEST(gap5_stub_pairs_have_distinct_numbers) {
    HMODULE w = t_module("win32u.dll");
    REQUIRE(w);
    if (t_is_wine()) { t_skip("Wine"); return; }
    FARPROC a = t_proc_in("win32u.dll", "NtGdiSetBrushAttributes");
    FARPROC b = t_proc_in("win32u.dll", "NtGdiClearBrushAttributes");
    FARPROC c = t_proc_in("win32u.dll", "NtUserWindowFromPhysicalPoint");
    FARPROC d = t_proc_in("win32u.dll", "NtUserWindowFromPoint");
    REQUIRE(a); REQUIRE(b); REQUIRE(c); REQUIRE(d);
    CHECK(k_stub_number(a) != k_stub_number(b));
    CHECK(k_stub_number(c) != k_stub_number(d));
    CHECK(k_stub_number(c) == 5407);
}
