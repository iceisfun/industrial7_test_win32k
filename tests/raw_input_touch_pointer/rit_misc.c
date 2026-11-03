/* Syscall stub shape for this area's entry points, input message source, last input time,
 * input locale info and the process-wide mouse-in-pointer switch (children only).
 */
#include "rit.h"

/* Wine ships win32u exports that abort the process with EXCEPTION_WINE_STUB (0x80000100) when an
 * unimplemented syscall is called; that is an emulator limitation, not a conformance result. This
 * handler downgrades it to an ordinary access violation so the harness reports one failed test and
 * carries on (Windows never raises it). It must be added after the harness's own handler so that it
 * runs first; hence installed lazily from the first test. */
static LONG CALLBACK rit_stub_veh(PEXCEPTION_POINTERS ep) {
    if (ep->ExceptionRecord->ExceptionCode == 0x80000100) ep->ExceptionRecord->ExceptionCode = 0xC0000005;
    return EXCEPTION_CONTINUE_SEARCH;
}
void rit_veh_once(void) {
    static int done;
    if (done) return;
    done = 1;
    AddVectoredExceptionHandler(1, rit_stub_veh);
}

/* ---- stub layout: every entry point named in this area is a plain win32u syscall stub -------- */
static void stub_check(const char *name, long num) {
    FARPROC p = t_proc_in("win32u.dll", name);
    REQUIRE(p);
    CHECK(k_stub_valid(p));
    long got = k_stub_number(p);
    if (k_is_19041()) CHECKF(got == num, "%s: service number %ld want %ld", name, got, num);
    else t_note("%s service number %ld (19041: %ld)", name, got, num);
    CHECK(got >= 0x1000);                                  /* win32k table = service table index 1 */
}
#define STUB(n, num) RT(stub_##n) { stub_check(#n, num); }
STUB(NtUserGetRawInputDeviceList, 5175)
STUB(NtUserGetRawInputDeviceInfo, 5174)
STUB(NtUserRegisterRawInputDevices, 5284)
STUB(NtUserGetRegisteredRawInputDevices, 5177)
STUB(NtUserGetRawInputData, 5173)
STUB(NtUserGetRawInputBuffer, 5172)
STUB(NtUserGetPointerDevices, 5160)
STUB(NtUserGetPointerDevice, 5154)
STUB(NtUserGetPointerDeviceProperties, 5158)
STUB(NtUserGetPointerType, 5165)
STUB(NtUserGetPointerInfoList, 5162)
STUB(NtUserGetPointerFrameTimes, 5161)
STUB(NtUserEnableMouseInPointer, 5091)
STUB(NtUserIsMouseInPointerEnabled, 5226)
STUB(NtUserEnableMouseInPointerForWindow, 5092)
STUB(NtUserIsTouchWindow, 5231)
STUB(NtUserModifyWindowTouchCapability, 5253)
STUB(NtUserGetTouchInputInfo, 5183)
STUB(NtUserGetTouchValidationStatus, 5184)
STUB(NtUserSetGestureConfig, 5330)
STUB(NtUserGetGestureConfig, 5128)
STUB(NtUserGetGestureInfo, 5130)
STUB(NtUserGetGestureExtArgs, 5129)
STUB(NtUserRegisterTouchHitTestingWindow, 5289)
STUB(NtUserRegisterPointerDeviceNotifications, 5282)
STUB(NtUserRegisterPointerInputTarget, 5283)
STUB(NtUserGetCurrentInputMessageSource, 5117)
STUB(NtUserGetCIMSSM, 5110)
STUB(NtUserGetInputLocaleInfo, 5137)
STUB(NtUserGetPointerDeviceCursors, 5155)
STUB(NtUserGetPointerDeviceRects, 5159)
STUB(NtUserGetPointerCursorId, 5153)
STUB(NtUserGetPointerProprietaryId, 5164)
STUB(NtUserGetPointerInputTransform, 5163)
STUB(NtUserGetPointerDeviceOrientation, 5157)
STUB(NtUserGetPointerDeviceInputSpace, 5156)
STUB(NtUserGetExtendedPointerDeviceProperty, 5127)
STUB(NtUserGetRawPointerDeviceData, 5176)
STUB(NtUserDiscardPointerFrameMessages, 5071)
/* ---- input message source -------------------------------------------------------------------- */
RT(cims_null_arg) {
    WINE_UNIMPL("NtUserGetCIMSSM/NtUserGetCurrentInputMessageSource");
    REQUIRE(U(GetCurrentInputMessageSource));
    REQUIRE(U(GetCIMSSM));
    SET_ERR(0);
    CHECK(!U(GetCurrentInputMessageSource)(NULL));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetCIMSSM)(NULL));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(cims_sizeof) { CHECK(sizeof(INPUT_MESSAGE_SOURCE) == 8); }
