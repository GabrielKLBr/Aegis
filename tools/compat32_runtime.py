"""Inspect/package PE32 files without executing them; only the Windows 10 x86 subset."""
from pathlib import Path
import argparse,json,struct
import nt_compatibility as nt
import pe_volume as volume

class Pe32(nt.InventoryPe):
    def __init__(self,path,data=None):
        self.data=nt.regular(Path(path),8<<20) if data is None else data;self.sections=[]
        if self.slice(0,2)!=b'MZ':raise ValueError('Invalid PE32 MZ')
        pe=self.u32(60);nt.require(self.slice(pe,4)==b'PE\0\0','Invalid PE32 signature')
        self.machine=self.u16(pe+4);count=self.u16(pe+6);optional=self.u16(pe+20);start=pe+24
        nt.require(self.machine==0x14C and self.u16(start)==0x10B,'x86 PE32 is required; x64/ARM/mixed packages are refused')
        nt.require(1<=count<=32 and optional==224 and self.u32(start+92)==16,'PE32 header rejected')
        self.slice(start,optional);self.image=self.u32(start+56);self.headers=self.u32(start+60);self.base=self.u32(start+28)
        nt.require(0<self.image<=8<<20 and 0<self.headers<=len(self.data),'PE32 image budget')
        self.directories=[(self.u32(start+96+i*8),self.u32(start+100+i*8)) for i in range(16)];self.flags=[]
        for i in range(count):
            at=start+optional+i*40;self.slice(at,40);virtual,rva,size,raw=struct.unpack_from('<4I',self.data,at+8);self.slice(raw,size)
            nt.require(rva>=self.headers and rva+max(virtual,size)<=self.image,'PE32 section outside image')
            for va,rs,_,vs in self.sections:nt.require(rva+max(virtual,size)<=va or va+max(vs,rs)<=rva,'PE32 overlapping sections')
            flags=self.u32(at+36);nt.require(not(flags&0x80000000 and flags&0x20000000),'Writable executable section refused')
            self.sections.append((rva,size,raw,virtual));self.flags.append(flags)
        for index in (3,9,10,13,14):nt.require(self.directory(index)==(0,0),'PE32 TLS/SEH/load-config/delay imports/CLR not implemented in this initial subset')
    def imports(self):
        rva,size=self.directory(1);result={}
        if not rva and not size:return result
        nt.require(20<=size<=65536,'PE32 import budget')
        for offset in range(0,size-19,20):
            ilt,stamp,chain,name,iat=struct.unpack('<5I',self.slice(self.at(rva+offset,20),20))
            if not any((ilt,stamp,chain,name,iat)):return result
            nt.require(name and iat and not (iat&3) and not(ilt&3),'PE32 import descriptor')
            symbols=result.setdefault(self.string(name).lower(),[])
            for i in range(4096):
                value=self.u32(self.at((ilt or iat)+i*4,4))
                if not value:break
                nt.require(not(value&0x80000000 and value&0x7FFF0000),'PE32 invalid ordinal')
                symbols.append('#'+str(value&65535) if value&0x80000000 else self.string(value+2))
            else:raise ValueError('PE32 imports quota')
        raise ValueError('PE32 missing import terminator')
    def import_records(self):return [{'module':dll,'symbol':symbol,'kind':'static','importType':'PE32'} for dll,names in self.imports().items() for symbol in names]

