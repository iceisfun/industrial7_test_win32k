/* Generated from tools/win32u_syscalls.tsv: one TEST per in-scope syscall.
 * Each checks the live win32u.dll export is a 19041 x64 syscall stub (k_stub_valid) whose encoded
 * service number is the one documented in tools/win32u_syscalls.tsv. Pure reads of stub bytes: no kernel entry. */
#include "t.h"
#include "k.h"

static void dcomp_stub_check(const char *name, long want) {
    HMODULE w = t_module("win32u.dll");
    REQUIRE(w);
    if (t_is_wine()) { t_skip("Wine win32u stubs are not win32k syscall stubs"); return; }
    FARPROC s = t_proc_in("win32u.dll", name);
    REQUIRE(s);
    CHECKF(k_stub_valid(s), "%s: export is not a 19041 x64 syscall stub", name);
    long n = k_stub_number(s);
    CHECKF(n == want, "%s: stub encodes service 0x%lx, want 0x%lx", name, n, want);
    CHECKF(n >= 0x1000, "%s: service 0x%lx outside win32k shadow table", name, n);
}
#define STUBT(nm, num) TEST(dcomp_win32kt_stub_##nm) { dcomp_stub_check(#nm, num); }
STUBT(NtBindCompositionSurface, 4370)
STUBT(NtCloseCompositionInputSink, 4371)
STUBT(NtCompositionInputThread, 4372)
STUBT(NtCompositionSetDropTarget, 4373)
STUBT(NtCompositorNotifyExitWindows, 4374)
STUBT(NtCompositorNotifyForegroundChanged, 4375)
STUBT(NtConfigureInputSpace, 4377)
STUBT(NtCreateCompositionInputSink, 4378)
STUBT(NtCreateCompositionSurfaceHandle, 4379)
STUBT(NtCreateImplicitCompositionInputSink, 4380)
STUBT(NtDCompositionAddCrossDeviceVisualChild, 4381)
STUBT(NtDCompositionBeginFrame, 4382)
STUBT(NtDCompositionCommitChannel, 4383)
STUBT(NtDCompositionCommitSynchronizationObject, 4384)
STUBT(NtDCompositionConfirmFrame, 4385)
STUBT(NtDCompositionConnectPipe, 4386)
STUBT(NtDCompositionCreateAndBindSharedSection, 4387)
STUBT(NtDCompositionCreateChannel, 4388)
STUBT(NtDCompositionCreateConnection, 4389)
STUBT(NtDCompositionCreateDwmChannel, 4390)
STUBT(NtDCompositionCreateSharedResourceHandle, 4391)
STUBT(NtDCompositionCreateSynchronizationObject, 4392)
STUBT(NtDCompositionDestroyChannel, 4393)
STUBT(NtDCompositionDestroyConnection, 4394)
STUBT(NtDCompositionDiscardFrame, 4395)
STUBT(NtDCompositionDuplicateHandleToProcess, 4396)
STUBT(NtDCompositionDuplicateSwapchainHandleToDwm, 4397)
STUBT(NtDCompositionEnableMMCSS, 4398)
STUBT(NtDCompositionGetBatchId, 4399)
STUBT(NtDCompositionGetChannels, 4400)
STUBT(NtDCompositionGetConnectionBatch, 4401)
STUBT(NtDCompositionGetDeletedResources, 4402)
STUBT(NtDCompositionGetFrameLegacyTokens, 4403)
STUBT(NtDCompositionGetFrameStatistics, 4404)
STUBT(NtDCompositionGetFrameSurfaceUpdates, 4405)
STUBT(NtDCompositionGetMaterialProperty, 4406)
STUBT(NtDCompositionProcessChannelBatchBuffer, 4407)
STUBT(NtDCompositionReferenceSharedResourceOnDwmChannel, 4408)
STUBT(NtDCompositionRegisterThumbnailVisual, 4409)
STUBT(NtDCompositionRegisterVirtualDesktopVisual, 4410)
STUBT(NtDCompositionReleaseAllResources, 4411)
STUBT(NtDCompositionRemoveCrossDeviceVisualChild, 4412)
STUBT(NtDCompositionRetireFrame, 4413)
STUBT(NtDCompositionSetChannelCommitCompletionEvent, 4414)
STUBT(NtDCompositionSetChannelConnectionId, 4415)
STUBT(NtDCompositionSetChildRootVisual, 4416)
STUBT(NtDCompositionSetDebugCounter, 4417)
STUBT(NtDCompositionSetMaterialProperty, 4418)
STUBT(NtDCompositionSubmitDWMBatch, 4419)
STUBT(NtDCompositionSuspendAnimations, 4420)
STUBT(NtDCompositionSynchronize, 4421)
STUBT(NtDCompositionTelemetryAnimationScenarioBegin, 4422)
STUBT(NtDCompositionTelemetryAnimationScenarioReference, 4423)
STUBT(NtDCompositionTelemetryAnimationScenarioUnreference, 4424)
STUBT(NtDCompositionTelemetrySetApplicationId, 4425)
STUBT(NtDCompositionTelemetryTouchInteractionBegin, 4426)
STUBT(NtDCompositionTelemetryTouchInteractionEnd, 4427)
STUBT(NtDCompositionTelemetryTouchInteractionUpdate, 4428)
STUBT(NtDCompositionUpdatePointerCapture, 4429)
STUBT(NtDCompositionWaitForChannel, 4430)
STUBT(NtDesktopCaptureBits, 4431)
STUBT(NtDuplicateCompositionInputSink, 4432)
STUBT(NtDxgkCreateTrackedWorkload, 4433)
STUBT(NtDxgkDestroyTrackedWorkload, 4434)
STUBT(NtDxgkDispMgrOperation, 4435)
STUBT(NtDxgkDisplayPortOperation, 4436)
STUBT(NtDxgkDuplicateHandle, 4437)
STUBT(NtDxgkEnumAdapters3, 4438)
STUBT(NtDxgkGetAvailableTrackedWorkloadIndex, 4439)
STUBT(NtDxgkGetProcessList, 4440)
STUBT(NtDxgkGetTrackedWorkloadStatistics, 4441)
STUBT(NtDxgkOutputDuplPresentToHwQueue, 4442)
STUBT(NtDxgkPinResources, 4443)
STUBT(NtDxgkRegisterVailProcess, 4444)
STUBT(NtDxgkResetTrackedWorkloadStatistics, 4445)
STUBT(NtDxgkSubmitPresentBltToHwQueue, 4446)
STUBT(NtDxgkSubmitPresentToHwQueue, 4447)
STUBT(NtDxgkUnpinResources, 4448)
STUBT(NtDxgkUpdateTrackedWorkload, 4449)
STUBT(NtDxgkVailConnect, 4450)
STUBT(NtDxgkVailDisconnect, 4451)
STUBT(NtDxgkVailPromoteCompositionSurface, 4452)
STUBT(NtFlipObjectAddContent, 4454)
STUBT(NtFlipObjectAddPoolBuffer, 4455)
STUBT(NtFlipObjectConsumerAcquirePresent, 4456)
STUBT(NtFlipObjectConsumerAdjustUsageReference, 4457)
STUBT(NtFlipObjectConsumerBeginProcessPresent, 4458)
STUBT(NtFlipObjectConsumerEndProcessPresent, 4459)
STUBT(NtFlipObjectConsumerPostMessage, 4460)
STUBT(NtFlipObjectConsumerQueryBufferInfo, 4461)
STUBT(NtFlipObjectCreate, 4462)
STUBT(NtFlipObjectDisconnectEndpoint, 4463)
STUBT(NtFlipObjectEnablePresentStatisticsType, 4464)
STUBT(NtFlipObjectOpen, 4465)
STUBT(NtFlipObjectPresentCancel, 4466)
STUBT(NtFlipObjectQueryBufferAvailableEvent, 4467)
STUBT(NtFlipObjectQueryEndpointConnected, 4468)
STUBT(NtFlipObjectQueryNextMessageToProducer, 4469)
STUBT(NtFlipObjectReadNextMessageToProducer, 4470)
STUBT(NtFlipObjectRemoveContent, 4471)
STUBT(NtFlipObjectRemovePoolBuffer, 4472)
STUBT(NtFlipObjectSetContent, 4473)
STUBT(NtFlipObjectSetMaximumBackchannelQueueDepth, 4474)
STUBT(NtInputSpaceRegionFromPoint, 4910)
STUBT(NtMinGetInputTransform, 4943)
STUBT(NtMinInteropCoreMessagingWithInput, 4944)
STUBT(NtMinQPeekForInput, 4945)
STUBT(NtMinQSuspendInputProcessing, 4946)
STUBT(NtMinQUpdateWakeMask, 4947)
STUBT(NtModerncoreBeginLayoutUpdate, 4948)
STUBT(NtModerncoreCreateDCompositionHwndTarget, 4949)
STUBT(NtModerncoreCreateGDIHwndTarget, 4950)
STUBT(NtModerncoreDestroyDCompositionHwndTarget, 4951)
STUBT(NtModerncoreDestroyGDIHwndTarget, 4952)
STUBT(NtModerncoreEnableResizeLayoutSynchronization, 4953)
STUBT(NtModerncoreGetNavigationWindowVisual, 4954)
STUBT(NtModerncoreGetResizeDCompositionSynchronizationObject, 4955)
STUBT(NtModerncoreGetWindowContentVisual, 4956)
STUBT(NtModerncoreIdleTimerThread, 4957)
STUBT(NtModerncoreIsResizeLayoutSynchronizationEnabled, 4958)
STUBT(NtModerncoreProcessConnect, 4959)
STUBT(NtModerncoreRegisterEnhancedNavigationWindowHandle, 4960)
STUBT(NtModerncoreRegisterNavigationWindowHandle, 4961)
STUBT(NtModerncoreSetNavigationServiceSid, 4962)
STUBT(NtModerncoreUnregisterNavigationWindowHandle, 4963)
STUBT(NtNotifyPresentToCompositionSurface, 4964)
STUBT(NtOpenCompositionSurfaceDirtyRegion, 4965)
STUBT(NtOpenCompositionSurfaceSectionInfo, 4966)
STUBT(NtOpenCompositionSurfaceSwapChainHandleInfo, 4967)
STUBT(NtQueryCompositionInputIsImplicit, 4968)
STUBT(NtQueryCompositionInputQueueAndTransform, 4969)
STUBT(NtQueryCompositionInputSink, 4970)
STUBT(NtQueryCompositionInputSinkLuid, 4971)
STUBT(NtQueryCompositionInputSinkViewId, 4972)
STUBT(NtQueryCompositionSurfaceBinding, 4973)
STUBT(NtQueryCompositionSurfaceHDRMetaData, 4974)
STUBT(NtQueryCompositionSurfaceRenderingRealization, 4975)
STUBT(NtQueryCompositionSurfaceStatistics, 4976)
STUBT(NtSetCompositionSurfaceAnalogExclusive, 4999)
STUBT(NtSetCompositionSurfaceBufferUsage, 5000)
STUBT(NtSetCompositionSurfaceDirectFlipState, 5001)
STUBT(NtSetCompositionSurfaceIndependentFlipInfo, 5002)
STUBT(NtSetCompositionSurfaceStatistics, 5003)
STUBT(NtSetCursorInputSpace, 5004)
STUBT(NtSetPointerDeviceInputSpace, 5005)
STUBT(NtTokenManagerConfirmOutstandingAnalogToken, 5007)
STUBT(NtTokenManagerCreateCompositionTokenHandle, 5008)
STUBT(NtTokenManagerCreateFlipObjectReturnTokenHandle, 5009)
STUBT(NtTokenManagerCreateFlipObjectTokenHandle, 5010)
STUBT(NtTokenManagerGetAnalogExclusiveSurfaceUpdates, 5011)
STUBT(NtTokenManagerGetAnalogExclusiveTokenEvent, 5012)
STUBT(NtTokenManagerOpenSectionAndEvents, 5013)
STUBT(NtTokenManagerThread, 5014)
STUBT(NtUnBindCompositionSurface, 5015)
STUBT(NtVisualCaptureBits, 5409)
static const struct { const char *n; long num; } dcomp_all[] = {
    {"NtBindCompositionSurface", 4370},
    {"NtCloseCompositionInputSink", 4371},
    {"NtCompositionInputThread", 4372},
    {"NtCompositionSetDropTarget", 4373},
    {"NtCompositorNotifyExitWindows", 4374},
    {"NtCompositorNotifyForegroundChanged", 4375},
    {"NtConfigureInputSpace", 4377},
    {"NtCreateCompositionInputSink", 4378},
    {"NtCreateCompositionSurfaceHandle", 4379},
    {"NtCreateImplicitCompositionInputSink", 4380},
    {"NtDCompositionAddCrossDeviceVisualChild", 4381},
    {"NtDCompositionBeginFrame", 4382},
    {"NtDCompositionCommitChannel", 4383},
    {"NtDCompositionCommitSynchronizationObject", 4384},
    {"NtDCompositionConfirmFrame", 4385},
    {"NtDCompositionConnectPipe", 4386},
    {"NtDCompositionCreateAndBindSharedSection", 4387},
    {"NtDCompositionCreateChannel", 4388},
    {"NtDCompositionCreateConnection", 4389},
    {"NtDCompositionCreateDwmChannel", 4390},
    {"NtDCompositionCreateSharedResourceHandle", 4391},
    {"NtDCompositionCreateSynchronizationObject", 4392},
    {"NtDCompositionDestroyChannel", 4393},
    {"NtDCompositionDestroyConnection", 4394},
    {"NtDCompositionDiscardFrame", 4395},
    {"NtDCompositionDuplicateHandleToProcess", 4396},
    {"NtDCompositionDuplicateSwapchainHandleToDwm", 4397},
    {"NtDCompositionEnableMMCSS", 4398},
    {"NtDCompositionGetBatchId", 4399},
    {"NtDCompositionGetChannels", 4400},
    {"NtDCompositionGetConnectionBatch", 4401},
    {"NtDCompositionGetDeletedResources", 4402},
    {"NtDCompositionGetFrameLegacyTokens", 4403},
    {"NtDCompositionGetFrameStatistics", 4404},
    {"NtDCompositionGetFrameSurfaceUpdates", 4405},
    {"NtDCompositionGetMaterialProperty", 4406},
    {"NtDCompositionProcessChannelBatchBuffer", 4407},
    {"NtDCompositionReferenceSharedResourceOnDwmChannel", 4408},
    {"NtDCompositionRegisterThumbnailVisual", 4409},
    {"NtDCompositionRegisterVirtualDesktopVisual", 4410},
    {"NtDCompositionReleaseAllResources", 4411},
    {"NtDCompositionRemoveCrossDeviceVisualChild", 4412},
    {"NtDCompositionRetireFrame", 4413},
    {"NtDCompositionSetChannelCommitCompletionEvent", 4414},
    {"NtDCompositionSetChannelConnectionId", 4415},
    {"NtDCompositionSetChildRootVisual", 4416},
    {"NtDCompositionSetDebugCounter", 4417},
    {"NtDCompositionSetMaterialProperty", 4418},
    {"NtDCompositionSubmitDWMBatch", 4419},
    {"NtDCompositionSuspendAnimations", 4420},
    {"NtDCompositionSynchronize", 4421},
    {"NtDCompositionTelemetryAnimationScenarioBegin", 4422},
    {"NtDCompositionTelemetryAnimationScenarioReference", 4423},
    {"NtDCompositionTelemetryAnimationScenarioUnreference", 4424},
    {"NtDCompositionTelemetrySetApplicationId", 4425},
    {"NtDCompositionTelemetryTouchInteractionBegin", 4426},
    {"NtDCompositionTelemetryTouchInteractionEnd", 4427},
    {"NtDCompositionTelemetryTouchInteractionUpdate", 4428},
    {"NtDCompositionUpdatePointerCapture", 4429},
    {"NtDCompositionWaitForChannel", 4430},
    {"NtDesktopCaptureBits", 4431},
    {"NtDuplicateCompositionInputSink", 4432},
    {"NtDxgkCreateTrackedWorkload", 4433},
    {"NtDxgkDestroyTrackedWorkload", 4434},
    {"NtDxgkDispMgrOperation", 4435},
    {"NtDxgkDisplayPortOperation", 4436},
    {"NtDxgkDuplicateHandle", 4437},
    {"NtDxgkEnumAdapters3", 4438},
    {"NtDxgkGetAvailableTrackedWorkloadIndex", 4439},
    {"NtDxgkGetProcessList", 4440},
    {"NtDxgkGetTrackedWorkloadStatistics", 4441},
    {"NtDxgkOutputDuplPresentToHwQueue", 4442},
    {"NtDxgkPinResources", 4443},
    {"NtDxgkRegisterVailProcess", 4444},
    {"NtDxgkResetTrackedWorkloadStatistics", 4445},
    {"NtDxgkSubmitPresentBltToHwQueue", 4446},
    {"NtDxgkSubmitPresentToHwQueue", 4447},
    {"NtDxgkUnpinResources", 4448},
    {"NtDxgkUpdateTrackedWorkload", 4449},
    {"NtDxgkVailConnect", 4450},
    {"NtDxgkVailDisconnect", 4451},
    {"NtDxgkVailPromoteCompositionSurface", 4452},
    {"NtFlipObjectAddContent", 4454},
    {"NtFlipObjectAddPoolBuffer", 4455},
    {"NtFlipObjectConsumerAcquirePresent", 4456},
    {"NtFlipObjectConsumerAdjustUsageReference", 4457},
    {"NtFlipObjectConsumerBeginProcessPresent", 4458},
    {"NtFlipObjectConsumerEndProcessPresent", 4459},
    {"NtFlipObjectConsumerPostMessage", 4460},
    {"NtFlipObjectConsumerQueryBufferInfo", 4461},
    {"NtFlipObjectCreate", 4462},
    {"NtFlipObjectDisconnectEndpoint", 4463},
    {"NtFlipObjectEnablePresentStatisticsType", 4464},
    {"NtFlipObjectOpen", 4465},
    {"NtFlipObjectPresentCancel", 4466},
    {"NtFlipObjectQueryBufferAvailableEvent", 4467},
    {"NtFlipObjectQueryEndpointConnected", 4468},
    {"NtFlipObjectQueryNextMessageToProducer", 4469},
    {"NtFlipObjectReadNextMessageToProducer", 4470},
    {"NtFlipObjectRemoveContent", 4471},
    {"NtFlipObjectRemovePoolBuffer", 4472},
    {"NtFlipObjectSetContent", 4473},
    {"NtFlipObjectSetMaximumBackchannelQueueDepth", 4474},
    {"NtInputSpaceRegionFromPoint", 4910},
    {"NtMinGetInputTransform", 4943},
    {"NtMinInteropCoreMessagingWithInput", 4944},
    {"NtMinQPeekForInput", 4945},
    {"NtMinQSuspendInputProcessing", 4946},
    {"NtMinQUpdateWakeMask", 4947},
    {"NtModerncoreBeginLayoutUpdate", 4948},
    {"NtModerncoreCreateDCompositionHwndTarget", 4949},
    {"NtModerncoreCreateGDIHwndTarget", 4950},
    {"NtModerncoreDestroyDCompositionHwndTarget", 4951},
    {"NtModerncoreDestroyGDIHwndTarget", 4952},
    {"NtModerncoreEnableResizeLayoutSynchronization", 4953},
    {"NtModerncoreGetNavigationWindowVisual", 4954},
    {"NtModerncoreGetResizeDCompositionSynchronizationObject", 4955},
    {"NtModerncoreGetWindowContentVisual", 4956},
    {"NtModerncoreIdleTimerThread", 4957},
    {"NtModerncoreIsResizeLayoutSynchronizationEnabled", 4958},
    {"NtModerncoreProcessConnect", 4959},
    {"NtModerncoreRegisterEnhancedNavigationWindowHandle", 4960},
    {"NtModerncoreRegisterNavigationWindowHandle", 4961},
    {"NtModerncoreSetNavigationServiceSid", 4962},
    {"NtModerncoreUnregisterNavigationWindowHandle", 4963},
    {"NtNotifyPresentToCompositionSurface", 4964},
    {"NtOpenCompositionSurfaceDirtyRegion", 4965},
    {"NtOpenCompositionSurfaceSectionInfo", 4966},
    {"NtOpenCompositionSurfaceSwapChainHandleInfo", 4967},
    {"NtQueryCompositionInputIsImplicit", 4968},
    {"NtQueryCompositionInputQueueAndTransform", 4969},
    {"NtQueryCompositionInputSink", 4970},
    {"NtQueryCompositionInputSinkLuid", 4971},
    {"NtQueryCompositionInputSinkViewId", 4972},
    {"NtQueryCompositionSurfaceBinding", 4973},
    {"NtQueryCompositionSurfaceHDRMetaData", 4974},
    {"NtQueryCompositionSurfaceRenderingRealization", 4975},
    {"NtQueryCompositionSurfaceStatistics", 4976},
    {"NtSetCompositionSurfaceAnalogExclusive", 4999},
    {"NtSetCompositionSurfaceBufferUsage", 5000},
    {"NtSetCompositionSurfaceDirectFlipState", 5001},
    {"NtSetCompositionSurfaceIndependentFlipInfo", 5002},
    {"NtSetCompositionSurfaceStatistics", 5003},
    {"NtSetCursorInputSpace", 5004},
    {"NtSetPointerDeviceInputSpace", 5005},
    {"NtTokenManagerConfirmOutstandingAnalogToken", 5007},
    {"NtTokenManagerCreateCompositionTokenHandle", 5008},
    {"NtTokenManagerCreateFlipObjectReturnTokenHandle", 5009},
    {"NtTokenManagerCreateFlipObjectTokenHandle", 5010},
    {"NtTokenManagerGetAnalogExclusiveSurfaceUpdates", 5011},
    {"NtTokenManagerGetAnalogExclusiveTokenEvent", 5012},
    {"NtTokenManagerOpenSectionAndEvents", 5013},
    {"NtTokenManagerThread", 5014},
    {"NtUnBindCompositionSurface", 5015},
    {"NtVisualCaptureBits", 5409},
};

