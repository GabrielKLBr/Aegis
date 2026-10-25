"""Phase 14.5 guest CRT dependencies. Microsoft DLLs are legal local inputs only."""
from pathlib import Path
import argparse,json,re
import api_set,native_nt,nt_compatibility as nt,pe_volume,win32_runtime

RTL='RtlCaptureContext RtlLookupFunctionEntry RtlVirtualUnwind RtlPcToFileHeader RtlUnwind RtlUnwindEx RtlCaptureStackBackTrace'.split()
def references(directory,names=None):
    policy=json.loads((nt.ROOT/'config/crt-references.json').read_bytes());records=[]
    for name in names or sorted(p.name for p in Path(directory).glob('*.dll')):
        nt.require(name in policy['files'],'CRT reference not in accepted version policy: '+name)
        path=Path(directory)/name;raw=nt.regular(path,8<<20);pe=nt.InventoryPe(path,raw)
        record={'version':native_nt.file_version(pe),'sha256':nt.sha(raw),'bytes':len(raw)}
        nt.require(record==policy['files'][name],'CRT version or digest is not accepted: '+name)
        records.append({'name':name,**record,'inputExecutedOnHost':False})
    nt.require(records,'CRT references missing');return records

def exports(folder,reference):
    references(reference)
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    native_nt.wrappers(folder/'NativeWrappers.cpp',folder/'NativeExports.def')
    with (folder/'NativeExports.def').open('a',encoding='utf-8') as f:
        f.write('\n'.join('    '+n for n in RTL[:5])+'\n')
    win32_runtime.exports(folder/'kernelbase.def',folder/'kernel32.def','win10')
    raw=(folder/'kernelbase.def').read_text();names={l.strip().split('=')[0] for l in raw.splitlines()[2:]}
    files=[nt.ROOT/'guest/fixtures/CrtWin32.inl',nt.ROOT/'guest/fixtures/CrtSynchronization.inl']
    own={s[2:]:s for s in re.findall(r'extern "C" [^{;\n]+?\b(Kb\w+)\(', '\n'.join(p.read_text(encoding='utf-8') for p in files if p.exists()))}
    required=set()
    for p in sorted(Path(reference).glob('*.dll')):
        for dll,symbols in nt.InventoryPe(p).imports().items():
            if dll=='kernel32.dll' or (dll.startswith(('api-ms-win-core-','ext-ms-win-'))):required.update(symbols)
    missing=sorted(required-names-own.keys()-set(RTL))
    # Eager imports outside this corpus are fail-closed, never successful no-ops.
    stubs=['#include "PeRuntimeApi.h"','extern "C" __declspec(noreturn) void CrtUnsupportedExport(const char*);']
    for n in missing:
        nt.require(re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*',n),'Invalid import name')
        stubs.append('extern "C" __declspec(noreturn) void Kb'+n+'(){CrtUnsupportedExport("'+n+'");}')
        own[n]='Kb'+n
    (folder/'CrtUnsupported.cpp').write_text('\n'.join(stubs)+'\n',encoding='utf-8')
    merged={l.strip().split('=')[0]:l.strip().split('=',1)[1] for l in raw.splitlines()[2:]}
    merged.update(own);merged.update({n:'ntdll.'+n for n in RTL})
    (folder/'kernelbase.def').write_text('LIBRARY kernelbase\nEXPORTS\n'+'\n'.join('    '+n+'='+v for n,v in sorted(merged.items()))+'\n',encoding='utf-8')
    (folder/'kernel32.def').write_text('LIBRARY kernel32\nEXPORTS\n'+'\n'.join('    '+n+'=kernelbase.'+n for n in sorted(merged))+'\n',encoding='utf-8')
    (folder/'coverage.json').write_bytes(nt.canonical({'exports':len(merged),'failClosedImports':missing}))
    return {'exports':len(merged),'failClosedImports':missing}

def bundle(build,config,output,reference,schema_reference,application='guardian-crt-probe.exe',runtimes=('ucrtbase.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll'),profile='win10'):
    nt.require(profile=='win10','This selected VC 14.44 reference package requires Windows 10; runtime versions are independent of NT profiles')
    build=Path(build);output=Path(output);output.parent.mkdir(parents=True,exist_ok=True);base=build/'guest/crt'/config
    schema,metadata=api_set.build(profile,schema_reference,('kernelbase.dll','kernel32.dll','ntdll.dll','ucrtbase.dll'))
    schema_path=output.parent/(output.name+'-apisetschema.dll');schema_path.write_bytes(schema)
    table,native=native_nt.syscall_map(base/'ntdll.dll',profile,False);table_path=output.parent/(output.name+'-syscalls.bin');table_path.write_bytes(table)
    files=[{'path':'c:\\app\\'+application,'source':str(base/application),'kind':'pe'}]
    for name in ('kernel32.dll','kernelbase.dll','ntdll.dll'):
        files.append({'path':'c:\\windows\\system32\\'+name,'source':str(base/name),'kind':'pe'})
    files.extend([{'path':'c:\\windows\\system32\\guardian-runtime-api.dll','source':str(base/'guardian-runtime-api.dll'),'kind':'pe'},
                  {'path':'c:\\windows\\system32\\apisetschema.dll','source':str(schema_path),'kind':'pe'},
                  {'path':'c:\\windows\\system32\\guardian-nt-syscalls.bin','source':str(table_path),'kind':'manifest'}])
    inventory=references(reference,runtimes)
    for name in runtimes:
        source=Path(reference)/name
        files.append({'path':'c:\\windows\\system32\\'+name,'source':str(source),'kind':'pe'})
    if application=='guardian-crt-launcher.exe':files.append({'path':'c:\\app\\guardian-crt.exe','source':str(base/'guardian-crt.exe'),'kind':'pe'})
    spec={'phase':'14.5','profile':profile,'root':files[0]['path'],'files':files,'nativeRuntime':native,
          'win32Runtime':{'apiSet':metadata,'nativeMicrosoftKernelBaseValidated':False},'crtRuntime':{'originalReferences':inventory,'completeWindowsCrt':False}}
    manifest=pe_volume.build(spec,build/'guest'/config/'guardian-kernel-crt.gk',output)
    actual={f['path'].rsplit('\\',1)[-1]:f['sha256'] for f in manifest['files']}
    for record in inventory:nt.require(actual[record['name']]==record['sha256'],'CRT input changed while building the volume: '+record['name'])
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True)
    e=s.add_parser('exports');e.add_argument('folder',type=Path);e.add_argument('--reference',type=Path,required=True)
    b=s.add_parser('bundle');b.add_argument('--build',type=Path,required=True);b.add_argument('--config',required=True);b.add_argument('--output',type=Path,required=True);b.add_argument('--reference',type=Path,required=True);b.add_argument('--schema-reference',type=Path,required=True);b.add_argument('--application',default='guardian-crt-probe.exe')
    a=p.parse_args()
    if a.command=='exports':result=exports(a.folder,a.reference)
    else:
        extra={'runtimes':('msvcrt.dll',)} if a.application=='guardian-crt-legacy.exe' else {}
        result=bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,**extra)
    print(json.dumps(result))
if __name__=='__main__':main()
