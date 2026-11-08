# SPDX-License-Identifier: GPL-3.0-only
"""INI keys inserted after other sections must survive save/reload."""
import pathlib
import subprocess
import tempfile
import unittest


class SkyrimIniTest(unittest.TestCase):
    def test_inserted_keys_keep_their_sections(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        src = (root / 'runtime/skyrim_shims.c').read_text()
        types = src[src.index('typedef struct { char *sec'):src.index('static IniFile *g_inis;')]
        parse = src[src.index('static char *trim'):src.index('static IniFile *ini_get')]
        save = src[src.index('static void ini_save'):src.index('/* (app, key, value, file)')]
        setter = src[src.index('static void ini_set'):src.index('void skyrim_seed_prefs')]
        code = '''#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <assert.h>
''' + types + parse + save + setter + '''
int main(int argc, char **argv) {
    IniFile f = {0};
    snprintf(f.path, sizeof f.path, "%s/prefs.ini", argv[1]);
    ini_set(&f, "Display", "quality", "1");
    ini_set(&f, "Decals", "enabled", "0");
    ini_set(&f, "Display", "iSize W", "1280");
    ini_set(&f, "Display", "iSize H", "720");
    ini_set(&f, "MAIN", "bGamepadEnable", "0");
    ini_save(&f);
    ini_parse(&f);
    int width = 0, height = 0, pad = 0;
    for (int i = 0; i < f.n; i++) {
        if (!f.e[i].key) continue;
        if (!strcmp(f.e[i].key, "iSize W")) {
            assert(!strcmp(f.e[i].sec, "Display"));
            assert(!strcmp(f.e[i].val, "1280")); width++;
        }
        if (!strcmp(f.e[i].key, "iSize H")) {
            assert(!strcmp(f.e[i].sec, "Display")); height++;
        }
        if (!strcmp(f.e[i].key, "bGamepadEnable")) {
            assert(!strcmp(f.e[i].sec, "MAIN")); pad++;
        }
    }
    assert(width == 1 && height == 1 && pad == 1);
    ini_free(&f);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c = pathlib.Path(tmp) / 'test.c'
            c.write_text(code)
            exe = pathlib.Path(tmp) / 'test'
            subprocess.run(['cc', str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe), tmp], check=True)
