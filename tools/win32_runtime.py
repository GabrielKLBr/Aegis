"""Owned KERNEL32/KERNELBASE build exports and immutable Win32 guest bundles."""
from pathlib import Path
import argparse
import json
import re
import native_nt
import api_set
import nt_compatibility as nt
import pe_volume

FORWARDS='QueryPerformanceCounter QueryPerformanceFrequency GetTickCount64 GetTickCount GetSystemTimeAsFileTime QueryUnbiasedInterruptTime Sleep LoadLibraryA FreeLibrary GetModuleHandleA FindResourceA FindResourceExA LoadResource LockResource SizeofResource CreateActCtxA ActivateActCtx DeactivateActCtx ReleaseActCtx RaiseException GetLastError SetLastError GetCurrentProcessId GetCurrentThreadId CreateThread ResumeThread GetExitCodeThread SwitchToThread GetExitCodeProcess ExitThread ExitProcess GetStdHandle CreateMutexA ReleaseMutex InitializeCriticalSectionEx EnterCriticalSection TryEnterCriticalSection LeaveCriticalSection DeleteCriticalSection'.split()

def exports(base,kernel,profile):
    files=[nt.ROOT/'guest/fixtures/Win32Base.cpp',*(nt.ROOT/'guest/fixtures'/name for name in ('Win32Environment.inl','Win32Files.inl','Win32Async.inl','Win32Threads.inl'))]
    symbols=sorted(set(re.findall(r'extern "C" [^{;\n]+?\b(Kb\w+)\(', '\n'.join(p.read_text(encoding='utf-8') for p in files)))-{'KbDllEntry'})
    own={name[2:]:name for name in symbols}
    if profile=='win7':own.pop('GetSystemTimePreciseAsFileTime',None)
    own['GetEnvironmentStringsA']='KbGetEnvironmentStrings'
    overlap=set(own)&set(FORWARDS);nt.require(not overlap,'Conflicting exports: '+str(overlap))
    names=sorted({*own,*FORWARDS})
    Path(base).write_text('LIBRARY kernelbase\nEXPORTS\n'+'\n'.join('    '+n+'='+own.get(n,'guardian-runtime-api.'+n) for n in names)+'\n',encoding='utf-8')
    Path(kernel).write_text('LIBRARY kernel32\nEXPORTS\n'+'\n'.join('    '+n+'=kernelbase.'+n for n in names)+'\n',encoding='utf-8')
    return names

def bundle(build,config,output,profile,reference=None):
    build=Path(build);output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    base=build/'guest/win32'/config/profile;native=build/'guest/native'/config
    schema,metadata=api_set.build(profile,reference if profile=='win10' else None)
    schema_path=output.parent/(output.name+'-apisetschema.dll');schema_path.write_bytes(schema)
    table,native_metadata=native_nt.syscall_map(native/'ntdll.dll',profile,False)
    table_path=output.parent/(output.name+'-syscalls.bin');table_path.write_bytes(table)
    files=[{'path':'c:\\app\\guardian-win32.exe','source':str(base/'guardian-win32.exe'),'kind':'pe'},
           {'path':'c:\\app\\guardian-win32-child.exe','source':str(build/'guest/win32'/config/'guardian-win32-child.exe'),'kind':'pe'},
           {'path':'c:\\app\\guardian-win32-plugin.dll','source':str(build/'guest/win32'/config/'guardian-win32-plugin.dll'),'kind':'pe'}]
    for name in ('kernel32.dll','kernelbase.dll'):files.append({'path':'c:\\windows\\system32\\'+name,'source':str(base/name),'kind':'pe'})
    files.extend([{'path':'c:\\windows\\system32\\ntdll.dll','source':str(native/'ntdll.dll'),'kind':'pe'},
                  {'path':'c:\\windows\\system32\\guardian-runtime-api.dll','source':str(build/'guest/external'/config/'guardian-runtime-api.dll'),'kind':'pe'},
                  {'path':'c:\\windows\\system32\\apisetschema.dll','source':str(schema_path),'kind':'pe'},
                  {'path':'c:\\windows\\system32\\guardian-nt-syscalls.bin','source':str(table_path),'kind':'manifest'}])
    spec={'phase':'14.4','profile':profile,'root':files[0]['path'],'files':files,'nativeRuntime':native_metadata,
          'win32Runtime':{'schema':1,'kind':'owned KERNEL32 forwarders and KERNELBASE guest implementation','apiSet':metadata,
                          'nativeMicrosoftKernelBaseValidated':False,'phase14_5Started':False}}
    return pe_volume.build(spec,build/'guest'/config/'guardian-kernel-win32.gk',output)

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True)
    e=s.add_parser('exports');e.add_argument('base');e.add_argument('kernel');e.add_argument('--profile',choices=nt.PROFILES,required=True)
    b=s.add_parser('bundle');b.add_argument('--build',type=Path,required=True);b.add_argument('--config',choices=('Release','Debug'),required=True);b.add_argument('--profile',choices=nt.PROFILES,required=True);b.add_argument('--output',type=Path,required=True);b.add_argument('--schema-reference',type=Path)
    a=p.parse_args()
    if a.command=='exports':print(json.dumps({'exports':len(exports(a.base,a.kernel,a.profile))}))
    else:print(json.dumps(bundle(a.build,a.config,a.output,a.profile,a.schema_reference)))
if __name__=='__main__':main()
