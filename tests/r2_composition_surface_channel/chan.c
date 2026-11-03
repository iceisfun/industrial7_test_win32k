/* Application-channel state machine: NtDCompositionCreateChannel / DestroyChannel / CommitChannel / GetBatchId /
 * WaitForChannel / SuspendAnimations / SetDebugCounter / SetChannelConnectionId / SetChannelCommitCompletionEvent /
 * ProcessChannelBatchBuffer / GetDeletedResources / Synchronize / ReleaseAllResources.
 *
 * Source of truth (win32kbase.sys, 19041):
 *  - NtDCompositionCreateChannel(pId, pSize, ppShared) is what dcomp!CDevice::Initialize calls with *pSize = 0x1000;
 *    CApplicationChannel::Create stores the channel id through arg1 and, when *pSize != 0, the section size and the
 *    mapped address through arg2/arg3. Channels live in the per-process CProcessData table
 *    (PsGetCurrentProcessWin32Process()+0x100).
 *  - Every channel-id syscall resolves the id with RtlLookupElementGenericTable; no CProcessData -> 0xC0000001, id
 *    not in the table -> 0xC0000022 (inline in CommitChannel / GetBatchId / ProcessChannelBatchBuffer, and the same
 *    helper ReferenceHandleAndLock for the rest). Because of that, a channel id is process-local and is dead after
 *    DestroyChannel.
 *  - Argument checks that precede the channel lookup return 0xC000000D without touching the channel. */
#include "rc.h"

UDECL(NTSTATUS, NtDCompositionGetBatchId, (UINT id, int type, UINT *pOut));
UDECL(NTSTATUS, NtDCompositionWaitForChannel, (UINT id, int a, int b));
UDECL(NTSTATUS, NtDCompositionSuspendAnimations, (UINT id, int suspend));
UDECL(NTSTATUS, NtDCompositionSetDebugCounter, (UINT id, int on));
UDECL(NTSTATUS, NtDCompositionSetChannelConnectionId, (UINT id, int second, LONGLONG connId));
UDECL(NTSTATUS, NtDCompositionSetChannelCommitCompletionEvent, (UINT id, HANDLE hEvent, int flag));
UDECL(NTSTATUS, NtDCompositionProcessChannelBatchBuffer, (UINT id, UINT size, ULONG *pProcessed, BYTE *pFlag));
UDECL(NTSTATUS, NtDCompositionGetDeletedResources, (UINT id, UINT count, UINT64 *buf, UINT *pOut));
UDECL(NTSTATUS, NtDCompositionSynchronize, (UINT id, UINT64 *pOut));
UDECL(NTSTATUS, NtDCompositionReleaseAllResources, (UINT id, BYTE *pFlag));

#define CHAN_FN(nm) nm##_pfn f_##nm = UN(nm); REQUIRE(f_##nm)

RT(create_returns_id_size_and_mapping) {
    NEED_WIN();
    NtDCompositionCreateChannel_pfn cr = UN(NtDCompositionCreateChannel);
    NtDCompositionDestroyChannel_pfn de = UN(NtDCompositionDestroyChannel);
    REQUIRE(cr); REQUIRE(de);
    UINT id = 0xFFFFFFFFu, size = 0x1000; void *mem = (void *)(ULONG_PTR)0x1;
    NTSTATUS st = cr(&id, &size, &mem);
    if (st < 0) { t_note("CreateChannel status 0x%08lx (composition not available to this session?)", (unsigned long)st); t_skip("no channel"); return; }
    CHECK_ST(st, 0);
    CHECK(id != 0xFFFFFFFFu);                         /* arg1 receives the channel id */
    CHECK(size >= 0x1000 || size == 0);               /* arg2 receives the actual shared-section size */
    CHECK(mem != (void *)(ULONG_PTR)0x1);             /* arg3 receives the mapped address */
    t_info("channel id=0x%x section size=0x%x mapped at %p", id, size, mem);
    if (mem && size) {
        MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
        unsigned char b = 0;
        CHECK(VirtualQuery(mem, &mbi, sizeof mbi) == sizeof mbi);
        CHECK(mbi.State == MEM_COMMIT);
        CHECK(mbi.RegionSize >= size);
        CHECK(k_try_read(mem, &b, 1));                /* kernel mapped it into this process */
        t_note("shared buffer type=0x%lx protect=0x%lx region=0x%llx", (unsigned long)mbi.Type, (unsigned long)mbi.Protect, (unsigned long long)mbi.RegionSize);
    }
    de(id);
}

