"""Phase 14.9 owned networking, guest TLS and cryptographic libraries."""
from pathlib import Path
import argparse,ipaddress,json,re,struct
import api_set,component_runtime,crt_runtime,native_nt,nt_compatibility as nt,pe_volume

DLL_EXPORTS={
 'ws2_32':'WSAStartup WSACleanup WSAGetLastError WSASetLastError socket WSASocketW closesocket connect send recv WSASend WSARecv WSAGetOverlappedResult ioctlsocket setsockopt getsockopt shutdown getpeername getsockname htons ntohs htonl ntohl inet_addr InetPtonW getaddrinfo freeaddrinfo WSAPoll',
 'mswsock':'TransmitFile',
 'dnsapi':'DnsQuery_A DnsQuery_W DnsRecordListFree DnsFree',
 'iphlpapi':'GetAdaptersAddresses',
 'bcrypt':'BCryptOpenAlgorithmProvider BCryptCloseAlgorithmProvider BCryptGetProperty BCryptSetProperty BCryptCreateHash BCryptHashData BCryptFinishHash BCryptDestroyHash BCryptGenRandom BCryptGenerateSymmetricKey BCryptDestroyKey BCryptEncrypt BCryptDecrypt BCryptGenerateKeyPair BCryptFinalizeKeyPair BCryptImportKeyPair BCryptExportKey BCryptSignHash BCryptVerifySignature',
 'ncrypt':'NCryptOpenStorageProvider NCryptCreatePersistedKey NCryptFinalizeKey NCryptOpenKey NCryptFreeObject NCryptDeleteKey NCryptSetProperty NCryptGetProperty NCryptSignHash NCryptVerifySignature NCryptExportKey',
 'crypt32':'CertCreateCertificateContext CertDuplicateCertificateContext CertFreeCertificateContext CertOpenStore CertOpenSystemStoreW CertCloseStore CertAddCertificateContextToStore CertAddEncodedCertificateToStore CertEnumCertificatesInStore CertGetCertificateChain CertFreeCertificateChain CertVerifyCertificateChainPolicy',
 'secur32':'AcquireCredentialsHandleW FreeCredentialsHandle FreeContextBuffer QuerySecurityPackageInfoW InitializeSecurityContextW DeleteSecurityContext QueryContextAttributesW EncryptMessage DecryptMessage',
 'winhttp':'WinHttpOpen WinHttpConnect WinHttpOpenRequest WinHttpSetTimeouts WinHttpSetStatusCallback WinHttpSetOption WinHttpQueryOption WinHttpAddRequestHeaders WinHttpSendRequest WinHttpReceiveResponse WinHttpWriteData WinHttpReadData WinHttpQueryDataAvailable WinHttpQueryHeaders WinHttpCloseHandle WinHttpSetCredentials',
 'wininet':'InternetOpenW InternetConnectW HttpOpenRequestW HttpSendRequestW HttpQueryInfoW InternetReadFile InternetQueryDataAvailable InternetSetOptionW InternetCloseHandle InternetOpenUrlW',
}
DLLS=tuple(DLL_EXPORTS)+('guardianinet',)
MAGIC=0x3154454E494147

def policy(spec):
    allowed={'mode','endpoints','requestLimit','byteLimit'};nt.require(isinstance(spec,dict) and not spec.keys()-allowed,'Unknown policy fields')
    modes={'offline':0,'simulated':1,'lab':2,'authorized':3};nt.require(spec.get('mode','offline') in modes,'Unknown network mode');mode=modes[spec.get('mode','offline')]
    entries=spec.get('endpoints',[]);nt.require(isinstance(entries,list) and len(entries)<=8 and (mode!=0 or not entries),'Endpoint count rejected')
    requests=spec.get('requestLimit',32768);budget=spec.get('byteLimit',2<<20);nt.require(type(requests) is int and 16<=requests<=65536 and type(budget) is int and 4096<=budget<=16<<20,'Policy quotas rejected')
    data=bytearray(struct.pack('<Q6I',MAGIC,1,800,mode,len(entries),requests,budget));seen=set()
    for entry in entries:
        nt.require(isinstance(entry,dict) and not entry.keys()-{'name','address','port','transport','service'},'Unknown endpoint fields');name=entry.get('name','');nt.require(isinstance(name,str) and len(name)<=63 and all(re.fullmatch('[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?',label) for label in name.split('.')),'Endpoint name rejected');address=ipaddress.IPv4Address(entry['address']);port=entry['port'];kind={'tcp':1,'udp':2}.get(entry.get('transport','tcp'));service={'echo':0,'http':1,'stall':2}.get(entry.get('service','echo'));nt.require(type(port) is int and 1<=port<=65535 and kind and service is not None,'Endpoint protocol rejected')
        nt.require(mode!=2 or str(address)=='127.0.0.1','Lab endpoints must be explicit loopback');nt.require(mode!=1 or address in ipaddress.IPv4Network('198.18.0.0/24') and int(address.packed[3])>0,'Simulated address rejected')
        nt.require(mode!=3 or address.is_global,'Authorized address must be public; host applies additional exclusions');identity=(name,port,kind);nt.require(identity not in seen,'Duplicate endpoint');seen.add(identity)
        data+=struct.pack('<64s16s4I',name.encode(),address.packed+b'\0'*12,2,port,kind,service)
    for _ in range(8-len(entries)):data+=struct.pack('<64s16s4I',b'',b'\0'*16,2,0,1,0)
    nt.require(len(data)==800,'Policy layout changed');return bytes(data)

