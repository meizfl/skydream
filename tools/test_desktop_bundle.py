#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bound package execution, preserving diagnostic output and timeout stacks."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    env=os.environ.copy()
    if sys.platform=='darwin':
        env['PATH']='/usr/bin:/bin'
        env.pop('DYLD_PRINT_LIBRARIES',None)
    elif sys.platform=='win32':
        system=os.environ.get('SystemRoot',r'C:\Windows')
        env['PATH']=system+'\\System32;'+system
    command=[str(Path(sys.argv[1]).resolve()),*sys.argv[2:]]
    process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        output,_=process.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        if sys.platform=='darwin':
            with tempfile.TemporaryDirectory() as td:
                report=Path(td)/'sample.txt'
                try:
                    sampled=subprocess.run(['/usr/bin/sample',str(process.pid),'1','1','-file',str(report)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
                    print(sampled.stdout.decode(errors='replace'),flush=True)
                    if report.exists():
                        print('\n'.join(report.read_text(errors='replace').splitlines()[:350]),flush=True)
                except Exception as error:
                    print(f'Cannot sample stalled process: {type(error).__name__}',flush=True)
        process.kill()
        output,_=process.communicate()
        print(output.decode(errors='replace'),flush=True)
        raise SystemExit('Packaged executable exceeded the 120-second diagnostic timeout')
    print(output.decode(errors='replace'),flush=True)
    if process.returncode:
        raise SystemExit(process.returncode)

if __name__ == '__main__':
    main()
