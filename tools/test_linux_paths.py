# SPDX-License-Identifier: GPL-3.0-only
"""Direct executable launches must locate game files independently of cwd."""
import os
import pathlib
import subprocess
import tempfile
import unittest


class LinuxPathsTest(unittest.TestCase):
    def test_direct_launch_and_explicit_paths(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'runtime/main.c').read_text()
        start = source.index('static void linux_paths(')
        function = source[start:source.index('\n#endif', start)]
        code = '''#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
char g_game_dir[1024], g_save_dir[1024];
void rt_fatal(const char *fmt, ...) {
    va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); exit(1);
}
''' + function + '''
int main(int argc, char **argv) {
    int wayland = argc > 1 && !strcmp(argv[1], "--wayland");
    int x11 = argc > 1 && !strcmp(argv[1], "--x11");
    linux_paths(argc, argv);
    if (wayland) assert(!strcmp(getenv("SDL_VIDEODRIVER"), "wayland"));
    if (x11) assert(!strcmp(getenv("SDL_VIDEODRIVER"), "x11"));
    printf("%s\\n%s\\n", g_game_dir, g_save_dir);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            base = pathlib.Path(tmp)
            game = base / 'Game with spaces'
            game.mkdir()
            exe = game / 'Skyrim'
            c = base / 'paths.c'
            c.write_text(code)
            subprocess.run(['cc', str(c), '-o', str(exe)], check=True)
            env = dict(os.environ, XDG_DATA_HOME=str(base / 'data'))
            result = subprocess.run([str(exe)], cwd='/', env=env, text=True, capture_output=True, check=True)
            self.assertEqual(result.stdout.splitlines(), [str(game), str(base / 'data/skyrim-native/save')])
            explicit = base / 'Other Game'
            explicit.mkdir()
            result = subprocess.run([str(exe), 'Other Game', './custom-save'], cwd=base,
                                    env=env, text=True, capture_output=True, check=True)
            self.assertEqual(result.stdout.splitlines(), [str(explicit), str(base / 'custom-save')])
            for flag in ('--wayland', '--x11'):
                backend_env = dict(env, SDL_VIDEODRIVER='invalid-test-backend')
                result = subprocess.run([str(exe), flag], cwd='/', env=backend_env,
                                        text=True, capture_output=True, check=True)
                self.assertEqual(result.stdout.splitlines()[0], str(game))
                result = subprocess.run([str(exe), flag, 'Other Game', './custom-save'], cwd=base,
                                        env=backend_env, text=True, capture_output=True, check=True)
                self.assertEqual(result.stdout.splitlines(), [str(explicit), str(base / 'custom-save')])
            linked = base / 'Launch Skyrim'
            linked.symlink_to(exe)
            result = subprocess.run([str(linked)], cwd='/', env=env, text=True, capture_output=True, check=True)
            self.assertEqual(result.stdout.splitlines()[0], str(game))
            result = subprocess.run([str(exe), '--help'], text=True, capture_output=True, check=True)
            self.assertIn('[GAME_DIR [SAVE_DIR]]', result.stdout)
