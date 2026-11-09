#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run a packaged executable with a bounded timeout and captured loader trace."""
import os
from pathlib import Path
import subprocess
import sys

def main():
    env=os.environ.copy()
    if sys.platform=='darwin':
        env['PATH']='/usr/bin:/bin'
        env['DYLD_PRINT_LIBRARIES']='1'
    elif sys.platform=='win32':
        env['PATH']=env['SystemRoot']+'\\System32;'+env['SystemRoot']
    command=[str(Path(sys.argv[1]).resolve()),*sys.argv[2:]]
    try:
        result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
        output=result.stdout.decode(errors='replace')
        print(output,flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)
        if sys.platform=='darwin' and any(line.startswith('dyld') and ('/opt/homebrew/' in line or '/usr/local/Cellar/' in line or '/usr/local/opt/' in line) for line in output.splitlines()):
            raise SystemExit('Homebrew library was loaded outside the package')
    except subprocess.TimeoutExpired as exc:
        print((exc.stdout or b'').decode(errors='replace'),flush=True)
        raise SystemExit('Packaged executable exceeded the 120-second diagnostic timeout')

if __name__ == "__main__":
    main()
