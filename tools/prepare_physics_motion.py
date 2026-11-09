#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate four recovered motion/velocity readers from fixed solver batch time.
Only exact global-load expressions in these audited TESV 1.9.32 functions change.
"""
from pathlib import Path
import re
TARGETS = {'D1DA60': (1, 1), 'D369E0': (0, 1), 'D64D70': (0, 1), 'D65860': (0, 1)}
FLOAT = 'MF32((uint32_t)(0x12D18DCu))'
BITS = 'TL32((uint32_t)(0x12D18DCu))'
MARKER = '/* SKYDREAM_MOTION_CLOCK */'

def prepare(folder):
    found=set(); changes=[]
    for path in sorted(Path(folder).glob('exe_*.c')):
        text=path.read_text();new=text
        for address,counts in TARGETS.items():
            match=re.search(r'void f_'+address+r'\(CPU \*c\) \{',new)
            if not match:continue
            end=new.index('\n}\n',match.end())+3;body=new[match.start():end]
            if MARKER not in body:
                if (body.count(FLOAT),body.count(BITS))!=counts:
                    raise ValueError('unexpected motion clock loads in '+address)
                body=body.replace(FLOAT,'physics_motion_delta()').replace(BITS,'physics_motion_delta_bits()')
                body=body.replace('{','{\n    '+MARKER+'\n    extern float physics_motion_delta(void);\n    extern uint32_t physics_motion_delta_bits(void);',1)
                new=new[:match.start()]+body+new[end:]
            elif FLOAT in body or BITS in body:
                raise ValueError('partially patched motion clock '+address)
            found.add(address)
        if new!=text:changes.append((path,new))
    if found!=set(TARGETS):raise ValueError('missing motion functions: '+str(set(TARGETS)-found))
    for path,text in changes:path.write_text(text)
    return len(changes)
if __name__=='__main__':
    import sys
    print('Physics motion clocks: updated',prepare(sys.argv[1]),'generated units')
