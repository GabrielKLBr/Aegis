"""Phase 14.6 owned ADVAPI32/SECHOST/USERENV guest package; never host execution."""
from pathlib import Path
import argparse,json,re
import api_set,crt_runtime,native_nt,nt_compatibility as nt,pe_volume

def exports(folder):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    sources=[nt.ROOT/'guest/fixtures'/n for n in ('SecurityAdvapi.cpp','SecurityIdentity.inl','SecurityServices.inl','SecurityEvents.inl')]
    names=sorted(set(re.findall(r'extern "C" [^\n{]+?\bAv(\w+)\(', '\n'.join(p.read_text(encoding='utf-8') for p in sources))))
    nt.require(len(names)>60,'Security exports missing')
    (folder/'advapi32.def').write_text('LIBRARY advapi32\nEXPORTS\n'+''.join('    '+n+'=Av'+n+'\n' for n in names),encoding='utf-8')
    profiles={'GetUserProfileDirectoryW','GetProfilesDirectoryW','CreateEnvironmentBlock','DestroyEnvironmentBlock'}
    service={n for n in names if any(s in n for s in ('Service','SCManager','EventLog','EventSource','ReportEvent'))}
    for dll,selected in (('sechost',service),('userenv',profiles)):
        nt.require(selected<=set(names),'Forwarder target missing')
        (folder/(dll+'.def')).write_text('LIBRARY '+dll+'\nEXPORTS\n'+''.join('    '+n+'=advapi32.'+n+'\n' for n in sorted(selected)),encoding='utf-8')
    (folder/'coverage.json').write_bytes(nt.canonical({'schema':1,'phase':'14.6','exports':names,'securityBroker':'guest kernel','completeAdvapi32':False,'serviceAccounts':['LocalService'],'profiles':['win10']}))
    return names

def bundle(build,config,output,reference,schema_reference,application='guardian-security.exe'):
    build=Path(build);output=Path(output);base=build/'guest/crt'/config;security=build/'guest/security'/config
    output.parent.mkdir(parents=True,exist_ok=True)
    schema,metadata=api_set.build('win10',schema_reference,('kernelbase.dll','kernel32.dll','ntdll.dll','ucrtbase.dll','advapi32.dll','sechost.dll','userenv.dll'))
    schema_path=output.parent/(output.name+'-apisetschema.dll');schema_path.write_bytes(schema)
    table,native=native_nt.syscall_map(base/'ntdll.dll','win10',False);table_path=output.parent/(output.name+'-syscalls.bin');table_path.write_bytes(table)
    files=[{'path':'c:\\app\\'+application,'source':str(security/application),'kind':'pe'}]
    for name in ('kernel32.dll','kernelbase.dll','ntdll.dll','guardian-runtime-api.dll'):
        files.append({'path':'c:\\windows\\system32\\'+name,'source':str(base/name),'kind':'pe'})
    for name in ('advapi32.dll','sechost.dll','userenv.dll'):
        files.append({'path':'c:\\windows\\system32\\'+name,'source':str(security/name),'kind':'pe'})
    files.extend([{'path':'c:\\windows\\system32\\apisetschema.dll','source':str(schema_path),'kind':'pe'},
                  {'path':'c:\\windows\\system32\\guardian-nt-syscalls.bin','source':str(table_path),'kind':'manifest'}])
    inventory=crt_runtime.references(reference,('ucrtbase.dll','vcruntime140.dll'))
    for record in inventory:files.append({'path':'c:\\windows\\system32\\'+record['name'],'source':str(Path(reference)/record['name']),'kind':'pe'})
    if application!='guardian-security-read.exe':
        files.append({'path':'c:\\app\\guardian-security-service.exe','source':str(security/'guardian-security-service.exe'),'kind':'pe'})
    if application=='guardian-security-orphan.exe':files.append({'path':'c:\\app\\guardian-security-peer.exe','source':str(security/'guardian-security-peer.exe'),'kind':'pe'})
    if application=='guardian-security-crash.exe':files.append({'path':'c:\\app\\guardian-security-fault.exe','source':str(security/'guardian-security-fault.exe'),'kind':'pe'})
    spec={'phase':'14.6','profile':'win10','root':files[0]['path'],'files':files,'nativeRuntime':native,'win32Runtime':{'apiSet':metadata,'nativeMicrosoftKernelBaseValidated':False},
          'crtRuntime':{'originalReferences':inventory,'completeWindowsCrt':False},'securityRuntime':{'ownedAdvapi32':True,'completeAdvapi32':False,'serviceAccount':'LocalService','persistentStateBytes':65536}}
    manifest=pe_volume.build(spec,build/'guest'/config/'guardian-kernel-security.gk',output)
    actual={f['path'].rsplit('\\',1)[-1]:f['sha256'] for f in manifest['files']}
    for record in inventory:nt.require(actual[record['name']]==record['sha256'],'Reference changed while bundling')
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True);e=s.add_parser('exports');e.add_argument('folder',type=Path)
    b=s.add_parser('bundle');b.add_argument('--build',type=Path,required=True);b.add_argument('--config',required=True);b.add_argument('--output',type=Path,required=True);b.add_argument('--reference',type=Path,required=True);b.add_argument('--schema-reference',type=Path,required=True);b.add_argument('--application',default='guardian-security.exe');a=p.parse_args()
    print(json.dumps(exports(a.folder) if a.command=='exports' else bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application)))
if __name__=='__main__':main()
