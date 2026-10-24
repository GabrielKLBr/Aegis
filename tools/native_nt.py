"""Build the owned NT wrapper and bind syscall numbers from the selected guest DLL, never the host."""
from pathlib import Path
import argparse
import json
import re
import struct
import nt_compatibility as nt
import pe_volume as pv

NAMES='AllocateVirtualMemory FreeVirtualMemory ProtectVirtualMemory QueryVirtualMemory CreateSection MapViewOfSection UnmapViewOfSection Close DuplicateObject QueryObject QueryInformationProcess QueryInformationThread QuerySystemInformation OpenProcess OpenThread CreateThreadEx ResumeThread TerminateThread TerminateProcess CreateEvent SetEvent ResetEvent CreateMutant ReleaseMutant CreateSemaphore ReleaseSemaphore CreateTimer SetTimer CancelTimer WaitForSingleObject WaitForMultipleObjects DelayExecution QueueApcThread TestAlert QuerySystemTime QueryPerformanceCounter FlushInstructionCache CreateFile OpenFile ReadFile WriteFile QueryInformationFile CreateKey OpenKey SetValueKey QueryValueKey DeleteValueKey DeleteKey'.split()
OPS='Allocate Free Protect QueryMemory CreateSection MapSection UnmapSection Close Duplicate QueryObject QueryProcess QueryThread QuerySystem OpenProcess OpenThread CreateThread ResumeThread TerminateThread TerminateProcess CreateEvent SetEvent ResetEvent CreateMutant ReleaseMutant CreateSemaphore ReleaseSemaphore CreateTimer SetTimer CancelTimer WaitSingle WaitMultiple Delay QueueApc TestAlert QueryTime QueryCounter FlushInstructionCache CreateFile OpenFile ReadFile WriteFile QueryFile CreateKey OpenKey SetValue QueryValue DeleteValue DeleteKey'.split()
SERVICES={('Nt'+name):(i+1,op) for i,(name,op) in enumerate(zip(NAMES,OPS))}

def file_version(pe):
    # Parse the resource from the same immutable bytes used for the syscall
    # map. Never load the input DLL into a host process or reread its metadata.
    raw=pe.data;rva,size=pe.directory(2);nt.require(rva and size,'Version resource missing')
    def entries(offset):
        nt.require(0<=offset<=size-16,'Version directory range')
        at=pe.at(rva+offset,16);named,ids=struct.unpack_from('<HH',raw,at+12);count=named+ids
        nt.require(count<=512 and offset+16+count*8<=size,'Version directory budget')
        return [struct.unpack_from('<II',raw,pe.at(rva+offset+16+i*8,8)) for i in range(count)]
    roots=[value&0x7fffffff for key,value in entries(0) if key==16 and value>>31];nt.require(len(roots)==1,'Unique version resource required')
    versions=[]
    for _,directory in entries(roots[0]):
        nt.require(directory>>31,'Version language directory required')
        for _,leaf in entries(directory&0x7fffffff):
            nt.require(not leaf>>31 and leaf<=size-16,'Version leaf range')
            address,length,_,reserved=struct.unpack_from('<4I',raw,pe.at(rva+leaf,16));nt.require(not reserved and 40<=length<=65536,'Version value range')
            block=raw[pe.at(address,length):pe.at(address,length)+length];total,value_length,kind=struct.unpack_from('<3H',block)
            key=('VS_VERSION_INFO\0').encode('utf-16le');at=(6+len(key)+3)&~3
            nt.require(kind==0 and value_length==52 and total<=length and at+52<=total and block[6:6+len(key)]==key,'Fixed version block required')
            values=struct.unpack_from('<13I',block,at);nt.require(values[0]==0xFEEF04BD,'Invalid fixed version signature')
            versions.append([values[2]>>16,values[2]&65535,values[3]>>16,values[3]&65535])
    nt.require(versions and all(v==versions[0] for v in versions),'Inconsistent version languages');return versions[0]

def wrappers(output,definition):
    header=(nt.ROOT/'guest/fixtures/NativeApi.h').read_text(encoding='utf-8')
    lines=['#define GUARDIAN_NTDLL_BUILD','#include "NativeApi.h"',
           'extern "C" uint32_t GuardianNativeCall(guardian::native::Packet*) noexcept;',
           'namespace guardian::native { namespace {',
           'uint64_t V(uint64_t v) noexcept{return v;} template<class T> uint64_t V(T* p) noexcept{return reinterpret_cast<uint64_t>(p);}',
           'template<class... T> Status Call(Op op,T... args) noexcept {Packet p;p.op=op;const uint64_t v[]{V(args)...,0};for(uint32_t i=0;i<sizeof...(args);++i)p.args[i]=v[i];return GuardianNativeCall(&p);}',
           '}']
    exports=['LIBRARY ntdll','EXPORTS']
    for name,parameters in re.findall(r'NTAPI_EXPORT Status (Nt\w+)\(([^;]*)\);',header):
        _,operation=SERVICES[name];types=parameters.split(',') if parameters else []
        args=[f'a{i}' for i in range(len(types))]
        lines.append(f'extern "C" Status {name}('+','.join(t+' '+a for t,a in zip(types,args))+'){return Call(Op::'+operation+(',' if args else '')+','.join(args)+');}')
        exports.extend((f'    {name}',f'    Zw{name[2:]}={name}'))
    lines.append('}')
    exports.extend('    '+n+'=guardian-runtime-api.'+n for n in ('RtlUnwindEx','RtlCaptureStackBackTrace','__C_specific_handler'))
    Path(output).write_text('\n'.join(lines)+'\n',encoding='utf-8');Path(definition).write_text('\n'.join(exports)+'\n',encoding='utf-8')