RT(cims_ok_on_gui_thread) {
    WINE_UNIMPL("NtUserGetCIMSSM/NtUserGetCurrentInputMessageSource");
    REQUIRE(U(GetCurrentInputMessageSource));
    rit_gui();
    INPUT_MESSAGE_SOURCE s; memset(&s, 0x7e, sizeof s);
    CHECK(U(GetCurrentInputMessageSource)(&s));
    CHECK(s.deviceType != 0x7e7e7e7e || s.originId != 0x7e7e7e7e);     /* both dwords written */
    memset(&s, 0x7e, sizeof s);
    BOOL r = U(GetCIMSSM)(&s);
    t_note("GetCIMSSM -> %d (%d, %d)", r, s.deviceType, s.originId);
}

typedef struct { int ok1, ok2; INPUT_MESSAGE_SOURCE a, b; int ok3; INPUT_MESSAGE_SOURCE c; BOOL got_msg; } cimsres;
static DWORD cims_fn(void *p) {
    cimsres *r = p;
    MSG m;
    memset(r, 0, sizeof *r);
    U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    /* a message posted from inside the process is not hardware input: kernel reports unavailable/unavailable */
    U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 7, 0, 0);
    for (int i = 0; i < 100 && !r->got_msg; i++) {
        if (U(PeekMessageW)(&m, NULL, WM_APP + 7, WM_APP + 7, PM_REMOVE)) r->got_msg = TRUE; else Sleep(5);
    }
    r->ok1 = U(GetCurrentInputMessageSource)(&r->a);
    r->ok2 = U(GetCIMSSM)(&r->b);
    return 0;
}
RT(cims_posted_message_unavailable) {
    WINE_UNIMPL("NtUserGetCIMSSM/NtUserGetCurrentInputMessageSource");
    REQUIRE(U(GetCurrentInputMessageSource));
    REQUIRE(U(PostThreadMessageW));
    cimsres r;
    k_run_gui_thread(cims_fn, &r);
    CHECK(r.got_msg);
    CHECK(r.ok1);
    CHECK(r.a.deviceType == IMDT_UNAVAILABLE);
    CHECK(r.a.originId == IMO_UNAVAILABLE);
    CHECK(r.ok2);
    CHECK(r.b.deviceType == IMDT_UNAVAILABLE);
    CHECK(r.b.originId == IMO_UNAVAILABLE);
}
static DWORD cims_fresh_fn(void *p) {
    cimsres *r = p;
    memset(r, 0, sizeof *r);
    r->a.deviceType = 0x55; r->a.originId = 0x55;
    r->ok1 = U(GetCurrentInputMessageSource)(&r->a);
    return 0;
}
RT(cims_fresh_thread_unavailable) {
    WINE_UNIMPL("NtUserGetCIMSSM/NtUserGetCurrentInputMessageSource");
    REQUIRE(U(GetCurrentInputMessageSource));
    cimsres r;
    k_run_gui_thread(cims_fresh_fn, &r);
    CHECK(r.ok1);
    /* the thread keeps the last hardware source seen by the thread: zero for a thread that never got input */
    CHECK(r.a.deviceType == IMDT_UNAVAILABLE);
    CHECK(r.a.originId == IMO_UNAVAILABLE);
}
RT(cims_direct_stub_matches) {
    WINE_UNIMPL("NtUserGetCIMSSM/NtUserGetCurrentInputMessageSource");
    REQUIRE(UN(NtUserGetCurrentInputMessageSource));
    REQUIRE(UN(NtUserGetCIMSSM));
    rit_gui();
    INPUT_MESSAGE_SOURCE a, b;
    memset(&a, 1, sizeof a); memset(&b, 2, sizeof b);
    CHECK(UN(NtUserGetCurrentInputMessageSource)(&a));
    CHECK(U(GetCurrentInputMessageSource)(&b));
    CHECK(a.deviceType == b.deviceType && a.originId == b.originId);
    SET_ERR(0);
    CHECK(!UN(NtUserGetCurrentInputMessageSource)(NULL));
    EXPECT_ERR(E_INVALID_PARAM);
}