def exports(output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True);groups={key:set(value.split()) for key,value in DLL_EXPORTS.items()};names=set().union(*groups.values())
    code='\n'.join(path.read_text(encoding='utf-8') for path in (nt.ROOT/'guest/fixtures').glob('Internet*.cpp'));implemented=set(re.findall(r'extern "C" [^\n{]+?\bNet(\w+)\(',code));nt.require(names<=implemented,'Missing internet exports: '+','.join(sorted(names-implemented)))
    ordinals=json.loads((nt.ROOT/'config/internet-ordinals.json').read_bytes())
    for dll in DLLS:
        selected=names if dll=='guardianinet' else groups[dll]
        lines=[]
        for name in sorted(selected):
            ordinal=ordinals.get(dll,{}).get(name);lines.append('    '+name+'='+('Net'+name if dll=='guardianinet' else 'guardianinet.'+name)+(' @'+str(ordinal) if ordinal else '')+'\n')
        (output/(dll+'.def')).write_text('LIBRARY '+dll+'\nEXPORTS\n'+''.join(lines),encoding='utf-8')
    proof={'phase':'14.9','dlls':{key:sorted(value) for key,value in groups.items()},'implementedExports':len(names),'hostTls':False,'hostKeys':False,'completeWindowsNetworking':False,'tlsVersions':['1.2'],'suites':['ECDHE-ECDSA-AES128-GCM-SHA256','ECDHE-RSA-AES128-GCM-SHA256']};(output/'coverage.json').write_bytes(nt.canonical(proof));return proof

