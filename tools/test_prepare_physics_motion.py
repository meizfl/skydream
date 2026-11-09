# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import tempfile
import unittest
from prepare_physics_motion import prepare, TARGETS, FLOAT, BITS, MARKER

class PhysicsMotionPreparation(unittest.TestCase):
    def source(self):
        return '\n'.join('void f_'+address+'(CPU *c) {\n'+
                         '\n'.join(['    fpush(c, '+FLOAT+');']*counts[0]+['    eax = '+BITS+';']*counts[1])+\
                         '\n}\n' for address,counts in TARGETS.items())
    def test_scoped_and_idempotent(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'exe_00.c'
            untouched='void f_D12BE0(CPU *c) {\n    eax = '+BITS+';\n}\n'
            path.write_text(self.source()+untouched)
            self.assertEqual(prepare(folder),1)
            first=path.read_text();self.assertEqual(first.count(MARKER),4)
            self.assertTrue(first.endswith(untouched));self.assertEqual(first.count(BITS),1)
            self.assertEqual(prepare(folder),0);self.assertEqual(path.read_text(),first)
    def test_unexpected_layout_aborts_before_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'exe_00.c'
            source=self.source().replace(BITS,'eax',1);path.write_text(source)
            with self.assertRaises(ValueError):prepare(folder)
            self.assertEqual(path.read_text(),source)
if __name__=='__main__':unittest.main()
