/* gap6_mit: NtMIT* (minuser input thread) and NtRIM* (raw input manager) syscalls reached from an
 * ordinary application thread.
 *
 * The NtMIT* bodies in win32kbase.sys are only usable by the single "input thread" created by the
 * input stack. Each one starts with CInputThread::IsInputThread(), which compares
 * PsGetCurrentThreadId() with the thread id stored in gpInputThread (+0x28); an application thread
 * is never that thread, so the call takes the rejection branch quoted per test. The rejection
 * branches do not touch their pointer arguments except where stated.
 */
#include "gap6.h"


/* NtMITActivateInputProcessing(p1, ULONGLONG *out): not the input thread -> UserSetLastError(5),
 * return 0, and *out is still written after the probe clamp. */
TEST(gap6_mit_activate_input_processing_rejected) {
    g6_fn14 f = g6_stub("NtMITActivateInputProcessing"); REQUIRE(f);
    ULONGLONG out = 0xDEADBEEFCAFEF00DULL;
    SetLastError(0);
    ULONG_PTR r = g6_call(f, 0, (ULONG_PTR)&out, 0, 0);
    CHECK(r == 0);
    CHECK(out == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
/* NtMITDispatchCompletion(p1, p2): thread id != input thread id -> UserSetLastError(5); the
 * function returns 0 unconditionally. */
TEST(gap6_mit_dispatch_completion_rejected) {
    g6_fn14 f = g6_stub("NtMITDispatchCompletion"); REQUIRE(f);
    SetLastError(0);
    CHECK(g6_call0(f) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
/* NtMITSetInputObservationState(int, int, int): not the input thread -> 0xc0000022. */
TEST(gap6_mit_set_input_observation_state_rejected) {
    g6_fn14 f = g6_stub("NtMITSetInputObservationState"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == G6_STATUS_ACCESS_DENIED);
    CHECK((unsigned long)g6_call(f, 1, 1, 1, 0) == G6_STATUS_ACCESS_DENIED);
}
/* NtMITSetKeyboardInputRoutingPolicy(int): needs the input thread AND gpInputThread+0x10 == 2;
 * an application thread fails the first test -> 0xc0000022. */
TEST(gap6_mit_set_keyboard_input_routing_policy_rejected) {
    g6_fn14 f = g6_stub("NtMITSetKeyboardInputRoutingPolicy"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == G6_STATUS_ACCESS_DENIED);
    CHECK((unsigned long)g6_call(f, 1, 0, 0, 0) == G6_STATUS_ACCESS_DENIED);
}
/* NtMITSetKeyboardOverriderState(int): not the input thread -> UserSetLastError(5), result 0.
 * (The accepted branch would flip the keyboard processor's overrider flag, so this is the only
 * safe branch.) */
TEST(gap6_mit_set_keyboard_overrider_state_rejected) {
    g6_fn14 f = g6_stub("NtMITSetKeyboardOverriderState"); REQUIRE(f);
    for (int v = 0; v < 2; v++) {
        SetLastError(0);
        CHECK(g6_call(f, v, 0, 0, 0) == 0);
        EXPECT_ERR(ERROR_ACCESS_DENIED);
    }
}
/* NtMITCoreMsgKOpenConnectionTo(MIT_ENDPOINT, callout *): only the DWM process (g_pepDwm) passes
 * the first test; everything else is -0x3fffffde = 0xc0000022 before the endpoint (> 0x12 would be
 * 0xc000000d) or the callout pointer is looked at. */
TEST(gap6_mit_core_msgk_open_connection_not_dwm) {
    g6_fn14 f = g6_stub("NtMITCoreMsgKOpenConnectionTo"); REQUIRE(f);
    for (ULONG_PTR ep = 0; ep < 0x16; ep += 3)
        CHECK((unsigned long)g6_call(f, ep, 0, 0, 0) == G6_STATUS_ACCESS_DENIED);
    CHECK((unsigned long)g6_call(f, 0xFFFFFFFF, 0, 0, 0) == G6_STATUS_ACCESS_DENIED);
}
/* NtMITInitMinuserThread(HANDLE event): ObReferenceObjectByHandle(NULL, EVENT_ALL_ACCESS, event
 * type) fails, so the thread flag 0x1000000 stays clear and the function returns FALSE. */
TEST(gap6_mit_init_minuser_thread_null_handle) {
    g6_fn14 f = g6_stub("NtMITInitMinuserThread"); REQUIRE(f);
    CHECK((g6_call0(f) & 0xFF) == 0);
    CHECK((g6_call0(f) & 0xFF) == 0);      /* nothing was latched by the first call */
}
/* NtMITPostThreadEventMessage(tid, ULONGLONG *ctx, p3, p4, p5): not the input thread ->
 * UserSetLastError(5), FALSE; ctx is not read on this branch. */
TEST(gap6_mit_post_thread_event_message_rejected) {
    g6_fn14 f = g6_stub("NtMITPostThreadEventMessage"); REQUIRE(f);
    SetLastError(0);
    CHECK((g6_call(f, GetCurrentThreadId(), 0, 0, 0) & 0xFF) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
/* NtMITPostWindowEventMessage(p1, pid, ULONGLONG *ctx, p4, p5, p6): not the input thread ->
 * UserSetLastError(5) and the initial result 0. */
TEST(gap6_mit_post_window_event_message_rejected) {
    g6_fn14 f = g6_stub("NtMITPostWindowEventMessage"); REQUIRE(f);
    SetLastError(0);
    CHECK((g6_call(f, 0, GetCurrentProcessId(), 0, 0) & 0xFF) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}

/* ---- Raw input manager (NtRIM*) ---------------------------------------------------------------- */

/* NtRIMRegisterForInput (win32kfull): after EnterCrit it calls IsProcessDwm(current process);
 * every non-DWM process gets 0xc0000022 and none of the ten arguments is read. */
TEST(gap6_rim_register_for_input_not_dwm) {
    g6_fn14 f = g6_stub("NtRIMRegisterForInput"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == G6_STATUS_ACCESS_DENIED);
    CHECK((unsigned long)g6_call(f, 1, 0, 1, 0) == G6_STATUS_ACCESS_DENIED);
}
/* NtRIMAddInputObserver: argument 7 == 0 returns
 * STATUS_INVALID_PARAMETER before any permission check or pointer use. The 7th argument is the
 * first stack argument after the four register ones and the three following register-spill slots. */
TEST(gap6_rim_add_input_observer_zero_param7) {
    g6_fn14 f = g6_stub("NtRIMAddInputObserver"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == G6_STATUS_INVALID_PARAMETER);
}
/* NtRIMUpdateInputObserverRegistration: argument 2 == 0 ->
 * STATUS_INVALID_PARAMETER. */
TEST(gap6_rim_update_input_observer_registration_zero_param2) {
    g6_fn14 f = g6_stub("NtRIMUpdateInputObserverRegistration"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == G6_STATUS_INVALID_PARAMETER);
}
/* The user-mode RIM entry points are gated on ApiSetEditionIsUsermodeRIMAccessAllowed(), an
 * edition-dependent feature slot, and otherwise resolve a raw-input device handle. With all-zero
 * arguments the gate closed answers 0xc0000022; with the gate open a NULL device handle cannot
 * resolve either. So the only portable claim is "an error status", and the exact value is logged. */
#define RIM_GATED(tag, sym) \
    TEST(gap6_rim_##tag##_zero_args_fails) { \
        g6_fn14 f = g6_stub(sym); REQUIRE(f); \
        unsigned long st = (unsigned long)g6_call0(f); \
        t_note(sym " zero args -> 0x%lx", st); \
        CHECK((long)st < 0); \
    }
RIM_GATED(are_sibling_devices,        "NtRIMAreSiblingDevices")
RIM_GATED(get_device_preparsed_data,  "NtRIMGetDevicePreparsedData")
RIM_GATED(get_device_properties,      "NtRIMGetDeviceProperties")
RIM_GATED(get_physical_device_rect,   "NtRIMGetPhysicalDeviceRect")
RIM_GATED(get_source_process_id,      "NtRIMGetSourceProcessId")
RIM_GATED(read_input,                 "NtRIMReadInput")
RIM_GATED(device_io_control,          "NtRIMDeviceIoControl")