RT(create_destroy_symmetry_ids_unique) {
    NEED_WIN();
    CHAN_FN(NtDCompositionCommitChannel);
    struct rc_chan c[6]; int i, j, n = 0;
    for (i = 0; i < 6; i++) { c[i] = rc_chan_create(); if (c[i].ok) n++; }
    if (n < 6) { t_note("only %d of 6 channels could be created", n); }
    for (i = 0; i < 6; i++) for (j = i + 1; j < 6; j++) if (c[i].ok && c[j].ok) CHECK(c[i].id != c[j].id);
    for (i = 0; i < 6; i++) if (c[i].ok) {
        UINT b = 0xAAAAAAAAu; BYTE fl = 0x55;
        CHECK_ST(f_NtDCompositionCommitChannel(c[i].id, &b, &fl, 0, NULL), 0);
    }
    for (i = 0; i < 6; i++) rc_chan_destroy(&c[i]);
    if (n) {   /* destroyed ids are gone from the table: lookup fails with ACCESS_DENIED */
        struct rc_chan keep = rc_chan_create();
        if (keep.ok) {
            for (i = 0; i < 6; i++) if (c[i].id) {
                UINT b = 0; BYTE fl = 0;
                if (c[i].id != keep.id) CHECK_ST(f_NtDCompositionCommitChannel(c[i].id, &b, &fl, 0, NULL), S_DENIED);
            }
            rc_chan_destroy(&keep);
        }
    }
}

RT(destroy_channel_is_not_a_handle_close) {
    NEED_WIN();
    unsigned long h0 = rc_handles();
    struct rc_chan c = rc_chan_create();
    if (!c.ok) { t_skip("no channel"); return; }
    unsigned long h1 = rc_handles();
    t_info("handle count around CreateChannel: %lu -> %lu", h0, h1);
    rc_chan_destroy(&c);
    unsigned long h2 = rc_handles();
    CHECKF(h2 <= h1, "handles after destroy %lu > before %lu", h2, h1);   /* nothing grew across destroy */
}

RT(commit_empty_channel_reports_batch_id) {
    NEED_WIN();
    CHAN_FN(NtDCompositionCommitChannel); CHAN_FN(NtDCompositionGetBatchId);
    struct rc_chan c; REQUIRE_CHAN(c);
    UINT batch = 0xAAAAAAAAu, again = 0xBBBBBBBBu; BYTE fl = 0x55;
    /* CommitChannel stores *pBatch = channel+0x178 (the submitted-batch id) and *pFlag = (channel+0x204 != 0), only on success. */
    CHECK_ST(f_NtDCompositionCommitChannel(c.id, &batch, &fl, 0, NULL), 0);
    CHECK(batch != 0xAAAAAAAAu);
    CHECK(fl == 0 || fl == 1);
    /* GetBatchId(type 0) reads the same +0x178 field. */
    CHECK_ST(f_NtDCompositionGetBatchId(c.id, 0, &again), 0);
    CHECK(again == batch);
    t_note("batch id after empty commit = %u, flag = %u", batch, fl);
 /* optional pBatch (arg2 == NULL) is allowed */
    BYTE fl2 = 0x55;
    CHECK_ST(f_NtDCompositionCommitChannel(c.id, NULL, &fl2, 0, NULL), 0);
    CHECK(fl2 == 0 || fl2 == 1);
    rc_chan_destroy(&c);
}

RT(commit_failure_leaves_outputs_untouched) {
    NEED_WIN();
    CHAN_FN(NtDCompositionCommitChannel);
    struct rc_chan c; REQUIRE_CHAN(c);
    UINT batch = 0xAAAAAAAAu; BYTE fl = 0x55;
    /* unknown id: lookup fails (0xC0000022) before any store; outputs are written only when status >= 0. */
    CHECK_ST(f_NtDCompositionCommitChannel(rc_bogus_id(c.id), &batch, &fl, 0, NULL), S_DENIED);
    CHECK(batch == 0xAAAAAAAAu);
    CHECK(fl == 0x55);
    /* observed on Windows 10 22H2: a sync handle value that is not a synchronization object does not stop the
     * commit (discard == 0); the call succeeds and the outputs are written as for any successful commit */
    NTSTATUS st = f_NtDCompositionCommitChannel(c.id, &batch, &fl, 0, (HANDLE)(ULONG_PTR)0x7FFC);
    CHECK_ST(st, 0);
    CHECK(batch != 0xAAAAAAAAu);
    CHECK(fl == 0 || fl == 1);
    rc_chan_destroy(&c);
}

