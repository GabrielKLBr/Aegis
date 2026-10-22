"""Compile Windows branches with native Linux LLVM; this never runs Windows.

Preparation only. MSVC, WHP boots, CTest and RtlVirtualUnwind still require a
native Windows acceptance run. _MSC_FULL_VER=0 is only a report placeholder.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT=pathlib.Path(__file__).resolve().parents[1]
SOURCES=['guest/Graphics.cpp','src/microvm/gui/Backend.cpp','src/microvm/gui/Reference.cpp','src/microvm/gui/Viewer.cpp','src/microvm/gui/Main.cpp','test/GraphicsTests.cpp','test/GuestGraphicsBootTests.cpp','src/microvm/boot/BootMain.cpp','src/microvm/boot/KernelBoot.cpp','src/microvm/boot/SessionSnapshot.cpp','src/microvm/state/VmMain.cpp','src/microvm/state/VmManager.cpp','src/microvm/net/Policy.cpp','src/microvm/net/Backend.cpp','src/microvm/net/Lab.cpp','src/microvm/net/Proxy.cpp','guest/NetworkStack.cpp','test/NetworkTests.cpp','test/GuestNetworkBootTests.cpp']

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=pathlib.Path,required=True);args=parser.parse_args()
    if sys.platform!='linux':parser.error('This preparation tool uses native Linux LLVM')
    compiler=sorted((pathlib.Path.home()/'.cache/guardian-native-deps').glob('llvm-mingw-*/bin/x86_64-w64-mingw32-clang++'))[-1]
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True);headers=output/'case-alias';headers.mkdir(exist_ok=True);alias=headers/'Windows.h'
    if alias.is_symlink():alias.unlink()
    alias.write_text('#include <windows.h>\n')
    report={'kind':'cross_compilation_precheck_only','host_compiler_format':'ELF','target_format':'PE-COFF objects','native_windows_validation':'not_run','sources':[]}
    try:
        for i,source in enumerate(SOURCES):
            command=[str(compiler),'-std=c++20','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fms-extensions','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-D_MSC_FULL_VER=0','-DGUARDIAN_BOOT_CONFIGURATION="cross-precheck"','-I',str(headers),'-I',str(ROOT/'shared'),'-c',str(ROOT/source),'-o',str(output/(str(i)+'.obj'))]
            p=subprocess.run(command,capture_output=True,text=True);(output/(str(i)+'.log')).write_text(json.dumps(command)+'\n'+p.stdout+p.stderr)
            report['sources'].append({'path':source,'sha256':hashlib.sha256((ROOT/source).read_bytes()).hexdigest(),'compiled':p.returncode==0})
            if p.returncode:raise RuntimeError(source+' failed; see '+str(output/(str(i)+'.log')))
        report['status']='passed'
    except BaseException as e:report.update(status='failed',error=str(e));raise
    finally:(output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(str(len(SOURCES))+' Windows modules compiled to COFF; native Windows acceptance NOT RUN')

if __name__=='__main__':main()
