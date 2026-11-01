"""Package the owned desktop runtime and stable phase 14.8 evidence only."""
from pathlib import Path
import argparse,json,shutil,zipfile
import nt_compatibility as nt
from validate_phase14_8 import REQUIRED,TOTAL_TESTS
COMPONENT_APPS=('client','client-crash','client-timeout','client-persist','controls','controls-cancel','rpc','child','server')
from component_runtime import DLLS as COMPONENT_DLLS
APPS=('demo','threads','settings','nativeprobe')
DLLS=('ntdll.dll','kernelbase.dll','kernel32.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--debug',type=Path,required=True);p.add_argument('--regression',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    for config,suite,folder in (('Release','acceptance',a.release),('Debug','acceptance',a.debug),('Release','regression',a.regression)):
        proof=json.loads((folder/'validation.json').read_bytes());covers=suite=='acceptance' and config=='Release' and proof['suite']=='regression';expected=TOTAL_TESTS if proof['suite']=='regression' else len(REQUIRED)
        nt.require(proof['phase']=='14.8' and proof['status']=='PASS' and (proof['suite']==suite or covers) and proof['configuration']==config and proof['tests']==expected and not proof['failures'] and not proof['skipped'] and proof['stableSourcesAndBinaries'],'Stable accepted evidence required')
        if suite=='acceptance':nt.require(REQUIRED<=set(proof['testNames']) if covers else REQUIRED==set(proof['testNames']),'Acceptance identities changed')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((a.build/name).read_bytes())==digest,'Binary changed: '+name)
        for name,digest in proof['flappyInputs'].items():nt.require(nt.sha(Path(name).read_bytes())==digest,'External input changed')
    out=a.output;archive=out.with_name(out.name+'.zip');nt.require(not out.exists() and not archive.exists(),'New package path required');out.mkdir(parents=True)
    def copy(source,name):
        dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-desktop','guardian-whp-memory','desktop_core_tests','desktop_viewer_tests','desktop_windows_demo','desktop_windows_threads','component_core_tests','component_windows_client','component_windows_controls','component_windows_rpc'):copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-desktop.gk','guest/'+config+'/guardian-kernel-desktop.gk')
        copy(a.build/'guest/crt'/config/'guardian-runtime-api.dll','guest/crt/'+config+'/guardian-runtime-api.dll')
        copy(a.build/'guest'/config/'guardian-kernel-components.gk','guest/'+config+'/guardian-kernel-components.gk')
        copy(a.build/'guest/security'/config/'advapi32.dll','guest/security/'+config+'/advapi32.dll')
        for name in (*(dll+'.dll' for dll in COMPONENT_DLLS),*(('guardian-component-'+app+'.exe') for app in COMPONENT_APPS),'guardian-component-inproc.dll'):copy(a.build/'guest/components'/config/name,'guest/components/'+config+'/'+name)
        for name in (*DLLS,*(('guardian-desktop-'+name+'.exe') for name in APPS)):copy(a.build/'guest/desktop'/config/name,'guest/desktop/'+config+'/'+name)
        for name in ('validation.json','ctest.log','ctest.xml'):copy(evidence/name,'validation/'+config+'/'+name)
    for name in ('validation.json','ctest.log','ctest.xml'):copy(a.regression/name,'validation/regression-Release/'+name)
    copy(a.build/'component-generated/coverage.json','validation/component-export-coverage.json');copy(a.build/'desktop-generated/coverage.json','validation/desktop-export-coverage.json');copy(nt.ROOT/'config/crt-references.json','config/crt-references.json')
    for name in ('desktop_runtime.py','crt_runtime.py','win32_runtime.py','native_nt.py','api_set.py','nt_compatibility.py','inspect_gui_compatibility.py','pe_volume.py','component_runtime.py','smoke_phase14_8.py'):copy(nt.ROOT/'tools'/name,'tools/'+name)
    for name in ('DesktopIntegrationTests.py','DesktopFlappyTests.py','ComponentIntegrationTests.py'):copy(nt.ROOT/'test'/name,'test/'+name)
    copy(nt.ROOT/'docs/PHASE-14.7.md','PHASE-14.7.md')
    copy(nt.ROOT/'docs/PHASE-14.8.md','PHASE-14.8.md')
    (out/'README.txt').write_text('Aegis 14.8 - MiniNT x64 components and desktop\n\nRequires Windows x64, WHP and NX active and Visual C++ runtime on host.\nContains kernel and own DLLs, test applications and evidence.\nProgram windows remain in MiniNT, shown in a viewer.\nThe --interactive parameter enables entry only in this host window.\nThe integration of a native window by guest window belongs to phase 12F.\n\nIt does not include Microsoft input DLLs, Flappy, ISOs, personal disks or GPVs.\nProvides its exact local references (config/crt-references.json), schema\nWin10 accepted and the original Flappy accompanied by settings.txt and icon.\n\nFor the COM client, in a new output folder in PowerShell:\npython -B tools/component_runtime.py bundle --build . --config Release --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --extra guest/components/Release/guardian-component-inproc.dll --extra guest/components/Release/guardian-component-server.exe --output componentes\n.\\Release\\guardian-desktop.exe guest/Release/guardian-kernel-components.gk componentes/runtime.gpv componentes/manifest.json componentes/binding.gnt sessao-componentes\n\nAll servers and objects of this example run on MiniNT.\nCLI accepts --application and --extra to select other programs and DLLs.\n\nFor the previous graphic demo, in a new output folder:\npython -B tools/desktop_runtime.py bundle --build . --config Release --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --output app\n.\\Release\\guardian-desktop.exe guest/Release/guardian-kernel-desktop.gk app/runtime.gpv app/manifest.json app/binding.gnt sessao --interactive\n\nThe bundle pattern opens the demo application itself. For Flappy,\nadd --application FLAPPY-PATH --asset CAMINHO-SETTINGS --asset CAMINHO-ICON\nin the creation of the bundle. The external executable never runs directly in the host.\n\nTo check the package in both configurations:\npython -B tools/smoke_phase14_8.py --package . --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --application FLAPPY-PATH --output NEW-EVIDENCE\n\nsecurity-state.bin guard 64 KiB from the guest disk and can be provided to a\nnew session with --state ARQUIVO. Do not change or publish data from your session.\nRead PHASE-14.8.md and PHASE-14.7.md for quotas, support and limitations.\n14.9 and 12F are not part of this delivery.\n',encoding='utf-8')
    files={path.relative_to(out).as_posix():nt.sha(path.read_bytes()) for path in sorted(out.rglob('*')) if path.is_file()};(out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.8','files':files,'acceptance':{'Release':len(REQUIRED),'Debug':len(REQUIRED)},'regressionTests':TOTAL_TESTS,'microsoftInputBinariesIncluded':False,'externalApplicationIncluded':False,'completeWindowsGui':False,'phase12FStarted':False,'phase14_8Started':True,'phase14_9Started':False,'completeCOM':False,'completeNDR':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}))
if __name__=='__main__':main()
