#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Optional comparison with a user-owned 32-bit X3DAudio DLL (Wine is test-only)."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from test_x3daudio import X3DAudioTest, ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll',type=Path)
    parser.add_argument('--wine-prefix',required=True,type=Path,help='Dedicated test prefix, not the game profile')
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    X3DAudioTest.setUpClass()
    try:
        test=X3DAudioTest()
        with tempfile.TemporaryDirectory(prefix='skydream-x3d-reference-') as tmp:
            work=Path(tmp)
            shutil.copy2(args.dll,work/'X3DAudio1_7.dll')
            subprocess.run(['i686-w64-mingw32-gcc','-O2',str(ROOT/'tools/x3daudio_reference.c'),'-o',str(work/'reference.exe')],check=True)
            cases=[];native=[];command=['wine','./reference.exe','X3DAudio1_7.dll']
            for mask,channels in [(3,1),(0x3f,1),(0x3f,2)]:
                for position in [(0,0,10),(10,0,0),(-10,0,0),(0,0,-10),(0,10,0),(0,0,200)]:
                    name='case-'+str(len(cases));mem=test.fixture(mask,channels)
                    struct.pack_into('<3f',mem,796,*position)
                    (work/(name+'.bin')).write_bytes(mem.raw)
                    test.calculate(mem,0x3d);native.append(mem.raw)
                    cases.append(dict(name=name,mask=mask,channels=channels,position=position))
                    command += [name+'.bin',name+'-reference.bin',str(mask),'0x3d']
            env=dict(os.environ,WINEPREFIX=str(args.wine_prefix.resolve()),WINEDEBUG='-all',WINEDLLOVERRIDES='mscoree,mshtml=;x3daudio1_7=n')
            subprocess.run(command,cwd=work,env=env,check=True,timeout=90)
            for case,a in zip(cases,native):
                b=(work/(case['name']+'-reference.bin')).read_bytes();diffs=[]
                for off,count in [(1040,8),(1280,case['channels']*bin(case['mask']).count('1'))]:
                    x=struct.unpack_from('<'+'f'*count,a,off);y=struct.unpack_from('<'+'f'*count,b,off)
                    diffs.extend(abs(i-j) for i,j in zip(x,y))
                case['max_absolute_error']=max(diffs)
            args.output.write_text(json.dumps(cases,indent=2)+'\n')
            for mask,channels in [(3,1),(0x3f,1),(0x3f,2)]:
                error=max(c['max_absolute_error'] for c in cases if c['mask']==mask and c['channels']==channels)
                print('mask=%#x source_channels=%d max_absolute_error=%.9g'%(mask,channels,error))
    finally:
        X3DAudioTest.tearDownClass()

if __name__=='__main__':main()
