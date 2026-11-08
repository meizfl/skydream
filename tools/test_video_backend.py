# SPDX-License-Identifier: GPL-3.0-only
"""Session selection must preserve explicit backend overrides."""
import pathlib
import subprocess
import tempfile
import unittest


class VideoBackendTest(unittest.TestCase):
    def test_session_and_override(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'runtime/platform_sdl.c').read_text()
        start = source.index('static void select_video_driver(void)')
        function = source[start:source.index('\n#endif', start)]
        code = '''#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <assert.h>
''' + function + '''
int main(void) {
    unsetenv("SDL_VIDEODRIVER"); unsetenv("WAYLAND_DISPLAY"); unsetenv("XDG_SESSION_TYPE");
    select_video_driver();
    assert(!strcmp(getenv("SDL_VIDEODRIVER"), "x11,wayland"));
    unsetenv("SDL_VIDEODRIVER"); setenv("XDG_SESSION_TYPE", "wayland", 1);
    select_video_driver();
    assert(!strcmp(getenv("SDL_VIDEODRIVER"), "wayland,x11"));
    unsetenv("SDL_VIDEODRIVER"); setenv("XDG_SESSION_TYPE", "x11", 1);
    setenv("WAYLAND_DISPLAY", "wayland-1", 1);
    select_video_driver();
    assert(!strcmp(getenv("SDL_VIDEODRIVER"), "wayland,x11"));
    setenv("SDL_VIDEODRIVER", "x11", 1);
    select_video_driver();
    assert(!strcmp(getenv("SDL_VIDEODRIVER"), "x11"));
    setenv("SDL_VIDEODRIVER", "", 1); setenv("WAYLAND_DISPLAY", "", 1);
    select_video_driver();
    assert(!strcmp(getenv("SDL_VIDEODRIVER"), "x11,wayland"));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'backend.c'
            path.write_text(code)
            exe = pathlib.Path(tmp) / 'backend'
            subprocess.run(['cc', str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
