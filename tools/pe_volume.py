"""Build/verify a bounded read-only MiniNT runtime volume; never executes input PEs."""
from pathlib import Path
import argparse
import json
import struct
import sys
import nt_compatibility as nt

MAGIC=0x314C4F56504547
HEADER=7216
EXTENDED_HEADER=9648
MAX_BYTES=64<<20

def fnv(data,volume=False):
    h=14695981039346656037
    for i,b in enumerate(data):
        h=((h^(0 if volume and 40<=i<48 else b))*1099511628211)&((1<<64)-1)
    return h

def text(value,size):
    nt.require(isinstance(value,str) and 0<len(value)<size and all(32<=ord(c)<=126 for c in value),'Invalid bounded text')
    return value.encode('ascii')+bytes(size-len(value))

def path(value,directory=False):
    nt.require(isinstance(value,str),'Invalid guest path')
    value=value.lower().replace('/','\\')
    if directory and not value.endswith('\\'):value+='\\'
    nt.require(directory or not value.endswith('\\'),'File path has trailing separator')
    nt.require(value.startswith(('c:\\app\\','c:\\windows\\system32\\','c:\\windows\\winsxs\\')),'Guest path outside volume namespace')
    nt.require(not any(c in value[2:] for c in ':"*?<>|') and all(p and p not in ('.','..') and not p.endswith(('.',' ')) for p in value[3:].rstrip('\\').split('\\')),'Guest path rejected')
    text(value,128)
    return value

def validate_volume(data):
    nt.require(HEADER<=len(data)<=MAX_BYTES,'Volume size rejected')
    magic,version,header,total,profile,count,root,aliases,assemblies,h=struct.unpack_from('<Q8IQ',data)
    nt.require((magic,total)==(MAGIC,len(data)) and (version,header) in ((1,HEADER),(2,EXTENDED_HEADER)) and profile in [p[0] for p in nt.PROFILES.values()], 'Volume header rejected')
    nt.require(0<count<=(16 if version==1 else 32) and root<count and aliases<=16 and assemblies<=8 and fnv(data,True)==h,'Volume table/hash rejected')
    def decode(at,size):
        raw=data[at:at+size];end=raw.find(b'\0')
        nt.require(end>0 and raw[end:]==bytes(size-end) and all(32<=b<=126 for b in raw[:end]),'Volume text rejected')
        return raw[:end].decode('ascii')
    records=[];previous=(header+4095)&~4095;names=set()
    for i in range(count):
        at=48+i*152 if i<16 else HEADER+(i-16)*152;name=decode(at,128);offset,size,kind,assembly,digest=struct.unpack_from('<4IQ',data,at+128)
        nt.require(path(name)==name and name not in names and assembly<=assemblies and kind in (1,2,3),'Volume file rejected')
        nt.require(0<size<=(4096 if kind==2 else 8<<20) and offset%4096==0 and previous<=offset<=len(data) and size<=len(data)-offset,'Volume file range rejected')
        nt.require(kind!=3 or (name.startswith('c:\\app\\') and (name.rsplit('.',1)[-1] in ('txt','ico','dat','bin') or version==2 and name.rsplit('.',1)[-1] in ('cfg','der','png','bmp','jpg','jpeg','wav'))),'Data file namespace/type rejected')
        content=data[offset:offset+size];nt.require(fnv(content)==digest,'File hash mismatch')
        records.append({'path':name,'bytes':size,'kind':kind,'assembly':assembly,'sha256':nt.sha(content)})
        names.add(name);previous=offset+size
    nt.require(records[root]['kind']==1,'Root is not a PE')
    directories=set();identities=set();identities_base=48+16*152+16*192
    for i in range(assemblies):
        at=identities_base+i*208;name=decode(at,64);v=struct.unpack_from('<4I',data,at+64);directory=decode(at+80,128)
        nt.require(path(directory,True)==directory and directory.startswith('c:\\windows\\winsxs\\') and directory not in directories,'Assembly directory rejected')
        nt.require(all(n<=65535 for n in v) and (name,v) not in identities,'Assembly identity rejected')
        directories.add(directory);identities.add((name,v))
        for record in records:
            if record['assembly']==i+1:nt.require(record['path'].startswith(directory),'File outside assembly')
    seen=set()
    for i in range(aliases):
        at=48+16*152+i*192;name=decode(at,64);target=decode(at+64,128)
        nt.require(name.startswith(('api-ms-','ext-ms-')) and name not in seen and any(r['path']==target and r['kind']==1 for r in records),'API alias rejected')
        seen.add(name)
    return profile,root,records

