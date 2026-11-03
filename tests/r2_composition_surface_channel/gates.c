/* Caller-identity gates and argument validation that an ordinary (non-DWM, non-input-thread) process can reach:
 * the NtMIT* input-thread family, DWM-only composition calls not covered by dcomp_flip_dxgk_neg, the telemetry
 * and frame statistics calls, and the early-argument checks of the DWM frame calls.
 *
 * Source (win32kbase.sys 19041):
 *  - CInputThread::IsInputThread() / "PsGetCurrentThreadId() == gpInputThread+0x28" is false for every thread of a
 *    normal process, so the NtMIT* bodies take their rejection branch: UserSetLastError(5) plus a zero/false result, or
 *    0xC0000022 for the ones that return a status (SetInputObservationState, SetKeyboardInputRoutingPolicy).
 *    NtMITActivateInputProcessing stores *argument 2 = 0 even on rejection. NtMITGetCursorUpdateHandle returns -1.
 *  - NtMITCoreMsgKOpenConnectionTo compares PsGetCurrentProcess() with g_pepDwm first -> 0xC0000022.
 *  - NtMITInitMinuserThread with a handle that is not an event: ObReferenceObjectByHandle fails, result FALSE and the
 *    thread flag bit 0x1000000 is not set (NtMITUninitMinuserThread then also returns FALSE).
 *  - NtCompositionInputThread: feature slot missing -> 0xC00000BB which is folded into 0xC0000002; otherwise the DWM
 *    check gives 0xC0000022.
 *  - NtDCompositionTelemetrySetApplicationId(ch, bytes, buf): INVALID_PARAMETER for NULL buf, 0, odd, or > 300 bytes
 *    BEFORE the channel is resolved. AnimationScenarioBegin: 0x30 <= bytes <= 0xF0 and even. Reference/Unreference:
 *    both pointers required.
 *  - NtDCompositionRetireFrame / BeginFrame / ConfirmFrame reject NULL pointers with 0xC000000D. */
#include "rc.h"

UDECL(NTSTATUS, NtCompositionInputThread, (void *mgr, void *p, int n));
UDECL(NTSTATUS, NtUpdateInputSinkTransforms, (void *p, UINT n));
UDECL(NTSTATUS, NtMITCoreMsgKOpenConnectionTo, (UINT endpoint, void *callout));
UDECL(ULONG_PTR, NtMITSynthesizeKeyboardInput, (int opts, void *in, void *extra));
UDECL(ULONG_PTR, NtMITSynthesizeMouseInput, (void *in, int opts, ULONGLONG *extra));
UDECL(BYTE,      NtMITSynthesizeTouchInput, (void *in));
UDECL(UINT,      NtMITPostMouseInputMessage, (ULONGLONG *p, UINT a, ULONGLONG *extra));
UDECL(ULONG_PTR, NtMITActivateInputProcessing, (ULONG_PTR a, ULONGLONG *pOut));
UDECL(NTSTATUS,  NtMITSetInputObservationState, (UINT a, UINT b, UINT c));
UDECL(NTSTATUS,  NtMITSetKeyboardInputRoutingPolicy, (UINT a));
UDECL(ULONG_PTR, NtMITSetKeyboardOverriderState, (int a));
UDECL(ULONG_PTR, NtMITDispatchCompletion, (ULONG_PTR a, ULONG_PTR b));
UDECL(ULONG_PTR, NtMITSetInputCallbacks, (void *cb));
UDECL(ULONG_PTR, NtMITGetCursorUpdateHandle, (void));
UDECL(ULONG_PTR, NtMITSetLastInputRecipient, (UINT tid));
UDECL(ULONG_PTR, NtMITSetInputDelegationMode, (UINT a, UINT b));
UDECL(ULONG_PTR, NtMITEnableMouseIntercept, (int a));
UDECL(ULONG_PTR, NtMITDisableMouseIntercept, (void));
UDECL(BYTE,      NtMITPostThreadEventMessage, (UINT tid, void *a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d));
UDECL(ULONG_PTR, NtMITAccessibilityTimerNotification, (int a));
UDECL(BYTE,      NtMITInitMinuserThread, (HANDLE hEvent));
UDECL(BYTE,      NtMITUninitMinuserThread, (void));
UDECL(BYTE,      NtMITDeactivateInputProcessing, (void));
UDECL(BYTE,      NtMITUpdateInputGlobals, (UINT a, int src, USHORT c, ULONG d, ULONG e));
UDECL(NTSTATUS, NtDCompositionTelemetrySetApplicationId, (UINT ch, ULONG_PTR bytes, void *buf));
UDECL(NTSTATUS, NtDCompositionTelemetryAnimationScenarioBegin, (UINT ch, ULONG_PTR bytes, void *buf));
UDECL(NTSTATUS, NtDCompositionTelemetryAnimationScenarioReference, (UINT ch, GUID *g, ULONGLONG *p));
UDECL(NTSTATUS, NtDCompositionTelemetryAnimationScenarioUnreference, (UINT ch, GUID *g, ULONGLONG *p));
UDECL(NTSTATUS, NtDCompositionRetireFrame, (ULONG_PTR a, void *b, void *c));
UDECL(NTSTATUS, NtDCompositionBeginFrame, (ULONG_PTR a, void *b, ULONG_PTR c));
UDECL(NTSTATUS, NtDCompositionGetFrameStatistics, (ULONGLONG *stats, ULONGLONG *extra));