def bundle(build,config,output,reference,schema_reference,application=None,extras=(),assets=(),kernel=None,presentation=False,native_gui=False,native_user32=False):
    build=Path(build);output=Path(output);folder=build/'guest/internet'/config;application=Path(application) if application else folder/'guardian-internet-crypto.exe';output.parent.mkdir(parents=True,exist_ok=True)
    if native_gui:extras=(*extras,build/'guest/native-gui'/config/'guardian-native-gui.dll')
    hosts=('kernelbase.dll','kernel32.dll','ntdll.dll','ucrtbase.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll','advapi32.dll',*(d+'.dll' for d in (*component_runtime.DLLS,*DLLS)))
    schema,metadata=api_set.build('win10',schema_reference,hosts);schema_path=output.parent/(output.name+'-schema.dll');schema_path.write_bytes(schema)
    table,native=native_nt.syscall_map(build/'guest/desktop'/config/'ntdll.dll','win10',False);table_path=output.parent/(output.name+'-syscalls.bin');table_path.write_bytes(table)
    files=[{'path':'c:\\app\\'+application.name.lower(),'source':str(application),'kind':'pe'}]
    def add(path,source,kind='pe'):files.append({'path':path,'source':str(source),'kind':kind})
    for name in ('ntdll.dll','kernel32.dll','kernelbase.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll'):add('c:\\windows\\system32\\'+name,build/('guest/native-user32' if native_user32 and name in ('user32.dll','win32u.dll') else 'guest/desktop')/config/name)
    add('c:\\windows\\system32\\guardian-runtime-api.dll',build/'guest/crt'/config/'guardian-runtime-api.dll');add('c:\\windows\\system32\\advapi32.dll',build/'guest/security'/config/'advapi32.dll')
    for dll in component_runtime.DLLS:add('c:\\windows\\system32\\'+dll+'.dll',build/'guest/components'/config/(dll+'.dll'))
    for dll in DLLS:add('c:\\windows\\system32\\'+dll+'.dll',folder/(dll+'.dll'))
    for path in extras:add('c:\\app\\'+Path(path).name.lower(),path)
    for path in assets:add('c:\\app\\'+Path(path).name.lower(),path,'data')
    reference_names=('msvcrt.dll',) if 'msvcrt.dll' in nt.InventoryPe(application).imports() else ('ucrtbase.dll','vcruntime140.dll');inventory=crt_runtime.references(reference,reference_names)
    for record in inventory:add('c:\\windows\\system32\\'+record['name'],Path(reference)/record['name'])
    add('c:\\windows\\system32\\apisetschema.dll',schema_path);add('c:\\windows\\system32\\guardian-nt-syscalls.bin',table_path,'manifest')
    available={f['path'].rsplit('\\',1)[-1]:f for f in files if f['kind']=='pe'};selected={application.name.lower(),'ntdll.dll','kernel32.dll','kernelbase.dll','guardian-runtime-api.dll','apisetschema.dll',*reference_names,*(Path(path).name.lower() for path in extras)};pending=list(selected)
    while pending:
        name=pending.pop();record=available.get(name);nt.require(record is not None,'Outside owned network corpus: '+name);pe=nt.InventoryPe(Path(record['source']));dependencies=set(pe.imports())|{e['forwarder'].rsplit('.',1)[0].lower()+'.dll' for e in pe.export_records() if e['forwarder']}
        for dependency in dependencies:
            if dependency.startswith(('api-ms-','ext-ms-')):continue
            if dependency not in selected:selected.add(dependency);pending.append(dependency)
    files=[file for file in files if file['kind']!='pe' or file['path'].rsplit('\\',1)[-1] in selected];nt.require(sum(file['kind']=='pe' for file in files)<=24,'Network module budget exceeded')
    network_exports={dll+'.dll':{name for record in nt.InventoryPe(folder/(dll+'.dll')).export_records() for name in (*record['names'],'#'+str(record['ordinal']))} for dll in DLLS}
    for file in files:
        if file['kind']=='pe':
            for imported in nt.InventoryPe(Path(file['source'])).import_records():
                if imported['module'] in network_exports:nt.require(imported['symbol'] in network_exports[imported['module']],'Missing owned network export: '+imported['module']+'!'+imported['symbol'])
    spec={'phase':'14.9','profile':'win10','root':files[0]['path'],'files':files,'nativeRuntime':native,'win32Runtime':{'apiSet':metadata},'crtRuntime':{'originalReferences':inventory,'completeWindowsCrt':False},'securityRuntime':{'ownedAdvapi32':True,'persistentStateBytes':65536},'desktopRuntime':{'width':1280,'height':720,'hostCallbacks':False,'completeWindowsGui':False},'componentRuntime':{'hostActivation':False,'completeCOM':False,'completeNDR':False},'internetRuntime':{'hostTls':False,'hostCredentials':False,'defaultNetwork':'offline','tlsVersions':['1.2'],'socketQuota':16,'payload':2048,'completeWindowsNetworking':False}}
    if presentation:spec['phase']='12F';spec['presentationRuntime']={'independentWindows':True,'guestCallbacks':True,'hostHandlesInGuest':False,'logicalDpi':96,'maxClient':[1280,720],'frameByteLimit':16<<20}
    if native_gui:
        spec['phase']='14.10A';spec['nativeGuiRuntime']={'backend':'win32-gdi-helper','protocol':1,'helperSha256':nt.sha(nt.regular(build/config/'guardian-native-gui-host.exe',16<<20)),'hostHandlesInGuest':False,'guestPixelsRequired':False,'standardUser32Passthrough':False,'phase14_10BStarted':False}
    if native_user32:
        nt.require(not native_gui and not presentation,'Native USER32 mode is exclusive')
        spec['phase']='14.10B';spec['nativeGuiRuntime']={'backend':'win32-user32-helper','protocol':2,'helperSha256':nt.sha(nt.regular(build/config/'guardian-native-user-host.exe',16<<20)),'hostHandlesInGuest':False,'guestPixelsRequired':False,'standardUser32Subset':True,'completeWindowsGui':False,'logicalDpi':96,'phase14_10CStarted':False}
    return pe_volume.build(spec,kernel or build/'guest'/config/'guardian-kernel-internet.gk',output)

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True);e=s.add_parser('exports');e.add_argument('output',type=Path);n=s.add_parser('policy');n.add_argument('input',type=Path);n.add_argument('output',type=Path)
    b=s.add_parser('bundle');b.add_argument('--build',type=Path,required=True);b.add_argument('--config',required=True);b.add_argument('--output',type=Path,required=True);b.add_argument('--reference',type=Path,required=True);b.add_argument('--schema-reference',type=Path,required=True);b.add_argument('--application',type=Path);b.add_argument('--extra',type=Path,action='append',default=[]);b.add_argument('--asset',type=Path,action='append',default=[])
    a=p.parse_args()
    if a.command=='policy':a.output.write_bytes(policy(json.loads(a.input.read_bytes())));print(json.dumps({'output':str(a.output),'bytes':800}));return
    print(json.dumps(exports(a.output) if a.command=='exports' else bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,a.extra,a.asset)))
if __name__=='__main__':main()