def build(spec,kernel,output):
    profile=spec['profile'];nt.require(profile in nt.PROFILES,'Unsupported profile')
    files,aliases,assemblies=spec['files'],spec.get('aliases',[]),spec.get('assemblies',[])
    extended=spec.get('phase') in ('14.8','14.9','12F','14.10A','14.10B','14.10C','14.10D','14.10E','14.10F','14.10G','14.11');header=EXTENDED_HEADER if extended else HEADER
    nt.require(0<len(files)<=(32 if extended else 16) and len(aliases)<=16 and len(assemblies)<=8,'Volume table budget exceeded')
    data=bytearray((header+4095)&~4095);records=[];pe_inventory=[];names=set()
    for entry in files:
        name=path(entry['path']);nt.require(name not in names,'Duplicate guest path');names.add(name)
        kind={'pe':1,'manifest':2,'data':3}[entry['kind']]
        nt.require(kind!=3 or spec.get('phase') in ('14.7','14.8','14.9','12F','14.10A','14.10B','14.10C','14.10D','14.10E','14.10F','14.10G','14.11'),'Data assets require phase 14.7')
        content=nt.regular(Path(entry['source']),4096 if kind==2 else 8<<20)
        nt.require(content,'Empty file rejected')
        assembly=entry.get('assembly',0);nt.require(type(assembly) is int and 0<=assembly<=len(assemblies),'Assembly index rejected')
        if assembly:nt.require(name.startswith(path(assemblies[assembly-1]['directory'],True)),'Assembly file outside registered directory')
        offset=(len(data)+4095)&~4095;data+=bytes(offset-len(data));data+=content
        nt.require(len(data)<=MAX_BYTES,'Volume exceeds 64 MiB')
        records.append((name,offset,len(content),kind,assembly,fnv(content),nt.sha(content)))
        if kind==1:
            pe=nt.InventoryPe(Path(entry['source']),content)
            nt.require(pe.image<=8<<20,'PE image exceeds 8 MiB')
            pe_inventory.append({'guestPath':name,'sha256':nt.sha(content),'fileBytes':len(content),'imageBytes':pe.image,
                                 'imports':pe.import_records(),'exports':pe.export_records(),'executed':False})
    root=next((i for i,r in enumerate(records) if r[0]==path(spec['root'])),None)
    nt.require(root is not None and records[root][3]==1,'Root PE missing')
    struct.pack_into('<Q8IQ',data,0,MAGIC,2 if extended else 1,header,len(data),nt.PROFILES[profile][0],len(records),root,len(aliases),len(assemblies),0)
    for i,r in enumerate(records):
        name,offset,size,kind,assembly,h,_=r
        at=48+i*152 if i<16 else HEADER+(i-16)*152
        data[at:at+152]=text(name,128)+struct.pack('<4IQ',offset,size,kind,assembly,h)
    seen=set()
    for i,a in enumerate(aliases):
        name=a['name'].lower();target=path(a['target'])
        nt.require(name.startswith(('api-ms-','ext-ms-')) and name not in seen and target in names,'API alias rejected')
        seen.add(name);at=48+16*152+i*192;data[at:at+192]=text(name,64)+text(target,128)
    seen=set()
    for i,a in enumerate(assemblies):
        directory=path(a['directory'],True);nt.require(directory.startswith('c:\\windows\\winsxs\\') and directory not in seen,'Assembly directory rejected');seen.add(directory)
        version=[int(v) for v in a['version'].split('.')];nt.require(len(version)==4 and all(0<=v<=65535 for v in version),'Assembly version rejected')
        at=48+16*152+16*192+i*208;data[at:at+208]=text(a['name'],64)+struct.pack('<4I',*version)+text(directory,128)
    struct.pack_into('<Q',data,40,fnv(data,True))
    validate_volume(data)
    kernel_data=nt.regular(kernel,4<<20);nt.require(b'GuardianExternalPe/1/volume' in kernel_data,'External PE kernel required')
    root_record=records[root]
    requested_phase=spec.get('phase','14.2');phase='14.9' if requested_phase in ('12F','14.10A','14.10B','14.10C','14.10D','14.10E','14.10F','14.10G','14.11') else requested_phase;nt.require(phase in ('14.2','14.3','14.4','14.5','14.6','14.7','14.8','14.9'),'Unsupported volume phase')
    if phase in ('14.3','14.4','14.5','14.6','14.7','14.8','14.9'):nt.require(b'GuardianNativeNt/1/guest' in kernel_data,'Native NT kernel required')
    if phase in ('14.4','14.5','14.6','14.7','14.8','14.9'):nt.require(b'GuardianWin32/1/guest' in kernel_data,'Win32 kernel required')
    if phase in ('14.5','14.6','14.7','14.8','14.9'):nt.require(b'GuardianCrt/1/guest' in kernel_data,'CRT kernel required')
    if phase in ('14.6','14.7','14.8','14.9'):nt.require(b'GuardianSecurity/1/guest' in kernel_data,'Security kernel required')
    manifest={'schema':1,'phase':phase,'contract':'guardian-nt-contract/1','volumeFormat':'guardian-pe-volume/2' if phase in ('14.8','14.9') else 'guardian-pe-volume/1',
              'profile':profile,'targetNt':nt.PROFILES[profile][1:],'architecture':'x64','revision':None,
              'runtimeSha256':nt.sha(data),'kernelSha256':nt.sha(kernel_data),'applicationSha256':root_record[6],
              'files':[{'path':r[0],'bytes':r[2],'kind':r[3],'assembly':r[4],'sha256':r[6]} for r in records],
              'apiMapKind':'explicit guest package mappings; no claim of native Windows schema conformance',
              'aliases':aliases,'assemblies':assemblies,'windowsBinaryCompatibility':False,'phase14_3Started':False}
    if phase in ('14.3','14.4','14.5','14.6','14.7','14.8','14.9'):manifest.update({'phase14_3Started':True,'phase14_4Started':phase in ('14.4','14.5','14.6','14.7','14.8','14.9'),'nativeRuntime':spec['nativeRuntime']})
    if phase in ('14.4','14.5','14.6','14.7','14.8','14.9'):manifest.update({'win32Runtime':spec['win32Runtime'],'phase14_5Started':phase in ('14.5','14.6','14.7','14.8','14.9')})
    if phase in ('14.5','14.6','14.7','14.8','14.9'):manifest.update({'crtRuntime':spec['crtRuntime']})
    if phase in ('14.6','14.7','14.8','14.9'):manifest.update({'securityRuntime':spec['securityRuntime']})
    if phase in ('14.7','14.8','14.9'):
        nt.require(b'GuardianDesktop/1/guest' in kernel_data,'Desktop kernel required')
        manifest.update({'desktopRuntime':spec['desktopRuntime']})
    if phase in ('14.8','14.9'):
        nt.require(b'GuardianComponent/1/guest' in kernel_data,'Component kernel required')
        manifest['componentRuntime']=spec['componentRuntime']
    if phase=='14.9':
        nt.require(b'GuardianInternet/1/guest' in kernel_data,'Internet kernel required')
        manifest['internetRuntime']=spec['internetRuntime'];manifest['phase14_9Started']=True
    if requested_phase=='12F':
        nt.require(b'GuardianWindowBridge/1/guest' in kernel_data,'Integrated window kernel required')
        manifest.update({'phase':'12F','phase12FStarted':True,'presentationRuntime':spec['presentationRuntime']})
    if requested_phase=='14.10A':
        nt.require(b'GuardianNativeGui/1/guest' in kernel_data,'Native GUI kernel required')
        manifest.update({'phase':'14.10A','nativeGuiRuntime':spec['nativeGuiRuntime'],'phase14_10AStarted':True,'phase14_10BStarted':False})
    if requested_phase=='14.10B':
        nt.require(b'GuardianNativeUser32/2/guest' in kernel_data,'Native USER32 kernel required')
        manifest.update({'phase':'14.10B','nativeGuiRuntime':spec['nativeGuiRuntime'],'phase14_10BStarted':True,'phase14_10CStarted':False})
    if requested_phase=='14.10C':
        nt.require(b'GuardianNativeGdi/3/guest' in kernel_data,'Native GDI kernel required')
        manifest.update({'phase':'14.10C','nativeGuiRuntime':spec['nativeGuiRuntime'],'phase14_10CStarted':True,'phase14_10DStarted':False})
    if requested_phase=='14.10D':
        nt.require(b'GuardianNativeControls/4/guest' in kernel_data,'Native controls kernel required')
        manifest.update({'phase':'14.10D','nativeGuiRuntime':spec['nativeGuiRuntime'],'phase14_10DStarted':True,'phase14_10EStarted':False})
    if requested_phase=='14.10E':
        nt.require(b'GuardianExpandedGdi/5/guest' in kernel_data,'Expanded GDI kernel required')
        manifest.update({'phase':'14.10E','nativeGuiRuntime':spec['nativeGuiRuntime'],'phase14_10EStarted':True,'phase14_10FStarted':False})
    if requested_phase=='14.10F':
        nt.require(b'GuardianSoftwareGraphics/6/guest' in kernel_data,'Software graphics kernel required')
        manifest.update({'phase':'14.10F','nativeGuiRuntime':spec['nativeGuiRuntime'],'phase14_10FStarted':True,'phase14_10GStarted':False})
    if requested_phase=='14.11':
        nt.require(b'GuardianFramework/2/cil-forms' in kernel_data,'Framework kernel required')
        manifest.update({'phase':'14.11','frameworkRuntime':spec['frameworkRuntime'],'nativeGuiRuntime':spec['nativeGuiRuntime'],'audioRuntime':spec['audioRuntime']})
    if requested_phase=='14.10G':
        nt.require(b'GuardianAudio/7/guest' in kernel_data,'Audio kernel required')
        manifest.update({'phase':'14.10G','nativeGuiRuntime':spec['nativeGuiRuntime'],'phase14_10GStarted':True,'audioRuntime':spec['audioRuntime']})
    encoded=nt.canonical(manifest)
    binding=struct.pack('<QQ8I',nt.MAGIC,0,1,176,*nt.PROFILES[profile],0,1)+b''.join(bytes.fromhex(s) for s in (
        nt.sha(encoded),manifest['kernelSha256'],manifest['applicationSha256'],manifest['runtimeSha256']))
    output=Path(output);output.mkdir(parents=True,exist_ok=False)
    for name,content in [('runtime.gpv',data),('manifest.json',encoded),('binding.gnt',binding),('inventory.json',nt.canonical(pe_inventory))]:
        (output/name).write_bytes(content)
    return manifest

