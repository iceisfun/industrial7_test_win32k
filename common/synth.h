/* Synthetic pointer devices.
 *
 * A guest without touch or pen hardware has no pointer device. CreateSyntheticPointerDevice makes a real one in the
 * kernel (it shows up in GetPointerDevices and, for touch and pen, as a HID device in the raw input list), so the
 * tests that need "a device" create one for the duration of the test and destroy it afterwards.
 *
 * Observed on Windows 10 22H2: the device appears in the pointer device list at once; after
 * DestroySyntheticPointerDevice it can stay listed for a short while (queries on it then fail with 0x3bc3), so
 * synth_end() waits until the count is back to what it was before. Never print the device name: it carries a counter. */
#ifndef SYNTH_H
#define SYNTH_H
#include "t.h"

#ifndef PT_TOUCH
#define PT_TOUCH 2
#define PT_PEN 3
#endif

struct synth { HANDLE h; UINT before; };

static inline UINT synth_count(void) {
    UINT n = 0;
    if (!U(GetPointerDevices) || !U(GetPointerDevices)(&n, NULL)) return 0;
    return n;
}
/* Waits (up to ~5 s) for the listed count to equal want; returns 1 when it does. */
static inline int synth_wait_count(UINT want) {
    for (int i = 0; i < 100; i++) {
        if (synth_count() == want) return 1;
        Sleep(50);
    }
    return synth_count() == want;
}
/* Creates a synthetic device of the given type (PT_TOUCH or PT_PEN). Returns 1 on success. */
static inline int synth_begin(struct synth *s, int type, ULONG maxContacts) {
    s->h = NULL; s->before = synth_count();
    if (!U(CreateSyntheticPointerDevice) || !U(DestroySyntheticPointerDevice)) return 0;
    U(GetDesktopWindow)();
    s->h = U(CreateSyntheticPointerDevice)(type, maxContacts, POINTER_FEEDBACK_DEFAULT);
    if (!s->h) return 0;
    if (!synth_wait_count(s->before + 1)) { U(DestroySyntheticPointerDevice)(s->h); s->h = NULL; return 0; }
    return 1;
}
static inline void synth_end(struct synth *s) {
    if (!s->h) return;
    U(DestroySyntheticPointerDevice)(s->h);
    s->h = NULL;
    synth_wait_count(s->before);
}
/* Declares a device that is destroyed again when the enclosing scope ends (also on early returns). Declare the
 * devices in creation order: the scope ends them in reverse. */
#define SYNTH_AUTO(sy) struct synth sy __attribute__((cleanup(synth_end))) = { NULL, 0 }
/* Makes sure the test has at least one pointer device: creates a synthetic touch device when the system lists none.
 * Skips (with a plain reason) only when even that is impossible. n receives the resulting count. */
#define SYNTH_ENSURE_OR_SKIP(sy, n) do { \
    (n) = synth_count(); \
    if ((n) == 0) { \
        if (!synth_begin(&(sy), PT_TOUCH, 5)) { t_skip("no pointer device and a synthetic one cannot be created"); return; } \
        (n) = synth_count(); \
    } } while (0)
/* Same, but guarantees at least two devices (a short buffer of n-1 >= 1 entries needs them): adds a pen device. */
#define SYNTH_ENSURE2_OR_SKIP(sy, sy2, n) do { \
    SYNTH_ENSURE_OR_SKIP(sy, n); \
    if ((n) < 2) { \
        if (!synth_begin(&(sy2), PT_PEN, 1)) { t_skip("fewer than two pointer devices and a second synthetic one cannot be created"); return; } \
        (n) = synth_count(); \
    } } while (0)
#endif