RT(batch_id_types_zero_one_two_and_other) {
    NEED_WIN();
    CHAN_FN(NtDCompositionGetBatchId);
    struct rc_chan c; REQUIRE_CHAN(c);
    static const int types[] = { 0, 1, 2, 3, 7, -1, 0x7FFFFFFF };
    size_t i;
    for (i = 0; i < sizeof types / sizeof types[0]; i++) {
        UINT v = 0xDEADBEEFu;
        /* type selects +0x178 / +0x17c / +0x180; every other value leaves the local at 0 and still succeeds. */
        CHECK_ST(f_NtDCompositionGetBatchId(c.id, types[i], &v), 0);
        if (types[i] > 2 || types[i] < 0) CHECKF(v == 0, "type %d -> %u", types[i], v);
        else t_note("batch id type %d = %u", types[i], v);
    }
    rc_chan_destroy(&c);
}

RT(batch_id_unknown_channel_denied) {
    NEED_WIN();
    CHAN_FN(NtDCompositionGetBatchId);
    struct rc_chan c; REQUIRE_CHAN(c);
    UINT v = 0xDEADBEEFu;
    CHECK_ST(f_NtDCompositionGetBatchId(rc_bogus_id(c.id), 0, &v), S_DENIED);
    CHECK(v == 0xDEADBEEFu);
    CHECK_ST(f_NtDCompositionGetBatchId(0xFFFFFFFFu, 0, &v), S_DENIED);
    rc_chan_destroy(&c);
}

RT(wait_for_channel_idle_returns_success) {
    NEED_WIN();
    CHAN_FN(NtDCompositionWaitForChannel);
    struct rc_chan c; REQUIRE_CHAN(c);
    int a, b;
    /* pending-batch counter (+0xe4) is 0 on a channel that never submitted: the wait is skipped, status 0. */
    for (a = 0; a < 2; a++) for (b = 0; b < 2; b++) CHECK_ST(f_NtDCompositionWaitForChannel(c.id, a, b), 0);
    CHECK_ST(f_NtDCompositionWaitForChannel(rc_bogus_id(c.id), 0, 0), S_DENIED);
    rc_chan_destroy(&c);
}

RT(suspend_animations_toggle) {
    NEED_WIN();
    CHAN_FN(NtDCompositionSuspendAnimations);
    struct rc_chan c; REQUIRE_CHAN(c);
    CHECK_ST(f_NtDCompositionSuspendAnimations(c.id, 1), 0);
    CHECK_ST(f_NtDCompositionSuspendAnimations(c.id, 1), 0);   /* idempotent */
    CHECK_ST(f_NtDCompositionSuspendAnimations(c.id, 0), 0);
    CHECK_ST(f_NtDCompositionSuspendAnimations(c.id, 0x1234), 0);   /* any non-zero means "suspend" */
    CHECK_ST(f_NtDCompositionSuspendAnimations(c.id, 0), 0);
    CHECK_ST(f_NtDCompositionSuspendAnimations(rc_bogus_id(c.id), 1), S_DENIED);
    rc_chan_destroy(&c);
}

RT(debug_counter_toggle) {
    NEED_WIN();
    CHAN_FN(NtDCompositionSetDebugCounter);
    struct rc_chan c; REQUIRE_CHAN(c);
    /* state machine on channel+0x250: 0<->1 with on/off, 2<->3 likewise; every call returns 0 when the channel exists */
    CHECK_ST(f_NtDCompositionSetDebugCounter(c.id, 1), 0);
    CHECK_ST(f_NtDCompositionSetDebugCounter(c.id, 1), 0);
    CHECK_ST(f_NtDCompositionSetDebugCounter(c.id, 0), 0);
    CHECK_ST(f_NtDCompositionSetDebugCounter(c.id, 0), 0);
    CHECK_ST(f_NtDCompositionSetDebugCounter(rc_bogus_id(c.id), 1), S_DENIED);
    rc_chan_destroy(&c);
}