def bundle(build,config,output,application,runtime=None,profile='win10'):
    nt.require(profile=='win10','Only NT 10.0.19045 is accepted for this x86 subset')
    build=Path(build);output=Path(output);runtime=Path(runtime) if runtime else build/'guest/compat32'/config
    nt.require(not output.exists(),'Use a fresh output directory');kernel=nt.regular(build/'guest'/config/'guardian-kernel-compat32.gk',8<<20)
    nt.require(b'GuardianCompat32/1/pe32' in kernel,'PE32 kernel required')
    files=[('c:\\app\\'+Path(application).name.lower(),Path(application))]+[('c:\\windows\\syswow64\\'+name,runtime/name) for name in ('kernel32.dll','user32.dll','win32u.dll','msvcrt.dll')]
    raw=bytearray((volume.HEADER+4095)&~4095);records=[];inventories=[]
    for name,file in files:
        content=nt.regular(file,8<<20);pe=Pe32(file,content);offset=(len(raw)+4095)&~4095;raw+=bytes(offset-len(raw))+content
        record={'path':name,'bytes':len(content),'kind':1,'assembly':0,'sha256':nt.sha(content)};records.append(record)
        i=len(records)-1;raw[48+i*152:48+(i+1)*152]=volume.text(name,128)+struct.pack('<4IQ',offset,len(content),1,0,volume.fnv(content))
        inventories.append({'path':name,'imports':pe.import_records(),'exports':pe.export_records(),'executedOnHost':False})
    available={name.rsplit('\\',1)[-1]:{n for e in inv['exports'] for n in e['names']} for (name,_),inv in zip(files,inventories)}
    for inv in inventories:
        for entry in inv['imports']:nt.require(entry['module'] in available and entry['symbol'] in available[entry['module']],'Unsupported x86 import: '+entry['module']+'!'+entry['symbol'])
    struct.pack_into('<Q8IQ',raw,0,volume.MAGIC,1,volume.HEADER,len(raw),1,len(records),0,0,0,0);struct.pack_into('<Q',raw,40,volume.fnv(raw,True))
    volume.validate_volume(raw)
    helper=nt.regular(build/config/'guardian-native-user-host.exe',16<<20)
    manifest={'schema':1,'phase':'14.12','profile':'win10','targetNt':[10,0,19045],'architecture':'x86','kernelArchitecture':'x64','revision':None,'contract':'guardian-nt-contract/1','volumeFormat':'guardian-pe-volume/1','windowsBinaryCompatibility':False,'files':records,'runtimeSha256':nt.sha(raw),'kernelSha256':nt.sha(kernel),'applicationSha256':records[0]['sha256'],'nativeGuiRuntime':{'backend':'win32-user32-helper','protocol':2,'helperSha256':nt.sha(helper)},'compat32':{'contract':1,'fullWOW64':False,'hostExecution':False,'systemDirectory':'c:\\windows\\syswow64','threads':1}}
    encoded=nt.canonical(manifest);binding=struct.pack('<QQ8I',nt.MAGIC,0,1,176,*nt.PROFILES['win10'],0,1)+b''.join(bytes.fromhex(h) for h in (nt.sha(encoded),nt.sha(kernel),records[0]['sha256'],nt.sha(raw)))
    output.mkdir(parents=True)
    for name,data in (('runtime.gpv',raw),('manifest.json',encoded),('binding.gnt',binding),('inventory.json',nt.canonical(inventories))):(output/name).write_bytes(data)
    verify(output,build/'guest'/config/'guardian-kernel-compat32.gk',build/config/'guardian-native-user-host.exe')
    return manifest

def verify(folder,kernel=None,helper=None):
    folder=Path(folder);encoded=nt.regular(folder/'manifest.json',1<<20);manifest=json.loads(encoded)
    nt.require(encoded==nt.canonical(manifest),'Noncanonical PE32 manifest')
    nt.require((manifest.get('schema'),manifest.get('phase'),manifest.get('architecture'),manifest.get('kernelArchitecture'),manifest.get('profile'),manifest.get('targetNt'))==(1,'14.12','x86','x64','win10',[10,0,19045]),'PE32 manifest identity rejected')
    raw=nt.regular(folder/'runtime.gpv',volume.MAX_BYTES);profile,root,records=volume.validate_volume(raw)
    nt.require(profile==1 and root==0 and records==manifest['files'] and len(records)==5,'PE32 manifest file table mismatch')
    nt.require(records[0]['path'].startswith('c:\\app\\') and records[0]['sha256']==manifest['applicationSha256'],'PE32 root rejected')
    nt.require([r['path'] for r in records[1:]]==['c:\\windows\\syswow64\\'+n for n in ('kernel32.dll','user32.dll','win32u.dll','msvcrt.dll')],'PE32 system DLL namespace rejected')
    nt.require(all(r['kind']==1 and r['assembly']==0 for r in records),'PE32 file kind rejected')
    inventories=[];available={}
    for i,record in enumerate(records):
        offset,size=struct.unpack_from('<II',raw,48+i*152+128);pe=Pe32(Path(record['path']),raw[offset:offset+size])
        inv={'path':record['path'],'imports':pe.import_records(),'exports':pe.export_records(),'executedOnHost':False};inventories.append(inv);available[record['path'].rsplit('\\',1)[-1]]={n for e in inv['exports'] for n in e['names']}
    for inv in inventories:
        for entry in inv['imports']:nt.require(entry['module'] in available and entry['symbol'] in available[entry['module']],'Unresolved PE32 import')
    nt.require(nt.regular(folder/'inventory.json',1<<20)==nt.canonical(inventories),'PE32 inventory mismatch')
    nt.require(manifest['runtimeSha256']==nt.sha(raw),'PE32 volume digest mismatch')
    expected=struct.pack('<QQ8I',nt.MAGIC,0,1,176,*nt.PROFILES['win10'],0,1)+bytes.fromhex(nt.sha(encoded))+b''.join(bytes.fromhex(manifest[h]) for h in ('kernelSha256','applicationSha256','runtimeSha256'))
    nt.require(nt.regular(folder/'binding.gnt',176)==expected,'PE32 binding mismatch')
    if kernel is not None:
        data=nt.regular(Path(kernel),8<<20);nt.require(nt.sha(data)==manifest['kernelSha256'] and b'GuardianCompat32/1/pe32' in data,'PE32 kernel mismatch')
    if helper is not None:nt.require(nt.sha(nt.regular(Path(helper),16<<20))==manifest['nativeGuiRuntime']['helperSha256'],'PE32 helper mismatch')
    return manifest

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','output','application'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--runtime',type=Path);a=p.parse_args();print(json.dumps(bundle(a.build,a.config,a.output,a.application,a.runtime)))
