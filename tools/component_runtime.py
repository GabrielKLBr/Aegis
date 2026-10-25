"""Owned COM/RPC/Shell runtime; input Windows DLLs stay local and never run on the host."""
from pathlib import Path
import argparse,json,re
import api_set,crt_runtime,desktop_runtime,native_nt,nt_compatibility as nt,pe_volume

DLL_EXPORTS={
 'ole32':'CoInitialize CoInitializeEx CoUninitialize CoGetApartmentType OleInitialize OleUninitialize CoGetClassObject CoCreateInstance CoRegisterClassObject CoRevokeClassObject CoFreeUnusedLibraries CoFreeUnusedLibrariesEx CoWaitForMultipleHandles CoMarshalInterface CoUnmarshalInterface CoReleaseMarshalData CoMarshalInterThreadInterfaceInStream CoGetInterfaceAndReleaseStream CreateStreamOnHGlobal CoTaskMemAlloc CoTaskMemRealloc CoTaskMemFree CoCreateGuid CLSIDFromString IIDFromString StringFromGUID2 StringFromCLSID StringFromIID PropVariantClear PropVariantCopy',
 'oleaut32':'SysAllocString SysAllocStringLen SysFreeString SysStringLen SysStringByteLen SysReAllocStringLen VariantInit VariantClear VariantCopy VariantChangeType SafeArrayCreateVector SafeArrayDestroy SafeArrayGetDim SafeArrayGetLBound SafeArrayGetUBound SafeArrayGetVartype SafeArrayAccessData SafeArrayUnaccessData SafeArrayPutElement SafeArrayGetElement SafeArrayCopy',
 'rpcrt4':'RpcStringBindingComposeW RpcStringFreeW RpcBindingFromStringBindingW RpcBindingFree I_RpcGetBuffer I_RpcFreeBuffer I_RpcSendReceive RpcServerUseProtseqEpW RpcServerRegisterIf RpcServerListen RpcMgmtStopServerListening RpcServerUnregisterIf UuidFromStringW UuidToStringW UuidCreate UuidEqual',
 'propsys':'PSCreateMemoryPropertyStore PropVariantClear PropVariantCopy InitPropVariantFromString',
 'shell32':'SHGetFolderPathW SHGetKnownFolderPath SHGetSpecialFolderPathW ShellExecuteExW ShellExecuteW SHFileOperationW',
 'shlwapi':'PathFindFileNameW PathFindExtensionW PathIsRelativeW PathCanonicalizeW PathCombineW PathAppendW PathRemoveFileSpecW PathAddBackslashW PathFileExistsW StrCmpIW StrStrIW',
 'comctl32':'InitCommonControls InitCommonControlsEx',
 'comdlg32':'GetOpenFileNameW GetSaveFileNameW CommDlgExtendedError',
 'uxtheme':'IsThemeActive IsAppThemed OpenThemeData CloseThemeData DrawThemeBackground DrawThemeText SetWindowTheme',
}
OLEAUT_ORDINALS={'SysAllocString':2,'SysAllocStringLen':4,'SysReAllocStringLen':5,'SysFreeString':6,'SysStringLen':7,'VariantInit':8,'VariantClear':9,'VariantCopy':10,'VariantChangeType':12,'SafeArrayDestroy':16,'SafeArrayGetDim':17,'SafeArrayGetUBound':19,'SafeArrayGetLBound':20,'SafeArrayAccessData':23,'SafeArrayUnaccessData':24,'SafeArrayGetElement':25,'SafeArrayPutElement':26,'SafeArrayCopy':27,'SafeArrayGetVartype':77,'SysStringByteLen':149,'SafeArrayCreateVector':411}
DLLS=tuple(DLL_EXPORTS)+('combase',)