def sample(build_dir,config,kernel,output,profile='win10'):
    base=Path(build_dir)/'guest/external'/config
    alternate=Path(output).parent/(Path(output).name+'-alternate.manifest')
    alternate.write_text('<?xml version="1.0"?><assembly manifestVersion="1.0"><dependency><dependentAssembly><assemblyIdentity name="Guardian.Widget" version="2.0.0.0" processorArchitecture="amd64" type="win32"/></dependentAssembly></dependency></assembly>',encoding='ascii')
    files=[{'path':'c:\\app\\guardian-external.exe','source':str(base/'guardian-external.exe'),'kind':'pe'}]
    for name in ('tracker','bridge','delay','reject','late'):
        files.append({'path':f'c:\\app\\guardian-{name}.dll','source':str(base/f'guardian-{name}.dll'),'kind':'pe'})
    files.append({'path':'c:\\app\\guardian_leaf.dll','source':str(base/'guardian_leaf.dll'),'kind':'pe'})
    files.append({'path':'c:\\windows\\system32\\guardian-runtime-api.dll','source':str(base/'guardian-runtime-api.dll'),'kind':'pe'})
    for version in (1,2):
        files.append({'path':f'c:\\windows\\winsxs\\widget_{version}\\widget.dll','source':str(base/f'widget{version}/widget.dll'),'kind':'pe','assembly':version})
    files.append({'path':'c:\\app\\alternate.manifest','source':str(alternate),'kind':'manifest'})
    spec={'profile':profile,'root':files[0]['path'],'files':files,
          'aliases':[{'name':'api-ms-win-guardian-probe-l1-1-0.dll','target':'c:\\app\\guardian_leaf.dll'}],
          'assemblies':[{'name':'Guardian.Widget','version':f'{i}.0.0.0','directory':f'c:\\windows\\winsxs\\widget_{i}\\'} for i in (1,2)]}
    return build(spec,kernel,output)

