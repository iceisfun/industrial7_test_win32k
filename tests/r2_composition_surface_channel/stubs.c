/* Stub layout of every syscall in this area, generated from tools/win32u_syscalls.tsv: each win32u export is a
 * 19041 x64 syscall stub (k_stub_valid) that encodes exactly the documented service number, the numbers are
 * unique and lie in the win32k shadow table (>= 0x1000), and neighbouring numbers of one family stay contiguous.
 * Pure reads of stub bytes, no kernel entry. */
#include "rc.h"

static const struct { const char *name; long num; } stubs[] = {
    {"NtBindCompositionSurface", 4370},
    {"NtCloseCompositionInputSink", 4371},
    {"NtCompositionInputThread", 4372},
    {"NtCompositionSetDropTarget", 4373},
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
    {"NtMITAccessibilityTimerNotification", 4912},
    {"NtMITActivateInputProcessing", 4913},
    {"NtMITCoreMsgKOpenConnectionTo", 4914},
    {"NtMITDeactivateInputProcessing", 4915},
    {"NtMITDisableMouseIntercept", 4916},
    {"NtMITDispatchCompletion", 4917},
    {"NtMITEnableMouseIntercept", 4918},
    {"NtMITGetCursorUpdateHandle", 4919},
    {"NtMITInitMinuserThread", 4920},
    {"NtMITMinuserSetInputTransformOffset", 4921},
    {"NtMITMinuserWindowCreated", 4922},
    {"NtMITMinuserWindowDestroyed", 4923},
    {"NtMITPostMouseInputMessage", 4924},
    {"NtMITPostThreadEventMessage", 4925},
    {"NtMITPostWindowEventMessage", 4926},
    {"NtMITPrepareReceiveInputMessage", 4927},
    {"NtMITPrepareSendInputMessage", 4928},
    {"NtMITProcessDelegateCapturedPointers", 4929},
    {"NtMITSetForegroundRoutingInfo", 4930},
    {"NtMITSetInputCallbacks", 4931},
    {"NtMITSetInputDelegationMode", 4932},
    {"NtMITSetInputObservationState", 4933},
    {"NtMITSetKeyboardInputRoutingPolicy", 4934},
    {"NtMITSetKeyboardOverriderState", 4935},
    {"NtMITSetLastInputRecipient", 4936},
    {"NtMITSynthesizeKeyboardInput", 4937},
    {"NtMITSynthesizeMouseInput", 4938},
    {"NtMITSynthesizeTouchInput", 4939},
    {"NtMITUninitMinuserThread", 4940},
    {"NtMITUpdateInputGlobals", 4941},
    {"NtMapVisualRelativePoints", 4942},
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
    {"NtUnBindCompositionSurface", 5015},
    {"NtUpdateInputSinkTransforms", 5016},
    {"NtUserAddVisualIdentifier", 5020},
    {"NtUserCompositionInputSinkLuidFromPoint", 5044},
    {"NtUserCompositionInputSinkViewInstanceIdFromPoint", 5045},
    {"NtUserConfirmResizeCommit", 5047},
    {"NtUserCreateDCompositionHwndTarget", 5050},
    {"NtUserDestroyDCompositionHwndTarget", 5065},
    {"NtUserDwmValidateWindow", 5088},
    {"NtUserGetResizeDCompositionSynchronizationObject", 5179},
    {"NtUserHwndQueryRedirectionInfo", 5203},
    {"NtUserHwndSetRedirectionInfo", 5204},
    {"NtUserMapPointsByVisualIdentifier", 5248},
    {"NtUserRemoveVisualIdentifier", 5300},
    {"NtValidateCompositionSurfaceHandle", 5408},
};
#define NSTUB (sizeof stubs / sizeof stubs[0])


RT(stubs_are_syscall_stubs) {
    NEED_WIN();
    size_t i; int seen = 0;
    for (i = 0; i < NSTUB; i++) {
        FARPROC p = t_proc_in("win32u.dll", stubs[i].name);
        if (!p) { t_note("%s: not exported by this build", stubs[i].name); continue; }
        seen++;
        CHECKF(k_stub_valid(p), "%s: not a 19041 x64 syscall stub", stubs[i].name);
    }
    CHECK(seen > 0);
}
RT(stubs_encode_documented_numbers) {
    NEED_WIN();
    size_t i;
    for (i = 0; i < NSTUB; i++) {
        FARPROC p = t_proc_in("win32u.dll", stubs[i].name);
        if (!p || !k_stub_valid(p)) continue;
        CHECKF(k_stub_number(p) == stubs[i].num, "%s: service 0x%lx want 0x%lx", stubs[i].name, k_stub_number(p), stubs[i].num);
    }
}
RT(stubs_numbers_in_shadow_table_and_unique) {
    NEED_WIN();
    size_t i, j;
    for (i = 0; i < NSTUB; i++) {
        FARPROC p = t_proc_in("win32u.dll", stubs[i].name);
        if (!p || !k_stub_valid(p)) continue;
        long n = k_stub_number(p);
        CHECKF(n >= 0x1000, "%s: 0x%lx is not a win32k service number", stubs[i].name, n);
        for (j = i + 1; j < NSTUB; j++) {
            FARPROC q = t_proc_in("win32u.dll", stubs[j].name);
            if (q && k_stub_valid(q)) CHECKF(k_stub_number(q) != n, "%s and %s share service 0x%lx", stubs[i].name, stubs[j].name, n);
        }
    }
}
RT(stubs_neighbours_share_win32k_table) {
    NEED_WIN();
    /* the NtDComposition* block is one contiguous alphabetical run of service numbers */
    size_t i; long prev = -1;
    for (i = 0; i < NSTUB; i++) {
        if (strncmp(stubs[i].name, "NtDComposition", 14)) { prev = -1; continue; }
        FARPROC p = t_proc_in("win32u.dll", stubs[i].name);
        if (!p || !k_stub_valid(p)) continue;
        long n = k_stub_number(p);
        if (prev >= 0) CHECKF(n > prev && n - prev <= 2, "%s: 0x%lx after 0x%lx", stubs[i].name, n, prev);
        prev = n;
    }
}