/* ---- last input time ------------------------------------------------------------------------- */
RT(lastinput_cbsize) {
    REQUIRE(U(GetLastInputInfo));
    UINT bad[] = { 0, 4, 12, 16, 0xffffffff };
    for (unsigned i = 0; i < 5; i++) {
        LASTINPUTINFO li; li.cbSize = bad[i]; li.dwTime = 0x1234;
        SET_ERR(0);
        CHECKF(!U(GetLastInputInfo)(&li), "cb %u", bad[i]);
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(li.dwTime == 0x1234);
    }
    CHECK(sizeof(LASTINPUTINFO) == 8);
}
RT(lastinput_reads_serverinfo) {
    REQUIRE(U(GetLastInputInfo));
    rit_gui();
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("gSharedInfo unreadable"); return; }
    unsigned long v1 = 0, v2 = 0;
    CHECK(k_try_read_u32((char *)si.psi + 0x1368, &v1));
    LASTINPUTINFO li = { sizeof li, 0 };
    CHECK(U(GetLastInputInfo)(&li));
    CHECK(k_try_read_u32((char *)si.psi + 0x1368, &v2));
    /* user32!GetLastInputInfo copies a DWORD from the shared server info (offset 0x1368) */
    CHECK(li.dwTime == v1 || li.dwTime == v2);
}
RT(lastinput_not_in_future) {
    REQUIRE(U(GetLastInputInfo));
    LASTINPUTINFO li = { sizeof li, 0 };
    CHECK(U(GetLastInputInfo)(&li));
    DWORD now = GetTickCount();
    /* the stamp is a past tick count (allowing for the coarse timer and wrap) */
    CHECK((LONG)(now - li.dwTime) >= -100);
    t_info("last input %lu ms ago", (unsigned long)(now - li.dwTime));
}
RT(lastinput_two_reads_monotonic) {
    REQUIRE(U(GetLastInputInfo));
    LASTINPUTINFO a = { sizeof a, 0 }, b = { sizeof b, 0 };
    CHECK(U(GetLastInputInfo)(&a));
    Sleep(20);
    CHECK(U(GetLastInputInfo)(&b));
    CHECK((LONG)(b.dwTime - a.dwTime) >= 0);
}