RT(connection_id_slot_state_machine) {
    NEED_WIN();
    CHAN_FN(NtDCompositionSetChannelConnectionId);
    struct rc_chan c; REQUIRE_CHAN(c);
    /* slot = channel + 0x298 (+8 for the second one). A non-zero id may only be stored into an empty slot;
     * storing 0 always works; a second non-zero id into an occupied slot is 0xC0000022. */
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(c.id, 0, 0x1234), 0);
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(c.id, 0, 0x5678), S_DENIED);
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(c.id, 1, 0x9ABC), 0);          /* independent second slot */
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(c.id, 7, 0xDEF0), S_DENIED);   /* any non-zero selector = second slot */
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(c.id, 0, 0), 0);               /* clear */
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(c.id, 1, 0), 0);
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(c.id, 0, 0x5678), 0);          /* empty again */
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(c.id, 0, 0), 0);
    CHECK_ST(f_NtDCompositionSetChannelConnectionId(rc_bogus_id(c.id), 0, 0), S_DENIED);
    rc_chan_destroy(&c);
}

RT(commit_completion_event_arguments) {
    NEED_WIN();
    CHAN_FN(NtDCompositionSetChannelCommitCompletionEvent);
    struct rc_chan c; REQUIRE_CHAN(c);
    /* a NULL event is rejected before the channel is looked up, so even a dead id gives INVALID_PARAMETER */
    CHECK_ST(f_NtDCompositionSetChannelCommitCompletionEvent(c.id, NULL, 0), S_INVPARAM);
    CHECK_ST(f_NtDCompositionSetChannelCommitCompletionEvent(rc_bogus_id(c.id), NULL, 1), S_INVPARAM);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    REQUIRE(ev);
    CHECK_ST(f_NtDCompositionSetChannelCommitCompletionEvent(rc_bogus_id(c.id), ev, 0), S_DENIED);
    NTSTATUS st = f_NtDCompositionSetChannelCommitCompletionEvent(c.id, (HANDLE)(ULONG_PTR)0x7FFC, 0);
    CHECK(NT_FAIL(st));                                   /* SetCommitCompletionEvent resolves the handle as an event object */
    st = f_NtDCompositionSetChannelCommitCompletionEvent(c.id, ev, 0);
    t_note("register commit-completion event on a live channel -> 0x%08lx", (unsigned long)st);
    CHECK(st == 0);
    if (st == 0) {   /* SetCommitCompletionEvent: slot channel+0x168 already used -> 0xC0000022 */
        CHECK_ST(f_NtDCompositionSetChannelCommitCompletionEvent(c.id, ev, 0), S_DENIED);
    }
    rc_chan_destroy(&c);
    CloseHandle(ev);
}

RT(batch_buffer_bad_channel_still_zeroes_outputs) {
    NEED_WIN();
    CHAN_FN(NtDCompositionProcessChannelBatchBuffer);
    struct rc_chan c; REQUIRE_CHAN(c);
    ULONG processed = 0xAAAAAAAAu; BYTE fl = 0x55;
    /* ProcessChannelBatchBuffer writes *pProcessed and *pFlag unconditionally at its end (0 / FALSE when the
     * lookup failed) and returns the lookup status. */
    CHECK_ST(f_NtDCompositionProcessChannelBatchBuffer(rc_bogus_id(c.id), 0, &processed, &fl), S_DENIED);
    CHECK(processed == 0);
    CHECK(fl == 0);
    rc_chan_destroy(&c);
}

RT(batch_buffer_size_above_section_rejected) {
    NEED_WIN();
    CHAN_FN(NtDCompositionProcessChannelBatchBuffer);
    struct rc_chan c; REQUIRE_CHAN(c);
    ULONG processed = 0xAAAAAAAAu; BYTE fl = 0x55;
    /* size > channel+0x2c8 (command buffer capacity) -> 0xC000000D, outputs still stored as 0 */
    CHECK_ST(f_NtDCompositionProcessChannelBatchBuffer(c.id, 0x7FFFFFFFu, &processed, &fl), S_INVPARAM);
    CHECK(processed == 0);
    CHECK(fl == 0);
    processed = 0xAAAAAAAAu; fl = 0x55;
    CHECK_ST(f_NtDCompositionProcessChannelBatchBuffer(c.id, 0xFFFFFFFFu, &processed, &fl), S_INVPARAM);
    CHECK(processed == 0);
    rc_chan_destroy(&c);
}

RT(batch_buffer_empty_command_stream) {
    NEED_WIN();
    CHAN_FN(NtDCompositionProcessChannelBatchBuffer);
    struct rc_chan c; REQUIRE_CHAN(c);
    ULONG processed = 0xAAAAAAAAu; BYTE fl = 0x55;
    NTSTATUS st = f_NtDCompositionProcessChannelBatchBuffer(c.id, 0, &processed, &fl);
    t_note("empty command stream -> 0x%08lx processed=%lu flag=%u", (unsigned long)st, processed, fl);
    CHECK(processed == 0);             /* nothing to iterate */
    CHECK(fl == 0 || fl == 1);
    rc_chan_destroy(&c);
}

