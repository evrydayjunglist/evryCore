"""Build the combat correction without replacing the running server executable."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def build(build_dir, output):
    build_dir, output = build_dir.resolve(), output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache = (build_dir/'CMakeCache.txt').read_text()
    source = Path(re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$', cache, re.M)[1])
    if source.resolve() != Path(__file__).resolve().parents[3]:
        raise ValueError('CMake source does not resolve to this checkout')
    ninja = re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache, re.M)[1]
    runtime = build_dir/'bin/RelWithDebInfo/worldserver.exe'
    old_sha = hashlib.sha256(runtime.read_bytes()).hexdigest()
    prefix = 'src\\server\\worldserver\\CMakeFiles\\worldserver.dir\\'
    targets = ['modules', 'scripts', 'game', 'tests'] + [prefix+p for p in
        ('cmake_pch.cxx.obj', 'Main.cpp.obj', 'CommandLine\\CliRunnable.cpp.obj',
         'RemoteAccess\\RASession.cpp.obj', 'TCSoap\\TCSoap.cpp.obj', 'worldserver.rc.res')]
    vcvars = r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    cmake = r'C:\Program Files\CMake\bin\cmake.exe'
    cmd = output/'compile.cmd'
    cmd.write_text('@echo off\ncall "'+vcvars+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+
        subprocess.list2cmdline([cmake, '--build', str(build_dir), '--target', *targets,
            '--config', 'RelWithDebInfo', '--parallel', '8'])+'\nexit /b %errorlevel%\n')
    with (output/'compile.log').open('w') as log:
        subprocess.run(['cmd', '/c', str(cmd)], stdout=log, stderr=subprocess.STDOUT, check=True)
    commands = subprocess.check_output([ninja, '-t', 'commands', 'worldserver'], cwd=build_dir, text=True)
    link = next(line for line in reversed(commands.splitlines()) if 'vs_link_exe' in line and '/out:' in line)
    for field, old, new in (
        ('out', r'bin\RelWithDebInfo\worldserver.exe', output/'worldserver.exe'),
        ('pdb', r'bin\RelWithDebInfo\worldserver.pdb', output/'worldserver.pdb'),
        ('implib', r'src\server\worldserver\worldserver.lib', output/'worldserver.lib')):
        token = '/'+field+':'+old
        if link.count(token) != 1:
            raise ValueError('Unexpected link command: '+field)
        link = link.replace(token, '/'+field+':"'+str(new)+'"')
    cmd = output/'link.cmd'
    cmd.write_text('@echo off\ncall "'+vcvars+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+link+'\nexit /b %errorlevel%\n')
    with (output/'link.log').open('w') as log:
        subprocess.run(['cmd', '/c', str(cmd)], cwd=build_dir, stdout=log, stderr=subprocess.STDOUT, check=True)
    if hashlib.sha256(runtime.read_bytes()).hexdigest() != old_sha:
        raise ValueError('Installed server changed during candidate build')
    result = dict(source=str(source), installedSha256=old_sha, files={
        name:hashlib.sha256((output/name).read_bytes()).hexdigest()
        for name in ('worldserver.exe', 'worldserver.pdb')})
    (output/'build.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    build(args.build, args.out)
