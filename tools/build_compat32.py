"""Compile owned guest-only x86 adapters with the MSVC cross toolchain; no host execution."""
from pathlib import Path
import argparse,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def build(output,config,vc=None,sdk=None):
    if vc is None:
        vs=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
        install=subprocess.check_output([str(vs),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
        versions=Path(install)/'VC/Tools/MSVC';vc=max(versions.iterdir(),key=lambda p:tuple(map(int,p.name.split('.'))))
    vc=Path(vc);sdk=Path(sdk) if sdk else Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10/Include'
    if not (sdk/'um').exists():sdk=max(sdk.iterdir(),key=lambda p:tuple(map(int,p.name.split('.'))))
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True);cl=vc/'bin/Hostx64/x86/cl.exe';link=vc/'bin/Hostx64/x86/link.exe';ml=vc/'bin/Hostx64/x86/ml.exe'
    common=['/nologo','/c','/std:c++17','/W4','/GS-','/Gs2147483647','/GR-','/EHs-c-','/Zl','/Oi','/utf-8','/D_WIN32_WINNT=0x0A00','/DGUARDIAN_COMPAT32=1','/O2' if config=='Release' else '/Od']
    includes=['/I'+str(p) for p in (ROOT/'shared',vc/'include',sdk/'shared',sdk/'um',sdk/'ucrt')]
    def run(args):subprocess.run(list(map(str,args)),check=True,cwd=output)
    def compile(source,name,extra=()):
        target=output/(name+'.obj');run([cl,*common,*includes,*extra,'/Fo'+str(target),ROOT/source]);return target
    memory=compile('guest/Memory.cpp','memory');kernel=compile('guest/fixtures/Compat32Kernel.cpp','kernel');user=compile('guest/fixtures/NativeUser32.cpp','user')
    gate=output/'gate.obj';run([ml,'/nologo','/c','/Fo'+str(gate),ROOT/'guest/fixtures/Compat32Gate.asm'])
    flags=['/nologo','/MACHINE:X86','/NODEFAULTLIB','/DYNAMICBASE','/NXCOMPAT','/SAFESEH:NO','/INCREMENTAL:NO','/MANIFEST:NO','/DEBUG:NONE']
    text=(ROOT/'guest/fixtures/Compat32Kernel.cpp').read_text(encoding='utf-8')
    exports=re.findall(r'extern "C" [^\n{]+?\b(?:WINAPI|__cdecl)\s+(\w+)\(',text)
    definition=output/'kernel32.def';definition.write_text('LIBRARY kernel32\nEXPORTS\n'+''.join('    '+name+'\n' for name in exports if name!='CompatDllMain'),encoding='utf-8')
    run([link,*flags,'/DLL','/ENTRY:CompatDllMain','/DEF:'+str(definition),'/OUT:'+str(output/'kernel32.dll'),kernel,memory,gate])
    run([link,*flags,'/DLL','/NOENTRY','/EXPORT:GuardianNativeUserCall','/OUT:'+str(output/'win32u.dll'),gate])
    run([link,*flags,'/DLL','/ENTRY:NativeUserEntry','/DEF:'+str(ROOT/'guest/fixtures/NativeUser32.def'),'/OUT:'+str(output/'user32.dll'),user,memory,output/'kernel32.lib',output/'win32u.lib'])
    crt=compile('guest/fixtures/Compat32Crt.cpp','crt')
    run([link,*flags,'/DLL','/ENTRY:CompatCrtEntry','/EXPORT:printf','/EXPORT:puts','/OUT:'+str(output/'msvcrt.dll'),crt,memory,output/'kernel32.lib'])
    for mode in ('console','gui','input','crash','gui_crash','exit'):
        fixture=compile('guest/fixtures/Compat32Acceptance.cpp','acceptance-'+mode,() if mode=='console' else ('/DCOMPAT32_'+mode.upper()+'=1',))
        run([link,*flags,'/ENTRY:Compat32Main','/SUBSYSTEM:CONSOLE','/OUT:'+str(output/('guardian-x86-'+mode+'.exe')),fixture,memory,output/'kernel32.lib',output/'user32.lib'])
    print('Owned x86 adapters built: '+str(output),flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--vc',type=Path);p.add_argument('--sdk',type=Path);a=p.parse_args();build(a.output,a.config,a.vc,a.sdk)