RT(deleted_resources_argument_table) {
    NEED_WIN();
    CHAN_FN(NtDCompositionGetDeletedResources);
    struct rc_chan c; REQUIRE_CHAN(c);
    UINT64 buf[16]; UINT out = 0xAAAAAAAAu;
    /* (count - 1) must be < 8 and both pointers non-NULL, checked before the channel lookup. */
    memset(buf, 0xCC, sizeof buf);
    CHECK_ST(f_NtDCompositionGetDeletedResources(c.id, 0, buf, &out), S_INVPARAM);
    CHECK_ST(f_NtDCompositionGetDeletedResources(c.id, 9, buf, &out), S_INVPARAM);
    CHECK_ST(f_NtDCompositionGetDeletedResources(c.id, 0x100, buf, &out), S_INVPARAM);
    CHECK_ST(f_NtDCompositionGetDeletedResources(c.id, 4, NULL, &out), S_INVPARAM);
    CHECK_ST(f_NtDCompositionGetDeletedResources(c.id, 4, buf, NULL), S_INVPARAM);
    CHECK_ST(f_NtDCompositionGetDeletedResources(rc_bogus_id(c.id), 0, buf, &out), S_INVPARAM);  /* args first, channel second */
    CHECK(out == 0xAAAAAAAAu);
    CHECK(buf[0] == 0xCCCCCCCCCCCCCCCCull);
    CHECK_ST(f_NtDCompositionGetDeletedResources(rc_bogus_id(c.id), 4, buf, &out), S_DENIED);
    rc_chan_destroy(&c);
}

RT(deleted_resources_fresh_channel_is_empty) {
    NEED_WIN();
    CHAN_FN(NtDCompositionGetDeletedResources);
    struct rc_chan c; REQUIRE_CHAN(c);
    UINT64 buf[8]; UINT out = 0xAAAAAAAAu; int n;
    for (n = 1; n <= 8; n++) {
        memset(buf, 0xCC, sizeof buf); out = 0xAAAAAAAAu;
        /* nothing was ever deleted: channel+0x204 == 0, so *pOut = 0 and the zero-filled local array is copied out */
        CHECK_ST(f_NtDCompositionGetDeletedResources(c.id, n, buf, &out), 0);
        CHECK(out == 0);
        CHECK(buf[0] == 0);
        CHECK(buf[n - 1] == 0);
    }
    rc_chan_destroy(&c);
}

RT(synchronize_unknown_channel) {
    NEED_WIN();
    CHAN_FN(NtDCompositionSynchronize);
    struct rc_chan c; REQUIRE_CHAN(c);
    UINT64 v = 0x1122334455667788ull;
    CHECK_ST(f_NtDCompositionSynchronize(rc_bogus_id(c.id), &v), S_DENIED);   /* channel lookup precedes the pointer check */
    CHECK(v == 0x1122334455667788ull);
    rc_chan_destroy(&c);
}

RT(release_all_resources_fresh_channel) {
    NEED_WIN();
    CHAN_FN(NtDCompositionReleaseAllResources); CHAN_FN(NtDCompositionCommitChannel);
    struct rc_chan c; REQUIRE_CHAN(c);
    BYTE fl = 0x55;
    NTSTATUS st = f_NtDCompositionReleaseAllResources(c.id, &fl);
    t_note("ReleaseAllResources on an empty channel -> 0x%08lx flag=%u", (unsigned long)st, fl);
    CHECK(st == 0 || NT_FAIL(st));
    /* the channel itself must survive: it can still commit */
    UINT b = 0; BYTE f2 = 0;
    CHECK_ST(f_NtDCompositionCommitChannel(c.id, &b, &f2, 0, NULL), 0);
    st = f_NtDCompositionReleaseAllResources(rc_bogus_id(c.id), NULL);   /* NULL flag pointer is allowed */
    t_note("ReleaseAllResources on a dead id -> 0x%08lx", (unsigned long)st);
    rc_chan_destroy(&c);
}

