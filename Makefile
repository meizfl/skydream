# SPDX-License-Identifier: GPL-3.0-only
CMAKE ?= cmake
BUILD_DIR ?= build/native
JOBS ?= 2
.PHONY: all build runtime test test-source
all: build
build:
	$(CMAKE) -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DLB_BUILD_GAME=ON
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)
runtime:
	$(CMAKE) -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DLB_BUILD_GAME=OFF
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)
test: runtime
	ctest --test-dir $(BUILD_DIR)/desktop --output-on-failure
test-source:
	python3 -m unittest discover -s tools -p 'test_*.py'