/* ---- input locale info ------------------------------------------------------------------------ */
typedef struct { DWORD cb, zero; HKL hkl; DWORD rest[4]; } locinfo;
RT(inputlocale_sizeof) { CHECK(sizeof(locinfo) == 0x20); }
RT(inputlocale_current_layout) {
    WINE_UNIMPL("NtUserGetInputLocaleInfo/NtUserEnableMouseInPointerForWindow");
    REQUIRE(UN(NtUserGetInputLocaleInfo));
    REQUIRE(U(GetKeyboardLayout));
    rit_gui();
    locinfo li; memset(&li, 0xaa, sizeof li); li.cb = 0x20;
    CHECK(UN(NtUserGetInputLocaleInfo)(NULL, &li));
    CHECK(li.cb == 0x20);
    CHECK(li.zero == 0);
    CHECK(li.hkl == U(GetKeyboardLayout)(0));        /* hkl == 0 selects the thread's current layout */
}
RT(inputlocale_explicit_hkl) {
    WINE_UNIMPL("NtUserGetInputLocaleInfo/NtUserEnableMouseInPointerForWindow");
    REQUIRE(UN(NtUserGetInputLocaleInfo));
    REQUIRE(U(GetKeyboardLayout));
    rit_gui();
    HKL k = U(GetKeyboardLayout)(0);
    locinfo li; memset(&li, 0, sizeof li); li.cb = 0x20;
    CHECK(UN(NtUserGetInputLocaleInfo)(k, &li));
    CHECK(li.hkl == k);
}
RT(inputlocale_each_loaded_layout) {
    WINE_UNIMPL("NtUserGetInputLocaleInfo/NtUserEnableMouseInPointerForWindow");
    REQUIRE(UN(NtUserGetInputLocaleInfo));
    REQUIRE(U(GetKeyboardLayoutList));
    rit_gui();
    HKL l[32]; int n = U(GetKeyboardLayoutList)(32, l);
    CHECK(n >= 1);
    for (int i = 0; i < n; i++) {
        locinfo li; memset(&li, 0, sizeof li); li.cb = 0x20;
        SET_ERR(0);
        BOOL r = UN(NtUserGetInputLocaleInfo)(l[i], &li);
        if (r) CHECK(li.hkl == l[i]);
        else t_note("layout %p not in this thread's list (err %lu)", l[i], GetLastError());
    }
}
RT(inputlocale_bad_cb) {
    WINE_UNIMPL("NtUserGetInputLocaleInfo/NtUserEnableMouseInPointerForWindow");
    REQUIRE(UN(NtUserGetInputLocaleInfo));
    rit_gui();
    DWORD bad[] = { 0, 0x1c, 0x1f, 0x21, 0x28, 0x100 };
    for (unsigned i = 0; i < 6; i++) {
        locinfo li; memset(&li, 0xaa, sizeof li); li.cb = bad[i];
        SET_ERR(0);
        CHECKF(!UN(NtUserGetInputLocaleInfo)(NULL, &li), "cb 0x%lx", bad[i]);
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(li.zero == 0xaaaaaaaa);
    }
}
RT(inputlocale_unknown_hkl) {
    WINE_UNIMPL("NtUserGetInputLocaleInfo/NtUserEnableMouseInPointerForWindow");
    REQUIRE(UN(NtUserGetInputLocaleInfo));
    rit_gui();
    HKL bad[] = { (HKL)(ULONG_PTR)0x12345678, (HKL)(ULONG_PTR)1, (HKL)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 3; i++) {
        locinfo li; memset(&li, 0, sizeof li); li.cb = 0x20;
        SET_ERR(0);
        CHECK(!UN(NtUserGetInputLocaleInfo)(bad[i], &li));
        EXPECT_ERR(E_INVALID_HANDLE);
        CHECK(li.hkl == NULL);
    }
}
RT(inputlocale_langid_matches) {
    WINE_UNIMPL("NtUserGetInputLocaleInfo/NtUserEnableMouseInPointerForWindow");
    REQUIRE(UN(NtUserGetInputLocaleInfo));
    REQUIRE(U(GetKeyboardLayout));
    rit_gui();
    locinfo li; memset(&li, 0, sizeof li); li.cb = 0x20;
    CHECK(UN(NtUserGetInputLocaleInfo)(NULL, &li));
    t_note("input locale info: hkl %p rest %08lx %08lx %08lx %08lx", li.hkl, li.rest[0], li.rest[1], li.rest[2], li.rest[3]);
    CHECK(((ULONG_PTR)li.hkl & 0xffff) != 0);          /* low word of an HKL is a LANGID */
}

