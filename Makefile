# SPDX-License-Identifier: GPL-3.0-only
.PHONY: all build runtime test
all: build
build:
	sh desktop/build.sh
runtime:
	sh desktop/build.sh -DLB_BUILD_GAME=OFF
test:
	python3 -m unittest discover -s tools -p 'test_*.py'
