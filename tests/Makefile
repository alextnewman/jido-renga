# SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
# SPDX-License-Identifier: MIT
# SPDX-FileContributor: Generated with GitHub Copilot

# Host build for the sdhci_embedded "pure core" unit tests. These exercise the
# kernel-independent logic (matcher, command policy, convergence, ADMA2, CSD,
# transactions, personalities) off-target -- no Haiku toolchain required.
#
#   make        build and run
#   make build  compile only
#   make clean

CXX ?= c++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wshadow -O1 -g -pthread \
	-I. -I../overlay/busses/mmc/sdhci_embedded \
	-I../overlay/bus_managers/iosf_mbi

DRIVER_DIR := ../overlay/busses/mmc/sdhci_embedded

# Pure-core sources with out-of-line definitions.
CORE_SRC := \
	$(DRIVER_DIR)/Matcher.cpp \
	$(DRIVER_DIR)/Personality.cpp

TEST_SRC := \
	framework/test_worker_rig.cpp \
	sdhci_embedded/test_command.cpp \
	sdhci_embedded/test_convergence.cpp \
	sdhci_embedded/test_contract.cpp \
	sdhci_embedded/test_adma2.cpp \
	sdhci_embedded/test_matcher.cpp \
	sdhci_embedded/test_personality.cpp \
	sdhci_embedded/test_transaction.cpp \
	sdhci_embedded/test_concurrency.cpp \
	sdhci_embedded/test_csd.cpp \
	iosf_mbi/test_iosf_mbi_protocol.cpp \
	sdhci_embedded/test_main.cpp

BIN := jr_tests

run: build
	./$(BIN)

build:
	$(CXX) $(CXXFLAGS) $(CORE_SRC) $(TEST_SRC) -o $(BIN)

clean:
	rm -rf $(BIN) $(BIN).dSYM jr_tests_san jr_tests_san.dSYM *.dSYM

.PHONY: run build clean