/* ---- mouse-in-pointer: validation that changes nothing ---------------------------------------- */
RT(mip_enable_bad_values) {
    REQUIRE(UN(NtUserEnableMouseInPointer));
    rit_gui();
    UINT bad[] = { 2, 3, 0x100, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < 5; i++) {
        SET_ERR(0);
        /* (param != 0) == param only for 0 and 1; everything else is 87 and leaves the process flags alone */
        CHECKF(UN(NtUserEnableMouseInPointer)(bad[i]) == 0, "val 0x%x", bad[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(mip_is_enabled_is_boolean) {
    REQUIRE(U(IsMouseInPointerEnabled));
    BOOL v = U(IsMouseInPointerEnabled)();
    CHECK(v == 0 || v == 1);                         /* bit 2 of the process flags */
    CHECK(UN(NtUserIsMouseInPointerEnabled)() == (UINT)v);
}
RT(mip_enable_for_window_bad_value) {
    REQUIRE(UN(NtUserEnableMouseInPointerForWindow));
    HWND h = k_make_window("mip");
    if (!h) { t_skip("no window"); return; }
    SET_ERR(0);
    CHECK(UN(NtUserEnableMouseInPointerForWindow)(h, 2) == 0);
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(h);
}
RT(mip_enable_for_window_invalid_hwnd) {
    WINE_UNIMPL("NtUserGetInputLocaleInfo/NtUserEnableMouseInPointerForWindow");
    REQUIRE(UN(NtUserEnableMouseInPointerForWindow));
    rit_gui();
    SET_ERR(0);
    CHECK(UN(NtUserEnableMouseInPointerForWindow)(NULL, 1) == 0);
    EXPECT_ERR(E_INVALID_HWND);
    SET_ERR(0);
    CHECK(UN(NtUserEnableMouseInPointerForWindow)((HWND)(ULONG_PTR)0x7fff1234, 0) == 0);
    EXPECT_ERR(E_INVALID_HWND);
}

/* ---- mouse-in-pointer: process-wide, irreversible -> children ---------------------------------- */
CHILD(rit_mip_enable) {
    int nfail = 0; (void)argc; (void)argv;
    if (!U(EnableMouseInPointer)) return 0;
    KC(!U(IsMouseInPointerEnabled)());                 /* default: bit 2 of the process flags clear */
    SET_ERR(0);
    BOOL r = U(EnableMouseInPointer)(TRUE);
    if (!r) { printf("  NOTE EnableMouseInPointer(TRUE) denied, err %lu\n", GetLastError()); return nfail; }
    KC(U(IsMouseInPointerEnabled)());
    KC(U(EnableMouseInPointer)(TRUE));                 /* same value again: allowed */
    SET_ERR(0);
    /* explicit once (bit 3): flipping the value is refused with ERROR_ACCESS_DENIED */
    KC(!U(EnableMouseInPointer)(FALSE));
    KC(GetLastError() == 5);
    KC(U(IsMouseInPointerEnabled)());
    /* pointer id 1 is the mouse and still reports PT_MOUSE; GetPointerInfo needs a live pointer message */
    POINTER_INPUT_TYPE t = 0;
    KC(U(GetPointerType)(1, &t));
    KC(t == PT_MOUSE);
    POINTER_INFO pi;
    SET_ERR(0);
    KC(!U(GetPointerInfo)(1, &pi));
    KC(GetLastError() == 87);
    return nfail;
}
RT(mip_child_enable) {
    struct k_child_result r = k_child_run("rit_mip_enable", "", 20000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}

CHILD(rit_mip_disable_first) {
    int nfail = 0; (void)argc; (void)argv;
    if (!U(EnableMouseInPointer)) return 0;
    KC(U(EnableMouseInPointer)(FALSE));                /* sets the "explicit" bit with the value cleared */
    KC(!U(IsMouseInPointerEnabled)());
    SET_ERR(0);
    KC(!U(EnableMouseInPointer)(TRUE));
    KC(GetLastError() == 5);
    KC(!U(IsMouseInPointerEnabled)());
    KC(U(EnableMouseInPointer)(FALSE));
    return nfail;
}
RT(mip_child_disable_first) {
    struct k_child_result r = k_child_run("rit_mip_disable_first", "", 20000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}

static DWORD WINAPI mip_thread_probe(LPVOID p) { *(BOOL *)p = U(IsMouseInPointerEnabled)(); return 0; }
CHILD(rit_mip_threads) {
    int nfail = 0; (void)argc; (void)argv;
    if (!U(EnableMouseInPointer)) return 0;
    BOOL seen = 99;
    HANDLE t = CreateThread(NULL, 0, mip_thread_probe, &seen, 0, NULL);
    WaitForSingleObject(t, 5000); CloseHandle(t);
    KC(seen == FALSE);
    if (!U(EnableMouseInPointer)(TRUE)) return nfail;
    seen = 99;
    t = CreateThread(NULL, 0, mip_thread_probe, &seen, 0, NULL);
    WaitForSingleObject(t, 5000); CloseHandle(t);
    KC(seen == TRUE);                                    /* the flag lives in the process, not the thread */
    return nfail;
}
RT(mip_child_process_wide) {
    struct k_child_result r = k_child_run("rit_mip_threads", "", 20000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}

CHILD(rit_mip_window) {
    int nfail = 0; (void)argc; (void)argv;
    if (!UN(NtUserEnableMouseInPointerForWindow)) return 0;
    HWND h = k_make_window("mipw");
    if (!h) return nfail;
    KC(UN(NtUserEnableMouseInPointerForWindow)(h, 1) != 0);
    KC(UN(NtUserEnableMouseInPointerForWindow)(h, 1) != 0);
    SET_ERR(0);
    /* the window now has its explicit bit: switching it off is ERROR_ACCESS_DENIED */
    KC(UN(NtUserEnableMouseInPointerForWindow)(h, 0) == 0);
    KC(GetLastError() == 5);
    HWND h2 = k_make_window("mipw2");
    if (h2) {
        KC(UN(NtUserEnableMouseInPointerForWindow)(h2, 0) != 0);   /* a fresh window may choose either way once */
        SET_ERR(0);
        KC(UN(NtUserEnableMouseInPointerForWindow)(h2, 1) == 0);
        KC(GetLastError() == 5);
        U(DestroyWindow)(h2);
    }
    U(DestroyWindow)(h);
    return nfail;
}
RT(mip_child_window) {
    WINE_UNIMPL("NtUserEnableMouseInPointerForWindow");
    struct k_child_result r = k_child_run("rit_mip_window", "", 20000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}

/* ---- pristine process: the area's kernel entry points right after the GUI conversion ------------ */
/* On Windows 10 a win32k call from a process that has not loaded user32 cannot convert the thread (the kernel callback
 * table does not exist yet and the call ends in STATUS_FATAL_USER_CALLBACK_EXCEPTION), so the thread is converted by the
 * user32 load itself; the first input syscall after it must work. */
CHILD(rit_pristine_input) {
    int nfail = 0; (void)argc; (void)argv;
    void *teb = k_teb(), *pti = (void *)1;
    k_try_read_ptr((char *)teb + TEB_Win32ThreadInfo, &pti);
    KC(GetModuleHandleA("user32.dll") == NULL);
    KC(pti == NULL);
    KC(LoadLibraryA("user32.dll") != NULL);
    pti = NULL;
    k_try_read_ptr((char *)teb + TEB_Win32ThreadInfo, &pti);
    KC(pti != NULL);                                     /* the thread became a GUI thread while user32 loaded */
    UINT n = 0;
    if (UN(NtUserGetRawInputDeviceList)) {
        KC(UN(NtUserGetRawInputDeviceList)(NULL, &n, 16) == 0);   /* a pure count query succeeds */
        void *pti2 = NULL;
        k_try_read_ptr((char *)teb + TEB_Win32ThreadInfo, &pti2);
        KC(pti2 == pti);
    }
    return nfail;
}
RT(pristine_input_syscall_after_user32_load) {
    struct k_child_result r = k_child_run("rit_pristine_input", "", 20000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}
