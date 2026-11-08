# SPDX-License-Identifier: GPL-3.0-only
"""Capture must distinguish gameplay HUD from menus layered above it."""
import pathlib
import subprocess
import tempfile
import unittest


class MouseCaptureTest(unittest.TestCase):
    def test_gameplay_and_menu_stack(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'runtime/platform_sdl.c').read_text()
        start = source.index('static int desktop_gameplay_mouse(void)')
        function = source[start:source.index('\n#endif', start)]
        code = '''#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
uint8_t *memory;
#define M32(a) (*(uint32_t *)(memory + (uint32_t)(a)))
''' + function + '''
int main(void) {
    memory = calloc(1, 0x01300000);
    assert(!desktop_gameplay_mouse()); // startup/main menu
    M32(0x012E3548) = 0x1000;
    M32(0x1094) = 0x2000;
    M32(0x109c) = 1;
    M32(0x2000) = 0x3000;
    M32(0x3000) = 0x010E537C; // HUD
    M32(0x3010) = 0x18902;
    assert(desktop_gameplay_mouse());
    M32(0x109c) = 2;
    M32(0x2004) = 0x4000;
    M32(0x4000) = 0x010E4BD4; // passive CursorMenu
    M32(0x4010) = 2;
    assert(desktop_gameplay_mouse());
    M32(0x4010) = 0x581; // main menu / paused screen
    assert(!desktop_gameplay_mouse());
    M32(0x4010) = 0x4400; // dialogue/inventory input context
    assert(!desktop_gameplay_mouse());
    M32(0x4010) = 0x807; // console
    assert(!desktop_gameplay_mouse());
    M32(0x109c) = 1;
    assert(desktop_gameplay_mouse()); // close menu -> camera
    M32(0x109c) = 0;
    assert(!desktop_gameplay_mouse());
    free(memory);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'capture.c'
            path.write_text(code)
            exe = pathlib.Path(tmp) / 'capture'
            subprocess.run(['cc', str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
