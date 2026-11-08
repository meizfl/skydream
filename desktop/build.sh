#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# JOBS limits all compilation; LB_GEN_JOBS limits memory-heavy generated C.
set -eu
cd "$(dirname "$0")/.."
cmake -S desktop -B desktop/build -G Ninja \
    -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    -DCMAKE_BUILD_TYPE=Release -DLB_GEN_JOBS="${LB_GEN_JOBS:-2}" "$@"
cmake --build desktop/build --parallel "${JOBS:-2}"
