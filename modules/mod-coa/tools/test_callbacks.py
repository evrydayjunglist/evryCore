"""Compile and execute the actual module callbacks with an isolated native host."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',required=True,type=Path)
args=parser.parse_args()
args.out.mkdir(parents=True,exist_ok=True)
host=args.out/'include'
host.mkdir(exist_ok=True)
shutil.copyfile(ROOT/'tools/fixture/host.h',host/'host.h')
for name in ['Chat','Player','Random','ScriptMgr','Spell','SpellAuraEffects','SpellAuras','SpellInfo','SpellScript']:
    (host/(name+'.h')).write_text('#include "host.h"\n')
command=args.out/'callbacks.cmd'
command.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
    +'cl /nologo /std:c++20 /EHsc /W4 /I"'+str(host)+'" /I"'+str(ROOT/'src')+'" "'+str(ROOT/'tools/fixture/callbacks.cpp')+'" /Fe:"'+str(args.out/'callbacks.exe')+'" /Fo:"'+str(args.out/'callbacks.obj')+'"\n'
    +'if errorlevel 1 exit /b %errorlevel%\n"'+str(args.out/'callbacks.exe')+'"\nexit /b %errorlevel%\n')
raise SystemExit(subprocess.call(['cmd','/c',str(command)],cwd=args.out))