/* Service numbers must be unique across the whole in-scope set (each syscall has its own slot in
 * the win32k service table) and every stub must be reachable only through win32u. */
TEST(dcomp_win32kt_numbers_unique) {
    size_t n = sizeof(dcomp_all)/sizeof(dcomp_all[0]);
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            CHECKF(dcomp_all[i].num != dcomp_all[j].num, "%s and %s share service %ld",
                   dcomp_all[i].n, dcomp_all[j].n, dcomp_all[i].num);
}

/* A stub export name is never forwarded to another module: GetProcAddress with win32u pinned and
 * the generic search must return the same address. */
TEST(dcomp_win32kt_stub_resolution_consistent) {
    size_t n = sizeof(dcomp_all)/sizeof(dcomp_all[0]);
    for (size_t i = 0; i < n; i++) {
        FARPROC a = t_proc_in("win32u.dll", dcomp_all[i].n), b = t_proc(dcomp_all[i].n);
        CHECKF(a != NULL, "%s missing from win32u.dll", dcomp_all[i].n);
        if (a) CHECKF(a == b, "%s: pinned %p vs search %p", dcomp_all[i].n, (void*)a, (void*)b);
    }
}

/* Stubs are contiguous-family aligned: adjacent service numbers of the same family are not
 * required, but each stub is 16-byte aligned like every win32u export. */
TEST(dcomp_win32kt_stub_alignment) {
    if (t_is_wine()) { t_skip("Wine win32u layout differs"); return; }
    size_t n = sizeof(dcomp_all)/sizeof(dcomp_all[0]);
    for (size_t i = 0; i < n; i++) {
        FARPROC a = t_proc_in("win32u.dll", dcomp_all[i].n);
        if (!a) continue;
        CHECKF((((ULONG_PTR)a) & 0xF) == 0, "%s at %p not 16-byte aligned", dcomp_all[i].n, (void*)a);
    }
}
