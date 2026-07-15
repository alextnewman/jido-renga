# SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
# SPDX-License-Identifier: MIT
# SPDX-FileContributor: Generated with GitHub Copilot

# Host build for the Jidō Renga "pure core" unit tests. These exercise the
# kernel-independent logic off-target -- no Haiku toolchain required.
#
# Covered:
#   * sdhci_embedded  (matcher, command policy, convergence, ADMA2, CSD,
#                      transactions, personalities)
#   * iosf_mbi        (mailbox protocol)
#   * i2c_atmel_mxt   (object-table parser, CRC characterization, touch engine)
#                     -- compiled against the minimal kernel-header shims in
#                     shims/ so pure driver logic can run on the host.
#
#   make        build and run
#   make build  compile only
#   make clean

CXX ?= c++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wshadow -O1 -g -pthread \
	-I. -I../overlay/busses/mmc/sdhci_embedded \
	-I../overlay/bus_managers/iosf_mbi \
	-Ishims -I../overlay/drivers/input/i2c_atmel_mxt

DRIVER_DIR := ../overlay/busses/mmc/sdhci_embedded
MXT_DIR := ../overlay/drivers/input/i2c_atmel_mxt

# Pure-core sources with out-of-line definitions.
CORE_SRC := \
	$(DRIVER_DIR)/Matcher.cpp \
	$(DRIVER_DIR)/Personality.cpp \
	$(MXT_DIR)/ObjectTable.cpp \
	$(MXT_DIR)/TouchEngine.cpp

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
	sdhci_embedded/test_bus_mode.cpp \
	sdhci_embedded/test_staged_request.cpp \
	iosf_mbi/test_iosf_mbi_protocol.cpp \
	i2c_atmel_mxt/test_object_table.cpp \
	i2c_atmel_mxt/test_touch_engine.cpp \
	sdhci_embedded/test_main.cpp

BIN := jr_tests

run: build
	./$(BIN)

build:
	$(CXX) $(CXXFLAGS) $(CORE_SRC) $(TEST_SRC) -o $(BIN)

clean:
	rm -rf $(BIN) $(BIN).dSYM jr_tests_san jr_tests_san.dSYM *.dSYM

.PHONY: run build clean
