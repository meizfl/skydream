# SPDX-License-Identifier: GPL-3.0-only
"""Regression for optimized guest threads waiting on shared memory flags."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SharedMemoryTest(unittest.TestCase):
    def test_optimized_wait_observes_other_thread(self):
        compiler = shutil.which('clang') or shutil.which('cc')
        if not compiler:
            self.skipTest('C compiler not available')
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / 'shared_flag.c'
            binary = tmp / 'shared_flag'
            src.write_text(r'''
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "lift_prelude.h"
uint8_t *MEM;
static void *writer(void *unused) {
    (void)unused;
    usleep(20000);
    TS32(0, 1);
    return NULL;
}
int main(void) {
    MEM = calloc(1, 16);
    if (!MEM) return 2;
    pthread_t thread;
    if (pthread_create(&thread, NULL, writer, NULL)) return 3;
    while (!TL32(0)) {}
    pthread_join(thread, NULL);
    free(MEM);
    return 0;
}
''')
            runtime = Path(__file__).resolve().parent.parent / 'runtime'
            subprocess.run([compiler, '-std=gnu11', '-O2', '-I' + str(runtime),
                            str(src), '-pthread', '-o', str(binary)],
                           capture_output=True, check=True, timeout=30)
            subprocess.run([str(binary)], capture_output=True, check=True, timeout=3)


if __name__ == '__main__':
    unittest.main()
