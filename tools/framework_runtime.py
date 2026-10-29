"""Prepare phase 14.11 guest runtimes. Inputs are inspected, never run on the host."""
from pathlib import Path
import argparse
import json
import internet_runtime
import nt_compatibility as nt
import struct
import subprocess

WINRT_EXPORTS=('RoInitialize','RoUninitialize','RoGetActivationFactory','RoActivateInstance',
    'WindowsCreateString','WindowsCreateStringReference','WindowsDuplicateString','WindowsDeleteString',
    'WindowsGetStringRawBuffer','WindowsGetStringLen','WindowsIsStringEmpty',
    'WindowsStringHasEmbeddedNull','WindowsCompareStringOrdinal')

def exports(base,output):
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(Path(base).read_text(encoding='utf-8')+'\n'+''.join('    '+name+'=Fw'+name+'\n' for name in WINRT_EXPORTS),encoding='utf-8')

def copy_exports(base,kernel,output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    def table(path):return dict(line.strip().split('=',1) for line in Path(path).read_text(encoding='utf-8-sig').splitlines()[2:] if '=' in line)
    a=table(base);b=table(kernel)
    for name in ('CopyFileW','CopyFileA'):a[name]='Fw'+name;b[name]='kernelbase.'+name
    for name,rows in (('kernelbase',a),('kernel32',b)):
        (output/(name+'.def')).write_text('LIBRARY '+name+'\nEXPORTS\n'+''.join('    '+key+'='+value+'\n' for key,value in sorted(rows.items())),encoding='utf-8')

def bundle(build, config, output, reference, schema_reference, application, extras=(), assets=(), profile='win10'):
    for file in (application,*extras):
        result=inspect(build,config,file)
        nt.require(result['status']=='PASS','Unsupported framework input: '+result['diagnostic'])
    return internet_runtime.bundle(Path(build),config,Path(output),Path(reference),Path(schema_reference),
        Path(application),extras,assets,kernel=Path(build)/'guest'/config/'guardian-kernel-frameworks.gk',
        frameworks=True,profile=profile)

def inspect(build,config,application):
    application=Path(application)
    if application.suffix.lower() in ('.msi','.msix','.appx','.winmd'):
        return {'status':'UNSUPPORTED','diagnostic':'package-or-metadata-format-not-implemented: '+application.suffix.lower(),'inputExecutedOnHost':False}
    try:pe=nt.InventoryPe(application)
    except (RuntimeError,OSError,ValueError,struct.error) as error:
        return {'status':'UNSUPPORTED','diagnostic':'unsupported-pe-format: '+str(error),'inputExecutedOnHost':False}
    data=pe.data;offset=struct.unpack_from('<I',data,60)[0]+24
    managed=struct.unpack_from('<I',data,offset+116+14*8)[0]!=0
    if managed:
        probe=Path(build)/config/'guardian-cli-inspect.exe'
        result=subprocess.run([str(probe),str(Path(application).resolve())],capture_output=True,encoding='utf-8',timeout=15)
        if result.stdout.strip():return json.loads(result.stdout)
        return {'status':'UNSUPPORTED','diagnostic':result.stderr.strip() or 'cli-inspection-failed','inputExecutedOnHost':False}
    unsupported={'clr.dll':'.NET Framework CLR','coreclr.dll':'CoreCLR','msi.dll':'Windows Installer MSI','setupapi.dll':'SetupAPI installation','microsoft.ui.xaml.dll':'WinUI 3','windowsappruntime.dll':'Windows App SDK'}
    for dll in pe.imports():
        if dll in unsupported:return {'status':'UNSUPPORTED','diagnostic':'framework-module-not-implemented: '+unsupported[dll], 'inputExecutedOnHost':False}
    return {'status':'PASS','diagnostic':'native-pe-requires-runtime-import-validation','inputExecutedOnHost':False,'runtimeChecksRequired':True}

def portable_bundle(build,config,output,reference,schema_reference,application,mode='install',profile='win10'):
    import nt_compatibility as nt
    import pe_volume
    build=Path(build);output=Path(output);application=Path(application)
    nt.require(mode in ('install','resume','uninstall'),'Unknown portable operation')
    source=pe_volume.path('c:\\app\\'+application.name.lower())
    nt.require(source.endswith('.exe'),'Portable payload must be an x64 executable')
    nt.require(not (output.parent/'portable.cfg').exists(),'Use a fresh portable bundle parent')
    output.parent.mkdir(parents=True,exist_ok=True);config_file=output.parent/'portable.cfg';config_file.write_bytes(source.encode('ascii'))
    return bundle(build,config,output,reference,schema_reference,build/'guest/frameworks'/config/('guardian-framework-'+mode+'.exe'),(application,),(config_file,),profile)

def main():
    import sys
    if len(sys.argv)==4 and sys.argv[1]=='exports':exports(sys.argv[2],sys.argv[3]);return
    if len(sys.argv)==5 and sys.argv[1]=='copy-exports':copy_exports(sys.argv[2],sys.argv[3],sys.argv[4]);return
    if len(sys.argv)>1 and sys.argv[1]=='inspect':
        p=argparse.ArgumentParser(description='Static framework diagnosis; never executes the input')
        p.add_argument('application',type=Path);p.add_argument('--build',type=Path,required=True);p.add_argument('--config',default='Release');a=p.parse_args(sys.argv[2:])
        result=inspect(a.build,a.config,a.application);print(json.dumps(result));raise SystemExit(0 if result['status']=='PASS' else 3)
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True);p.add_argument('--config',default='Release')
    p.add_argument('--output',type=Path,required=True);p.add_argument('--reference',type=Path,required=True)
    p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--application',type=Path,required=True)
    p.add_argument('--extra',type=Path,action='append',default=[]);p.add_argument('--asset',type=Path,action='append',default=[])
    p.add_argument('--profile',choices=('win7','win10','win11'),default='win10')
    p.add_argument('--portable',choices=('install','resume','uninstall'))
    a=p.parse_args()
    if a.portable:
        p.error('--extra/--asset are not supported with --portable') if a.extra or a.asset else None
        result=portable_bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,a.portable,a.profile)
    else:result=bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,a.extra,a.asset,a.profile)
    print(json.dumps(result))
if __name__=='__main__':main()