def syscall_map(dll,profile,original):
    raw=nt.regular(Path(dll),8<<20);pe=nt.InventoryPe(Path(dll),raw);entries=[];observations=[]
    version=None
    if original:
        version=file_version(pe);expected=nt.PROFILES[profile][1:]
        nt.require(version[:2]==list(expected[:2]) and (version[2]==expected[2] or (profile=='win10' and version[2]==19041)),'Original DLL version does not match target family')
        for export in pe.export_records():
            name=next((n for n in export['names'] if n in SERVICES),None)
            if not name:continue
            # Read the export RVA from the PE tables, not a process address.
            directory=pe.directory(0);base=pe.at(directory[0],40);h=struct.unpack_from('<10I',raw,base)
            function=struct.unpack_from('<I',raw,pe.at(h[7]+(export['ordinal']-h[4])*4,4))[0]
            stub=raw[pe.at(function,32):pe.at(function,32)+32]
            if stub[:4]!=b'\x4c\x8b\xd1\xb8' or b'\x0f\x05' not in stub:
                nt.require(name=='NtQuerySystemTime','Unrecognized guest syscall stub: '+name)
                observations.append({'export':name,'userModeRoutine':True,'mapped':False});continue
            number=struct.unpack_from('<I',stub,4)[0];operation,_=SERVICES[name]
            nt.require(number<0x2000 and number not in [e[0] for e in entries],'Duplicate syscall number')
            entries.append((number,operation));observations.append({'export':name,'number':number,'operation':operation})
        nt.require(len(entries)>=30,'Too few validated original syscall stubs')
    nt.require(len(entries)<=64,'Syscall map budget')
    encoded=struct.pack('<Q8I',0x31504D53544E47,1,552,nt.PROFILES[profile][0],len(entries),int(original),0,pv.fnv(raw)&0xffffffff,pv.fnv(raw)>>32)
    encoded+=b''.join(struct.pack('<II',*e) for e in entries)+bytes((64-len(entries))*8)
    return encoded,{'schema':1,'profile':profile,'source':'selected guest DLL','original':original,'dllSha256':nt.sha(raw),'fileVersion':version,'mappedServices':observations,
                    'nativeKernelReference':'host Windows reference recorded separately; no exact target-kernel conformity claim'}

def main():
    p=argparse.ArgumentParser(description=__doc__);c=p.add_subparsers(dest='command',required=True)
    w=c.add_parser('wrappers');w.add_argument('output');w.add_argument('definition')
    m=c.add_parser('map');m.add_argument('dll',type=Path);m.add_argument('output',type=Path);m.add_argument('--profile',choices=nt.PROFILES,required=True);m.add_argument('--original',action='store_true')
    b=c.add_parser('bundle');b.add_argument('--build',type=Path,required=True);b.add_argument('--config',choices=('Release','Debug'),required=True);b.add_argument('--profile',choices=nt.PROFILES,required=True);b.add_argument('--output',type=Path,required=True);b.add_argument('--original',type=Path)
    a=p.parse_args()
    if a.command=='wrappers':wrappers(a.output,a.definition)
    elif a.command=='map':
        data,metadata=syscall_map(a.dll,a.profile,a.original);a.output.write_bytes(data);a.output.with_suffix('.json').write_bytes(nt.canonical(metadata));print(json.dumps(metadata))
    else:print(json.dumps(bundle(a.build,a.config,a.output,a.profile,a.original)))

def bundle(build,config,output,profile='win10',original=None):
    build=Path(build);output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    base=build/'guest/native'/config;dll=Path(original) if original else base/'ntdll.dll'
    table,metadata=syscall_map(dll,profile,bool(original));table_path=output.parent/(output.name+'-syscalls.bin');table_path.write_bytes(table)
    kind='original' if original else 'owned'
    files=[{'path':'c:\\app\\guardian-native.exe','source':str(base/f'guardian-native-{kind}.exe'),'kind':'pe'},
           {'path':'c:\\windows\\system32\\ntdll.dll','source':str(dll),'kind':'pe'},
           {'path':'c:\\windows\\system32\\guardian-runtime-api.dll','source':str(build/'guest/external'/config/'guardian-runtime-api.dll'),'kind':'pe'},
           {'path':'c:\\windows\\system32\\guardian-nt-syscalls.bin','source':str(table_path),'kind':'manifest'}]
    spec={'phase':'14.3','profile':profile,'root':files[0]['path'],'files':files,'nativeRuntime':metadata}
    return pv.build(spec,build/'guest'/config/'guardian-kernel-native-nt.gk',output)
if __name__=='__main__':main()