#define LO32(x) ((ULONG)(ULONG_PTR)(x))
#define NEED_FN(nm) nm##_pfn f_##nm = UN(nm); REQUIRE(f_##nm)

RT(composition_input_thread_non_dwm) {
    NEED_WIN(); NEED_FN(NtCompositionInputThread);
    NTSTATUS st = f_NtCompositionInputThread(NULL, NULL, 0);
    t_note("NtCompositionInputThread -> 0x%08lx", (unsigned long)st);
    CHECK(st == S_NOTIMPL || st == S_DENIED || st == (NTSTATUS)0xC000001C);
}
RT(update_input_sink_transforms_non_dwm) {
    NEED_WIN(); NEED_FN(NtUpdateInputSinkTransforms);
    NTSTATUS st = f_NtUpdateInputSinkTransforms(NULL, 0);
    t_note("NtUpdateInputSinkTransforms -> 0x%08lx", (unsigned long)st);
    CHECK(NT_FAIL(st));                       /* the loop that resolves sink handles only runs for the DWM */
}
RT(mit_open_connection_non_dwm) {
    NEED_WIN(); NEED_FN(NtMITCoreMsgKOpenConnectionTo);
    CHECK_ST(f_NtMITCoreMsgKOpenConnectionTo(0, NULL), S_DENIED);
    CHECK_ST(f_NtMITCoreMsgKOpenConnectionTo(0x13, NULL), S_DENIED);       /* the endpoint range check (> 0x12) comes after the DWM check */
    CHECK_ST(f_NtMITCoreMsgKOpenConnectionTo(0xFFFFFFFFu, NULL), S_DENIED);
}
RT(mit_synthesize_input_rejected) {
    NEED_WIN(); NEED_FN(NtMITSynthesizeKeyboardInput); NEED_FN(NtMITSynthesizeMouseInput); NEED_FN(NtMITSynthesizeTouchInput);
    NEED_FN(NtMITPostMouseInputMessage);
    SetLastError(0);
    CHECK(LO32(f_NtMITSynthesizeKeyboardInput(0, NULL, NULL)) == 0);
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    SetLastError(0);
    CHECK(LO32(f_NtMITSynthesizeMouseInput(NULL, 0, NULL)) == 0);
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    SetLastError(0);
    CHECK(f_NtMITSynthesizeTouchInput(NULL) == 0);
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    SetLastError(0);
    CHECK(f_NtMITPostMouseInputMessage(NULL, 0, NULL) == 0);         /* returns STATUS_SUCCESS (0) but flags the failure through the last error */
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
}
RT(mit_activate_input_processing_zeroes_output) {
    NEED_WIN(); NEED_FN(NtMITActivateInputProcessing);
    ULONGLONG out = 0x1122334455667788ull;
    SetLastError(0);
    CHECK(LO32(f_NtMITActivateInputProcessing(0, &out)) == 0);
    CHECK(out == 0);
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
}
RT(mit_status_returning_calls_denied) {
    NEED_WIN(); NEED_FN(NtMITSetInputObservationState); NEED_FN(NtMITSetKeyboardInputRoutingPolicy);
    CHECK_ST(f_NtMITSetInputObservationState(0, 0, 0), S_DENIED);
    CHECK_ST(f_NtMITSetInputObservationState(1, 1, 1), S_DENIED);
    CHECK_ST(f_NtMITSetKeyboardInputRoutingPolicy(0), S_DENIED);
    CHECK_ST(f_NtMITSetKeyboardInputRoutingPolicy(2), S_DENIED);
}
RT(mit_last_error_five_family) {
    NEED_WIN();
    NEED_FN(NtMITSetKeyboardOverriderState); NEED_FN(NtMITDispatchCompletion); NEED_FN(NtMITSetInputCallbacks);
    NEED_FN(NtMITSetLastInputRecipient); NEED_FN(NtMITSetInputDelegationMode); NEED_FN(NtMITEnableMouseIntercept);
    NEED_FN(NtMITDisableMouseIntercept); NEED_FN(NtMITPostThreadEventMessage); NEED_FN(NtMITAccessibilityTimerNotification);
    NEED_FN(NtMITDeactivateInputProcessing);
    /* each body ends in UserSetLastError(5) and a FALSE/0 result when the caller is not the input thread */
    SetLastError(0); CHECK(LO32(f_NtMITSetKeyboardOverriderState(1)) == 0);       CHECK(GetLastError() == 5);
    SetLastError(0); f_NtMITDispatchCompletion(0, 0);                              CHECK(GetLastError() == 5);
    SetLastError(0); CHECK(LO32(f_NtMITSetInputCallbacks(NULL)) == 0);             CHECK(GetLastError() == 5);
    SetLastError(0); CHECK(LO32(f_NtMITSetLastInputRecipient(GetCurrentThreadId())) == 0); CHECK(GetLastError() == 5);
    SetLastError(0); CHECK(LO32(f_NtMITSetInputDelegationMode(0, 0)) == 0);        CHECK(GetLastError() == 5);
    SetLastError(0); CHECK(LO32(f_NtMITEnableMouseIntercept(0)) == 0);             CHECK(GetLastError() == 5);
    SetLastError(0); CHECK(LO32(f_NtMITDisableMouseIntercept()) == 0);             CHECK(GetLastError() == 5);
    SetLastError(0); CHECK(f_NtMITPostThreadEventMessage(GetCurrentThreadId(), NULL, 0, 0, 0) == 0); CHECK(GetLastError() == 5);
    SetLastError(0); f_NtMITAccessibilityTimerNotification(0);                     CHECK(GetLastError() == 5);
    SetLastError(0); CHECK(f_NtMITDeactivateInputProcessing() == 0);               CHECK(GetLastError() == 5);
}
RT(mit_cursor_update_handle_minus_one) {
    NEED_WIN(); NEED_FN(NtMITGetCursorUpdateHandle);
    SetLastError(0);
    ULONG_PTR r = f_NtMITGetCursorUpdateHandle();
    CHECK(r == (ULONG_PTR)-1);
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
}
RT(mit_minuser_thread_flag_not_set) {
    NEED_WIN(); NEED_FN(NtMITInitMinuserThread); NEED_FN(NtMITUninitMinuserThread); NEED_FN(NtMITUpdateInputGlobals);
    /* Init needs an event handle with 0x1f0003 access; anything else fails inside ObReferenceObjectByHandle and the
     * per-thread "minuser" flag (+0x4d0 bit 24) stays clear, which Uninit reports as FALSE. */
    CHECK(f_NtMITInitMinuserThread(NULL) == 0);
    CHECK(f_NtMITInitMinuserThread((HANDLE)(ULONG_PTR)0x7FFC) == 0);
    CHECK(f_NtMITInitMinuserThread(GetCurrentProcess()) == 0);        /* a process handle is not an event */
    CHECK(f_NtMITUninitMinuserThread() == 0);
    CHECK(f_NtMITUpdateInputGlobals(0, 0, 0, 0, 0) == 0);              /* returns (threadId == gpInputThread thread) */
}
RT(telemetry_application_id_size_table) {
    NEED_WIN(); NEED_FN(NtDCompositionTelemetrySetApplicationId);
    struct rc_chan c; REQUIRE_CHAN(c);
    static WCHAR buf[160] = { 'r', '2', 'c', 's' };
    static const struct { ULONG_PTR bytes; int ptr; NTSTATUS want; } rows[] = {
        { 0, 1, S_INVPARAM }, { 1, 1, S_INVPARAM }, { 3, 1, S_INVPARAM }, { 299, 1, S_INVPARAM }, { 301, 1, S_INVPARAM },
        { 302, 1, S_INVPARAM }, { 400, 1, S_INVPARAM }, { 0x10000, 1, S_INVPARAM }, { 0xFFFFFFFFull, 1, S_INVPARAM },
        { 4, 0, S_INVPARAM }, { 300, 0, S_INVPARAM },
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++)
        CHECKF(f_NtDCompositionTelemetrySetApplicationId(c.id, rows[i].bytes, rows[i].ptr ? buf : NULL) == rows[i].want,
               "row %zu bytes=%llu ptr=%d", i, (unsigned long long)rows[i].bytes, rows[i].ptr);
    /* argument checks come first: a dead channel id does not change the verdict */
    CHECK_ST(f_NtDCompositionTelemetrySetApplicationId(rc_bogus_id(c.id), 3, buf), S_INVPARAM);
    /* valid sizes reach the channel lookup */
    CHECK_ST(f_NtDCompositionTelemetrySetApplicationId(rc_bogus_id(c.id), 4, buf), S_DENIED);
    CHECK_ST(f_NtDCompositionTelemetrySetApplicationId(rc_bogus_id(c.id), 300, buf), S_DENIED);
    rc_chan_destroy(&c);
}
RT(telemetry_scenario_begin_size_table) {
    NEED_WIN(); NEED_FN(NtDCompositionTelemetryAnimationScenarioBegin);
    struct rc_chan c; REQUIRE_CHAN(c);
    static ULONGLONG buf[48];                       /* 8-byte aligned: the body raises a misalignment exception otherwise */
    static const ULONG_PTR bad[] = { 0, 1, 2, 0x2E, 0x2F, 0x31, 0xEF, 0xF1, 0xF2, 0x100, 0x1000, 0xFFFFFFFFull };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECKF(f_NtDCompositionTelemetryAnimationScenarioBegin(c.id, bad[i], buf) == S_INVPARAM, "bytes=0x%llx", (unsigned long long)bad[i]);
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioBegin(c.id, 0x30, NULL), S_INVPARAM);
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioBegin(rc_bogus_id(c.id), 0x30, buf), S_DENIED);   /* lower bound is valid */
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioBegin(rc_bogus_id(c.id), 0xF0, buf), S_DENIED);   /* upper bound is valid */
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioBegin(rc_bogus_id(c.id), 0x32, buf), S_DENIED);
    rc_chan_destroy(&c);
}
RT(telemetry_scenario_reference_pointers) {
    NEED_WIN(); NEED_FN(NtDCompositionTelemetryAnimationScenarioReference); NEED_FN(NtDCompositionTelemetryAnimationScenarioUnreference);
    struct rc_chan c; REQUIRE_CHAN(c);
    GUID g = { 0x12345678, 0x1234, 0x5678, { 1, 2, 3, 4, 5, 6, 7, 8 } }; ULONGLONG v = 7;
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioReference(c.id, NULL, &v), S_INVPARAM);
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioReference(c.id, &g, NULL), S_INVPARAM);
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioUnreference(c.id, NULL, &v), S_INVPARAM);
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioUnreference(c.id, &g, NULL), S_INVPARAM);
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioReference(rc_bogus_id(c.id), &g, &v), S_DENIED);
    CHECK_ST(f_NtDCompositionTelemetryAnimationScenarioUnreference(rc_bogus_id(c.id), &g, &v), S_DENIED);
    CHECK(v == 7);
    rc_chan_destroy(&c);
}
RT(frame_calls_null_arguments) {
    NEED_WIN(); NEED_FN(NtDCompositionRetireFrame); NEED_FN(NtDCompositionBeginFrame);
    NTSTATUS a = f_NtDCompositionRetireFrame(0, NULL, NULL);
    NTSTATUS b = f_NtDCompositionBeginFrame(0, NULL, 0);
    t_note("RetireFrame(NULL,NULL) -> 0x%08lx, BeginFrame(NULL) -> 0x%08lx", (unsigned long)a, (unsigned long)b);
    CHECK(NT_FAIL(a));
    CHECK(NT_FAIL(b));
}
RT(frame_statistics_arguments) {
    NEED_WIN(); NEED_FN(NtDCompositionGetFrameStatistics);
    ULONGLONG st[5], extra[2];
    CHECK_ST(f_NtDCompositionGetFrameStatistics(NULL, NULL), S_INVPARAM);
    CHECK_ST(f_NtDCompositionGetFrameStatistics(NULL, extra), S_INVPARAM);
    memset(st, 0xCD, sizeof st);
    NTSTATUS r = f_NtDCompositionGetFrameStatistics(st, NULL);
    t_note("GetFrameStatistics -> 0x%08lx", (unsigned long)r);
    CHECK(r == 0 || r == S_DENIED);            /* ACCESS_DENIED when there is no default connection (no DWM) */
    if (r == 0) {
        /* DCOMPOSITION_FRAME_STATISTICS = {lastFrameTime, currentCompositionRate(8), currentTime, timeFrequency, nextEstimatedFrameTime} */
        CHECK(st[0] != 0xCDCDCDCDCDCDCDCDull);
        CHECK(st[4] != 0xCDCDCDCDCDCDCDCDull);
        t_info("frame stats: last=%llu rate=0x%llx now=%llu freq=%llu next=%llu", st[0], st[1], st[2], st[3], st[4]);
        memset(extra, 0xCD, sizeof extra);
        CHECK_ST(f_NtDCompositionGetFrameStatistics(st, extra), 0);
        CHECK(extra[0] != 0xCDCDCDCDCDCDCDCDull || extra[1] != 0xCDCDCDCDCDCDCDCDull);   /* the capability info is stored when asked for */
    } else {
        CHECK(st[0] == 0xCDCDCDCDCDCDCDCDull);
    }
}
