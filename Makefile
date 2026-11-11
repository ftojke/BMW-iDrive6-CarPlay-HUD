# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# CarPlay -> BMW HUD/KOMBI : host unit tests + (future) QNX cross-build.
#
# Host tests (this machine): a C++98 compiler is enough. The portable w64devkit ships one plus make:
#     ..\_toolchain\w64devkit\bin\make.exe test
# or on Linux/WSL:
#     make test
# The PowerShell wrapper run_host_tests.ps1 does the same without needing make on PATH.
#
# QNX target (phase P1): the final hook .so and the cp-kombi feeder cross-compile for QNX 6.5 armle-v7
# with arm-unknown-nto-qnx6.5.0eabi-gcc/g++ from the luka-dev/qnx65-armv7-toolchain Docker image (no
# license key), or ntoarmv7-gcc from an SDP 6.5 install. That target is a stub until the toolchain is in place.

CXX      ?= g++
CXXFLAGS ?= -std=gnu++98 -Wall -Wextra -Wno-long-long -O1
SRC       = src
TST       = tests
BUILD     = build

TESTS = test_iap2_rgi test_cp_maneuver_map test_cp_rgi_state test_ident_patch
TEST_BINS = $(addprefix $(BUILD)/,$(TESTS))

HEADERS = $(SRC)/iap2_rgi.hpp $(SRC)/kombi_defs.hpp $(SRC)/cp_maneuver_map.hpp $(SRC)/cp_rgi_state.hpp \
          hook/ident_patch.h

.PHONY: all test clean qnx

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_ident_patch: $(TST)/test_ident_patch.cpp hook/ident_patch.c hook/ident_patch.h $(TST)/test_util.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $< hook/ident_patch.c -o $@

$(BUILD)/%: $(TST)/%.cpp $(HEADERS) $(TST)/test_util.hpp $(TST)/iap2_build.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "--- $$t"; ./$$t || exit 1; done
	@echo "ALL HOST TESTS PASSED"

clean:
	rm -rf $(BUILD)

qnx:
	@echo "QNX build+run lives in WSL (Docker toolchain + QEMU QNX 6.5):"
	@echo "  wsl -d Ubuntu-24.04 -u root -- bash -c \"tr -d '\\r' < tools/qnx_test.sh | bash\""
	@false