def exports(output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    groups={k:set(v.split()) for k,v in DLL_EXPORTS.items()}
    core=set().union(*(groups[k] for k in ('ole32','oleaut32','rpcrt4','propsys')))
    ui=set().union(*(groups[k] for k in ('shell32','shlwapi','comctl32','comdlg32','uxtheme')))
    sources=['ComponentCom.cpp','ComponentAutomation.inl','ComponentMarshal.inl','ComponentGuids.inl','ComponentRpc.inl','ComponentProperties.cpp','ComponentDesktop.cpp','ComponentShell.inl','ComponentDialogs.inl','ComponentTheme.inl']
    implementations=set(re.findall(r'extern "C" [^\n{]+?\bCmp(\w+)\(', '\n'.join((nt.ROOT/'guest/fixtures'/n).read_text(encoding='utf-8') for n in sources)))
    nt.require(core|ui<=implementations,'Component exports missing implementation')
    for dll in DLLS:
        names=core if dll=='combase' else ui if dll=='shell32' else groups[dll]
        target='combase' if dll in ('ole32','oleaut32','rpcrt4','propsys') else 'shell32'
        (output/(dll+'.def')).write_text('LIBRARY '+dll+'\nEXPORTS\n'+''.join('    '+name+'='+(('Cmp'+name) if dll in ('combase','shell32') else target+'.'+name)+(' @'+str(OLEAUT_ORDINALS[name]) if dll=='oleaut32' else '')+'\n' for name in sorted(names)),encoding='utf-8')
    (output/'coverage.json').write_bytes(nt.canonical({'phase':'14.8','dlls':{k:sorted(v) for k,v in groups.items()},'transport':'bounded guest IPC, scalar/BSTR IDispatch and explicit RPC dispatch tables','hostActivation':False,'completeCOM':False,'completeNDR':False}))
    return {'dlls':len(DLLS),'implementedExports':len(core|ui)}

def bundle(build,config,output,reference,schema_reference,application=None,extras=(),assets=()):
    build=Path(build);output=Path(output);folder=build/'guest/components'/config
    if application is None:
        supplied={Path(p).resolve() for p in extras}
        extras=(*extras,*(p for p in (folder/'guardian-component-inproc.dll',folder/'guardian-component-server.exe') if p.resolve() not in supplied))
    application=Path(application) if application else folder/'guardian-component-client.exe'
    output.parent.mkdir(parents=True,exist_ok=True)
    schema,metadata=api_set.build('win10',schema_reference,('kernelbase.dll','kernel32.dll','ntdll.dll','ucrtbase.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll','advapi32.dll',*(d+'.dll' for d in DLLS)))
    schema_path=output.parent/(output.name+'-schema.dll');schema_path.write_bytes(schema)
    table,native=native_nt.syscall_map(build/'guest/desktop'/config/'ntdll.dll','win10',False)
    table_path=output.parent/(output.name+'-syscalls.bin');table_path.write_bytes(table)
    files=[{'path':'c:\\app\\'+application.name.lower(),'source':str(application),'kind':'pe'}]
    def add(path,source,kind='pe'):files.append({'path':path,'source':str(source),'kind':kind})
    for name in ('ntdll.dll','kernel32.dll','kernelbase.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll'):add('c:\\windows\\system32\\'+name,build/'guest/desktop'/config/name)
    add('c:\\windows\\system32\\guardian-runtime-api.dll',build/'guest/crt'/config/'guardian-runtime-api.dll')
    add('c:\\windows\\system32\\advapi32.dll',build/'guest/security'/config/'advapi32.dll')
    for dll in DLLS:add('c:\\windows\\system32\\'+dll+'.dll',folder/(dll+'.dll'))
    for path in extras:add('c:\\app\\'+Path(path).name.lower(),path)
    for path in assets:add('c:\\app\\'+Path(path).name.lower(),path,'data')
    inventory=crt_runtime.references(reference,('ucrtbase.dll','vcruntime140.dll'))
    for record in inventory:add('c:\\windows\\system32\\'+record['name'],Path(reference)/record['name'])
    add('c:\\windows\\system32\\apisetschema.dll',schema_path)
    add('c:\\windows\\system32\\guardian-nt-syscalls.bin',table_path,'manifest')
    # Resolve static imports/forwarders and include explicitly supplied dynamic servers.
    available={f['path'].rsplit('\\',1)[-1]:f for f in files if f['kind']=='pe'}
    selected={application.name.lower(),'ntdll.dll','kernel32.dll','kernelbase.dll','guardian-runtime-api.dll','apisetschema.dll','ucrtbase.dll',*(Path(p).name.lower() for p in extras)}
    pending=list(selected)
    while pending:
        name=pending.pop();record=available.get(name)
        nt.require(record is not None,'Dependency is outside the owned corpus: '+name)
        pe=nt.InventoryPe(Path(record['source']))
        dependencies=set(pe.imports())|{e['forwarder'].rsplit('.',1)[0].lower()+'.dll' for e in pe.export_records() if e['forwarder']}
        for dependency in dependencies:
            if dependency.startswith(('api-ms-','ext-ms-')):continue
            if dependency not in selected:selected.add(dependency);pending.append(dependency)
    files=[f for f in files if f['kind']!='pe' or f['path'].rsplit('\\',1)[-1] in selected]
    nt.require(sum(f['kind']=='pe' for f in files)<=24,'Component module budget exceeded')
    spec={'phase':'14.8','profile':'win10','root':files[0]['path'],'files':files,'nativeRuntime':native,'win32Runtime':{'apiSet':metadata,'nativeMicrosoftKernelBaseValidated':False},'crtRuntime':{'originalReferences':inventory,'completeWindowsCrt':False},'securityRuntime':{'ownedAdvapi32':True,'persistentStateBytes':65536},'desktopRuntime':{'width':1280,'height':720,'windows':32,'hostCallbacks':False,'completeWindowsGui':False},'componentRuntime':{'hostActivation':False,'completeCOM':False,'completeNDR':False,'endpoints':12,'calls':16,'payload':768}}
    return pe_volume.build(spec,build/'guest'/config/'guardian-kernel-components.gk',output)

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True)
    e=s.add_parser('exports');e.add_argument('output',type=Path)
    b=s.add_parser('bundle');b.add_argument('--build',type=Path,required=True);b.add_argument('--config',required=True);b.add_argument('--output',type=Path,required=True);b.add_argument('--reference',type=Path,required=True);b.add_argument('--schema-reference',type=Path,required=True);b.add_argument('--application',type=Path);b.add_argument('--extra',type=Path,action='append',default=[]);b.add_argument('--asset',type=Path,action='append',default=[])
    a=p.parse_args();print(json.dumps(exports(a.output) if a.command=='exports' else bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,a.extra,a.asset)))
if __name__=='__main__':main()