RT(no_channel_process_data_status) {
    /* A process that never created a channel has no CProcessData (win32 process +0x100 == NULL): lookups return
     * 0xC0000001, not 0xC0000022. Child process so the channel table is pristine. */
    NEED_WIN();
    struct k_child_result r = k_child_run("r2_chan_pristine", "", 15000);
    CHECK(r.exit_code == 0);
}
CHILD(r2_chan_pristine) {
    int fails = 0;
    NtDCompositionCommitChannel_pfn cm = UN(NtDCompositionCommitChannel);
    NtDCompositionGetBatchId_pfn gb = UN(NtDCompositionGetBatchId);
    NtDCompositionProcessChannelBatchBuffer_pfn pb = UN(NtDCompositionProcessChannelBatchBuffer);
    if (!cm || !gb || !pb) { printf("missing exports\n"); return 0; }
    UINT b = 0, v = 0; BYTE fl = 0; ULONG pr = 5;
    NTSTATUS s1 = cm(1, &b, &fl, 0, NULL), s2 = gb(1, 0, &v), s3 = pb(1, 0, &pr, &fl);
    printf("NOTE commit=0x%08lx batchid=0x%08lx batchbuf=0x%08lx\n", (unsigned long)s1, (unsigned long)s2, (unsigned long)s3);
    /* with no process data the inline code path yields 0xC0000001; a process that the loader already gave
     * channel data (e.g. injected shim DLL using dcomp) would give 0xC0000022 -- accept both, never success. */
    if (!(s1 == S_UNSUCC || s1 == S_DENIED)) { printf("FAIL commit status\n"); fails++; }
    if (!(s2 == S_UNSUCC || s2 == S_DENIED)) { printf("FAIL batchid status\n"); fails++; }
    if (!(s3 == S_UNSUCC || s3 == S_DENIED)) { printf("FAIL batchbuf status\n"); fails++; }
    if (pr != 0) { printf("FAIL processed not zeroed\n"); fails++; }
    return fails;
}

/* ---- channels belong to the process, not to the creating thread ---- */
struct xthr { UINT id; NTSTATUS commit, batch, destroy; UINT out; };
static DWORD WINAPI xthr_use(LPVOID p) {
    struct xthr *x = (struct xthr *)p;
    NtDCompositionCommitChannel_pfn cm = UN(NtDCompositionCommitChannel);
    NtDCompositionGetBatchId_pfn gb = UN(NtDCompositionGetBatchId);
    NtDCompositionDestroyChannel_pfn de = UN(NtDCompositionDestroyChannel);
    UINT b = 0; BYTE fl = 0;
    x->commit = cm ? cm(x->id, &b, &fl, 0, NULL) : -1;
    x->batch = gb ? gb(x->id, 0, &x->out) : -1;
    x->destroy = de ? de(x->id) : -1;
    return 0;
}
RT(channel_usable_from_other_thread) {
    NEED_WIN();
    struct rc_chan c; REQUIRE_CHAN(c);
    struct xthr x; memset(&x, 0, sizeof x); x.id = c.id;
    HANDLE t = CreateThread(NULL, 0, xthr_use, &x, 0, NULL);
    REQUIRE(t);
    CHECK(WaitForSingleObject(t, 10000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CHECK_ST(x.commit, 0);            /* the table is keyed by process (win32 process +0x100), any thread may use the id */
    CHECK_ST(x.batch, 0);
    t_note("DestroyChannel from a second thread -> 0x%08lx", (unsigned long)x.destroy);
    /* the other thread destroyed it: the id is dead for everybody now */
    struct rc_chan keep = rc_chan_create();
    if (keep.ok && keep.id != c.id) {
        UINT b = 0; BYTE fl = 0;
        NtDCompositionCommitChannel_pfn cm = UN(NtDCompositionCommitChannel);
        CHECK_ST(cm(c.id, &b, &fl, 0, NULL), S_DENIED);
    }
    rc_chan_destroy(&keep);
}

RT(channel_many_create_destroy) {
    NEED_WIN();
    enum { N = 96 };
    static struct rc_chan c[N];
    int i, j, ok = 0;
    unsigned long h0 = rc_handles();
    for (i = 0; i < N; i++) { c[i] = rc_chan_create(); if (!c[i].ok) break; ok++; }
    t_note("%d of %d channels could be created in one process", ok, N);
    if (!ok) { t_skip("no channels"); return; }
    for (i = 0; i < ok; i++) for (j = i + 1; j < ok; j++) if (c[i].id == c[j].id) { CHECKF(0, "duplicate channel id 0x%x", c[i].id); i = j = ok; }
    for (i = 0; i < ok; i++) rc_chan_destroy(&c[i]);
    CHECKF(rc_handles() <= h0 + 4, "handles grew from %lu to %lu across %d channels", h0, rc_handles(), ok);
}
