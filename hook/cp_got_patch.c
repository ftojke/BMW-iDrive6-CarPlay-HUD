/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * CarPlay -> BMW HUD/KOMBI: GOT redirection + fingerprint gate. Portable C (host tests + QNX).
 */
#include "cp_got_patch.h"
#include <string.h>

#if defined(__QNX__) || defined(__unix__) || defined(__APPLE__)
#  include <sys/mman.h>
#  include <unistd.h>
#  define CP_HAVE_MPROTECT 1
#endif

int cp_verify_fingerprints(const void* base, const cp_fingerprint_t* fps, size_t n) {
    const uint8_t* b = (const uint8_t*)base;
    size_t i;
    for (i = 0; i < n; ++i) {
        uint32_t w;
        memcpy(&w, b + fps[i].offset, 4);        /* little-endian host == armle target */
        if (w != fps[i].word) return 0;
    }
    return 1;
}

static int make_writable(void* addr) {
#ifdef CP_HAVE_MPROTECT
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t a = (uintptr_t)addr & ~((uintptr_t)ps - 1);
    return mprotect((void*)a, (size_t)ps, PROT_READ | PROT_WRITE) == 0 ? 1 : 0;
#else
    (void)addr;
    return 1;    /* host test build: the GOT array is ordinary writable memory */
#endif
}

int cp_redirect_got(void* got, size_t got_bytes, const void* old_fn, const void* new_fn) {
    void** slots = (void**)got;
    size_t count = got_bytes / sizeof(void*), i;
    for (i = 0; i < count; ++i) {
        if (slots[i] == old_fn) {
            if (!make_writable(&slots[i])) return 0;
            slots[i] = (void*)new_fn;
            return 1;
        }
    }
    return 0;
}

int cp_redirect_slot(void** slot, const void* new_fn, void** old_out) {
    if (!slot) return 0;
    if (old_out) *old_out = *slot;
    if (!make_writable((void*)slot)) return 0;
    *slot = (void*)new_fn;
    return 1;
}
