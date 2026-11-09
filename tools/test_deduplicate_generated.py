# SPDX-License-Identifier: GPL-3.0-only
"""Conservative function sharing preserves guest entry addresses and ABI."""
import importlib.util
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('dedup',ROOT/'tools/deduplicate_generated.py');dedup=importlib.util.module_from_spec(spec);spec.loader.exec_module(dedup)
def function(name,offset=4,label='AAA',extra=''):
    return f'void f_{name}(CPU *c) {{\n{extra}L_{label}:\n    c->r[0]=c->r[1]+{offset};\n    c->r[4]+=4;\n}}\n'
class DedupTests(unittest.TestCase):
    def test_preserve_differences_and_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'exe_00.c'
            p.write_text(function('401000')+function('401010',label='BBB')+function('401020',offset=8)+function('401030',extra='    static int state; ++state;\n')+function('401040',extra='    static int state; ++state;\n'))
            before=p.read_text();r=dedup.audit(tmp);self.assertEqual(r['new_duplicate_bodies'],1);self.assertEqual(p.read_text(),before)
            dedup.audit(tmp,True);text=p.read_text();self.assertIn('SKYDREAM_DEDUP: f_401000',text);self.assertIn('f_401020(CPU',text);self.assertIn('__attribute__((noinline))',text)
            second=dedup.audit(tmp,True);self.assertEqual(second['new_duplicate_bodies'],0);self.assertEqual(second['existing_aliases'],1);self.assertEqual(p.read_text(),text)
    def test_literals_and_protected_entry_are_not_folded(self):
        a=function('401000',extra='    const char *s="L_AAA"; (void)s;\n')
        b=function('401010',label='BBB',extra='    const char *s="L_BBB"; (void)s;\n')
        self.assertNotEqual(dedup.normalize(a),dedup.normalize(b))
        self.assertNotEqual(dedup.normalize(function("401000",extra="    c->r[1]=L_AAA;\n")),dedup.normalize(function("401010",label="BBB",extra="    c->r[1]=L_BBB;\n")))
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp)/'exe_00.c').write_text(function('401000')+function('8F9720',label='BBB'))
            self.assertEqual(dedup.audit(tmp)['new_duplicate_bodies'],0)
    def test_cross_file_calls_and_entry_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);header='#include "x86.h"\nvoid f_401000(CPU *);\n'
            (base/'exe_00.c').write_text(header+function('401000'))
            (base/'exe_01.c').write_text(header+function('401010',label='BBB'))
            dedup.audit(base,True)
            harness=base/'test.c';harness.write_text('#include "x86.h"\n#include <assert.h>\nvoid f_401000(CPU *);void f_401010(CPU *);\nint main(void){CPU a={0},b={0};a.r[1]=b.r[1]=19;a.r[4]=b.r[4]=100;f_401000(&a);f_401010(&b);assert(a.r[0]==23&&a.r[4]==104);assert(!memcmp(&a,&b,sizeof a));assert(f_401000!=f_401010);return 0;}\n')
            exe=base/'test';subprocess.run(['cc','-O2','-I',str(ROOT/'runtime'),str(base/'exe_00.c'),str(base/'exe_01.c'),str(harness),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
