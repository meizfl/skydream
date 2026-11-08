#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Paths are resolved by the executable before it enters the game directory.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ ! -x "$ROOT/build/lb" ]; then
    echo "Build the Linux port first: sh $ROOT/desktop/build.sh" >&2
    exit 1
fi
exec "$ROOT/build/lb" "$@"
