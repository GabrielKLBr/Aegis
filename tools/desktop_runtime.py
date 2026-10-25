"""Owned 14.7 desktop runtime. External applications and Microsoft inputs remain local."""
from pathlib import Path
import argparse,json,re
import api_set,crt_runtime,native_nt,nt_compatibility as nt,pe_volume
GDI=set('GetStockObject GetSysColor GetObjectType CreateSolidBrush CreatePen DeleteObject SelectObject CreateFontIndirectW CreateFontIndirectA CreateFontW CreateFontA GetObjectW SetTextColor GetTextColor SetBkColor GetBkColor SetBkMode GetBkMode TextOutW TextOutA GetTextExtentPoint32W GetTextExtentPoint32A GetTextMetricsW GetTextFaceW MoveToEx LineTo Rectangle IntersectClipRect GetClipBox CreateCompatibleDC DeleteDC CreateCompatibleBitmap BitBlt GetDeviceCaps GdiFlush'.split())
def exports(folder,reference):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True);base=folder/'base';crt_runtime.exports(base,reference)
    names=set(re.findall(r'extern "C" [^\n{]+?\bKb(\w+)\(', (nt.ROOT/'guest/fixtures/DesktopWin32.inl').read_text(encoding='utf-8-sig')))
    merged={line.strip().split('=')[0]:line.strip().split('=',1)[1] for line in (base/'kernelbase.def').read_text(encoding='utf-8-sig').splitlines()[2:]};merged.update({n:'Kb'+n for n in names})
    (folder/'kernelbase.def').write_text('LIBRARY kernelbase\nEXPORTS\n'+''.join('    '+n+'='+v+'\n' for n,v in sorted(merged.items())),encoding='utf-8');(folder/'kernel32.def').write_text('LIBRARY kernel32\nEXPORTS\n'+''.join('    '+n+'=kernelbase.'+n+'\n' for n in sorted(merged)),encoding='utf-8')
    unsupported=(base/'CrtUnsupported.cpp').read_text(encoding='utf-8-sig');unsupported='\n'.join(line for line in unsupported.splitlines() if not any(' Kb'+n+'()' in line for n in names));(folder/'CrtUnsupported.cpp').write_text(unsupported+'\n',encoding='utf-8')
    sources=[nt.ROOT/'guest/fixtures'/name for name in ('DesktopUser.cpp','DesktopGdi.inl','DesktopWindows.inl','DesktopDialog.inl')];functions=sorted(set(re.findall(r'extern "C" [^\n{]+?\bUi(\w+)\(', '\n'.join(p.read_text(encoding='utf-8-sig') for p in sources))))
    nt.require(GDI<=set(functions),'GDI export definitions missing')
    (folder/'user32.def').write_text('LIBRARY user32\nEXPORTS\n'+''.join('    '+n+'=Ui'+n+'\n' for n in functions),encoding='utf-8')
    for dll,selected in (('gdi32',GDI-{'GetSysColor'}),('gdi32full',GDI-{'GetSysColor'}),('dwmapi',{n for n in functions if n.startswith('Dwm')})):
        (folder/(dll+'.def')).write_text('LIBRARY '+dll+'\nEXPORTS\n'+''.join('    '+n+'=user32.'+n+'\n' for n in sorted(selected)),encoding='utf-8')
    (folder/'coverage.json').write_bytes(nt.canonical({'schema':1,'phase':'14.7','user32Exports':functions,'gdiExports':sorted(GDI-{'GetSysColor'}),'extraKernelExports':sorted(names),'completeWindowsGui':False,'callbacks':'guest CPL3 only'}));return {'user32Exports':len(functions),'kernelExports':len(merged)}
def bundle(build,config,output,reference,schema_reference,application=None,assets=None):
    build=Path(build);output=Path(output);base=build/'guest/crt'/config;desktop=build/'guest/desktop'/config;application=Path(application) if application else desktop/'guardian-desktop-demo.exe';output.parent.mkdir(parents=True,exist_ok=True)
    schema,metadata=api_set.build('win10',schema_reference,('kernelbase.dll','kernel32.dll','ntdll.dll','ucrtbase.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll','advapi32.dll'))
    schema_path=output.parent/(output.name+'-schema.dll');schema_path.write_bytes(schema);table,native=native_nt.syscall_map(desktop/'ntdll.dll','win10',False);table_path=output.parent/(output.name+'-syscalls.bin');table_path.write_bytes(table)
    files=[{'path':'c:\\app\\'+application.name.lower(),'source':str(application),'kind':'pe'}]
    for name in ('guardian-runtime-api.dll',):files.append({'path':'c:\\windows\\system32\\'+name,'source':str(base/name),'kind':'pe'})
    for name in ('ntdll.dll','kernel32.dll','kernelbase.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll'):files.append({'path':'c:\\windows\\system32\\'+name,'source':str(desktop/name),'kind':'pe'})
    files.extend([{'path':'c:\\windows\\system32\\apisetschema.dll','source':str(schema_path),'kind':'pe'},{'path':'c:\\windows\\system32\\guardian-nt-syscalls.bin','source':str(table_path),'kind':'manifest'}])
    imports=nt.InventoryPe(application).imports();names=('msvcrt.dll',) if 'msvcrt.dll' in imports else ('ucrtbase.dll','vcruntime140.dll');inventory=crt_runtime.references(reference,names)
    for record in inventory:files.append({'path':'c:\\windows\\system32\\'+record['name'],'source':str(Path(reference)/record['name']),'kind':'pe'})
    if 'advapi32.dll' in imports:files.append({'path':'c:\\windows\\system32\\advapi32.dll','source':str(build/'guest/security'/config/'advapi32.dll'),'kind':'pe'})
    for path in assets or []:files.append({'path':'c:\\app\\'+Path(path).name.lower(),'source':str(path),'kind':'data'})
    spec={'phase':'14.7','profile':'win10','root':files[0]['path'],'files':files,'nativeRuntime':native,'win32Runtime':{'apiSet':metadata,'nativeMicrosoftKernelBaseValidated':False},'crtRuntime':{'originalReferences':inventory,'completeWindowsCrt':False},'securityRuntime':{'ownedAdvapi32':True,'completeAdvapi32':False,'persistentStateBytes':65536},'desktopRuntime':{'width':1280,'height':720,'windows':32,'hostCallbacks':False,'completeWindowsGui':False}}
    manifest=pe_volume.build(spec,build/'guest'/config/'guardian-kernel-desktop.gk',output);actual={f['path'].rsplit('\\',1)[-1]:f['sha256'] for f in manifest['files']}
    for record in inventory:nt.require(actual[record['name']]==record['sha256'],'Reference changed while bundling')
    return manifest
def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True);e=s.add_parser('exports');e.add_argument('folder',type=Path);e.add_argument('--reference',type=Path,required=True)
    b=s.add_parser('bundle');b.add_argument('--build',type=Path,required=True);b.add_argument('--config',required=True);b.add_argument('--output',type=Path,required=True);b.add_argument('--reference',type=Path,required=True);b.add_argument('--schema-reference',type=Path,required=True);b.add_argument('--application',type=Path);b.add_argument('--asset',type=Path,action='append',default=[]);a=p.parse_args()
    print(json.dumps(exports(a.folder,a.reference) if a.command=='exports' else bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,a.asset)))
if __name__=='__main__':main()
