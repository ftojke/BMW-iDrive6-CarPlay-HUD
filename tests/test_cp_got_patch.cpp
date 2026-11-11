// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests for the GOT redirection + fingerprint gate (hook/cp_got_patch.c).
#include "../hook/cp_got_patch.h"
#include "../tests/test_util.hpp"
#include <stdint.h>

static int test_fingerprint_match() {
    // simulate a module image: 4 words at known offsets
    uint32_t img[8] = { 0xE92D41F0, 0x11111111, 0xEBFFC022, 0x22222222,
                        0xEBFFC066, 0x33333333, 0xE92D4FF0, 0x44444444 };
    cp_fingerprint_t good[3] = {
        { 0,  0xE92D41F0 },   // link_send_message prologue
        { 8,  0xEBFFC022 },   // bl memcpy at the recv seam
        { 24, 0xE92D4FF0 },   // dispatcher prologue
    };
    CHECK_EQ(cp_verify_fingerprints(img, good, 3), 1);

    cp_fingerprint_t bad[1] = { { 8, 0xDEADBEEF } };   // a firmware whose bytes differ
    CHECK_EQ(cp_verify_fingerprints(img, bad, 1), 0);
    return 0;
}

static void fnA(void) {}
static void fnB(void) {}
static void fnC(void) {}

static int test_got_redirect() {
    void* got[5] = { (void*)&fnA, (void*)&fnB, (void*)&fnC, (void*)&fnA, 0 };
    // redirect the first fnB -> fnC
    int n = cp_redirect_got(got, sizeof(got), (void*)&fnB, (void*)&fnC);
    CHECK_EQ(n, 1);
    CHECK(got[1] == (void*)&fnC);
    // the other slots untouched
    CHECK(got[0] == (void*)&fnA);
    CHECK(got[2] == (void*)&fnC);
    // a function not present -> 0 slots changed
    CHECK_EQ(cp_redirect_got(got, sizeof(got), (void*)0x1234, (void*)&fnA), 0);
    return 0;
}

static int test_got_redirect_first_only() {
    // fnA appears twice; redirect swaps only the first occurrence (deterministic)
    void* got[3] = { (void*)&fnA, (void*)&fnB, (void*)&fnA };
    int n = cp_redirect_got(got, sizeof(got), (void*)&fnA, (void*)&fnC);
    CHECK_EQ(n, 1);
    CHECK(got[0] == (void*)&fnC);
    CHECK(got[2] == (void*)&fnA);
    return 0;
}

int main() {
    test_fingerprint_match();
    test_got_redirect();
    test_got_redirect_first_only();
    TEST_RETURN();
}