def verify(bundle):
    bundle=Path(bundle);raw=nt.regular(bundle/'manifest.json',4<<20);manifest=json.loads(raw,object_pairs_hook=nt.pairs_unique)
    nt.require(raw==nt.canonical(manifest) and manifest['phase'] in ('14.2','14.3','14.4','14.5','14.6','14.7','14.8','14.9','12F','14.10A','14.10B','14.10C','14.10D','14.10E','14.10F','14.10G','14.11'),'Manifest rejected')
    data=nt.regular(bundle/'runtime.gpv',MAX_BYTES);nt.require(nt.sha(data)==manifest['runtimeSha256'],'Volume digest mismatch')
    profile,root,records=validate_volume(data)
    p=manifest['profile'];nt.require(p in nt.PROFILES and manifest['architecture']=='x64' and manifest['revision'] is None,'Profile rejected')
    nt.require(profile==nt.PROFILES[p][0] and records==manifest['files'] and records[root]['sha256']==manifest['applicationSha256'],'Volume/manifest mismatch')
    expected=struct.pack('<QQ8I',nt.MAGIC,0,1,176,*nt.PROFILES[p],0,1)+b''.join(bytes.fromhex(s) for s in (
        nt.sha(raw),manifest['kernelSha256'],manifest['applicationSha256'],manifest['runtimeSha256']))
    nt.require(nt.regular(bundle/'binding.gnt',176)==expected,'Binding mismatch')
    return manifest

def main():
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='command',required=True)
    for operation in ('create','sample'):
        p=commands.add_parser(operation);p.add_argument('--kernel',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
        if operation=='create':p.add_argument('--spec',type=Path,required=True)
        else:
            p.add_argument('--build',type=Path,required=True);p.add_argument('--config',choices=('Debug','Release'),required=True);p.add_argument('--profile',choices=nt.PROFILES,default='win10')
    commands.add_parser('verify').add_argument('bundle',type=Path)
    args=parser.parse_args()
    try:
        if args.command=='verify':result=verify(args.bundle)
        elif args.command=='sample':result=sample(args.build,args.config,args.kernel,args.output,args.profile)
        else:result=build(json.loads(nt.regular(args.spec,1<<20),object_pairs_hook=nt.pairs_unique),args.kernel,args.output)
        print(json.dumps({k:result[k] for k in ('phase','profile','runtimeSha256','windowsBinaryCompatibility')}));return 0
    except (ValueError,KeyError,TypeError,OSError,struct.error) as error:print(str(error),file=sys.stderr);return 2

if __name__=='__main__':raise SystemExit(main())
