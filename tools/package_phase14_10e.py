"""Package the owned desktop runtime and stable phase 14.10E evidence only."""
from pathlib import Path
import argparse,json,shutil,zipfile
import nt_compatibility as nt
from validate_phase14_10e import REQUIRED,TOTAL_TESTS
COMPONENT_APPS=('client','client-crash','client-timeout','client-persist','controls','controls-cancel','rpc','child','server')
from component_runtime import DLLS as COMPONENT_DLLS
APPS=('demo','threads','settings','nativeprobe')
DLLS=('ntdll.dll','kernelbase.dll','kernel32.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--debug',type=Path,required=True);p.add_argument('--regression',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--development-log',type=Path,action='append',default=[]);a=p.parse_args()
    for config,suite,folder in (('Release','acceptance',a.release),('Debug','acceptance',a.debug),('Release','regression',a.regression)):
        proof=json.loads((folder/'validation.json').read_bytes());covers=suite=='acceptance' and config=='Release' and proof['suite']=='regression';expected=TOTAL_TESTS if proof['suite']=='regression' else len(REQUIRED)
        nt.require(proof['phase']=='14.10E' and proof['status']=='PASS' and (proof['suite']==suite or covers) and proof['configuration']==config and proof['tests']==expected and not proof['failures'] and not proof['skipped'] and proof['stableSourcesAndBinaries'],'Stable accepted evidence required')
        if suite=='acceptance':nt.require(REQUIRED<=set(proof['testNames']) if covers else REQUIRED==set(proof['testNames']),'Acceptance identities changed')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((a.build/name).read_bytes())==digest,'Binary changed: '+name)
        for name,digest in {**proof['flappyInputs'],**proof['independentBinariesAndReceipts'],**proof['independentSources'],**proof['legalInputFiles']}.items():nt.require(nt.sha(Path(name).read_bytes())==digest,'External input changed')
    out=a.output;archive=out.with_name(out.name+'.zip');nt.require(not out.exists() and not archive.exists(),'New package path required');out.mkdir(parents=True)
    def copy(source,name):
        dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-desktop','guardian-whp-memory','desktop_core_tests','desktop_viewer_tests','desktop_windows_demo','desktop_windows_threads','window_bridge_core_tests','window_bridge_native_tests','native_gui_core_tests','native_gui_broker_tests','guardian-native-gui-host','guardian-native-user-host','guardian-native-gdi-host','native_gdi_reference','native_gdi_protocol_tests','native_gdi_broker_tests','guardian-native-controls-host','guardian-expanded-gdi-host','expanded_gdi_protocol_tests','expanded_gdi_broker_tests','expanded_gdi_reference','expanded_gdi_widgets_reference','expanded_gdi_pad_reference','expanded_gdi_layout_reference','native_controls_protocol_tests','native_controls_broker_tests','native_controls_widgets_reference','native_controls_pad_reference','native_controls_layout_reference','native_user32_protocol_tests','native_user32_broker_tests','internet_core_tests','internet_crypto_reference','internet_sockets_reference','internet_http_reference','internet_timeout_reference','internet_wininet_reference','component_core_tests','component_windows_client','component_windows_controls','component_windows_rpc'):copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-desktop.gk','guest/'+config+'/guardian-kernel-desktop.gk')
        copy(a.build/'guest/crt'/config/'guardian-runtime-api.dll','guest/crt/'+config+'/guardian-runtime-api.dll')
        copy(a.build/'guest'/config/'guardian-kernel-components.gk','guest/'+config+'/guardian-kernel-components.gk')
        copy(a.build/'guest/security'/config/'advapi32.dll','guest/security/'+config+'/advapi32.dll')
        for name in (*(dll+'.dll' for dll in COMPONENT_DLLS),*(('guardian-component-'+app+'.exe') for app in COMPONENT_APPS),'guardian-component-inproc.dll'):copy(a.build/'guest/components'/config/name,'guest/components/'+config+'/'+name)
        for name in (*DLLS,*(('guardian-desktop-'+name+'.exe') for name in APPS)):copy(a.build/'guest/desktop'/config/name,'guest/desktop/'+config+'/'+name)
        copy(a.build/'guest'/config/'guardian-kernel-internet.gk','guest/'+config+'/guardian-kernel-internet.gk')
        copy(a.build/'guest'/config/'guardian-kernel-integrated.gk','guest/'+config+'/guardian-kernel-integrated.gk')
        for path in sorted((a.build/'guest/integrated'/config).glob('*.exe')):copy(path,'guest/integrated/'+config+'/'+path.name)
        copy(a.build/'guest'/config/'guardian-kernel-native-gui.gk','guest/'+config+'/guardian-kernel-native-gui.gk')
        for path in sorted((a.build/'guest/native-gui'/config).iterdir()):
            if path.suffix.lower() in ('.dll','.exe'):copy(path,'guest/native-gui/'+config+'/'+path.name)
        copy(a.build/'guest'/config/'guardian-kernel-native-user32.gk','guest/'+config+'/guardian-kernel-native-user32.gk')
        for path in sorted((a.build/'guest/native-user32'/config).iterdir()):
            if path.suffix.lower() in ('.dll','.exe'):copy(path,'guest/native-user32/'+config+'/'+path.name)
        copy(a.build/'guest'/config/'guardian-kernel-native-gdi.gk','guest/'+config+'/guardian-kernel-native-gdi.gk')
        for path in sorted((a.build/'guest/native-gdi'/config).iterdir()):
            if path.suffix.lower() in ('.dll','.exe'):copy(path,'guest/native-gdi/'+config+'/'+path.name)
        copy(a.build/'guest'/config/'guardian-kernel-native-controls.gk','guest/'+config+'/guardian-kernel-native-controls.gk')
        for path in sorted((a.build/'guest/native-controls'/config).iterdir()):
            if path.suffix.lower() in ('.dll','.exe'):copy(path,'guest/native-controls/'+config+'/'+path.name)
        copy(a.build/'guest'/config/'guardian-kernel-expanded-gdi.gk','guest/'+config+'/guardian-kernel-expanded-gdi.gk')
        for path in sorted((a.build/'guest/expanded-gdi'/config).iterdir()):
            if path.suffix.lower() in ('.dll','.exe'):copy(path,'guest/expanded-gdi/'+config+'/'+path.name)
        for path in sorted((a.build/'guest/internet'/config).iterdir()):
            if path.suffix.lower() in ('.dll','.exe'):copy(path,'guest/internet/'+config+'/'+path.name)
        for name in ('validation.json','ctest.log','ctest.xml'):copy(evidence/name,'validation/'+config+'/'+name)
        for case in ('crypto','sockets','http','tls','sspi','reference'):
            runs=sorted((a.build/('internet-'+case+'-'+config)).iterdir(),key=lambda path:int(path.name));nt.require(bool(runs),'Missing integration evidence')
            run=runs[-1];result=json.loads((run/'result.json').read_bytes());nt.require(result['status']=='PASS' and result['suite']==case,'Latest integration failed')
            copy(run/'result.json','validation/internet/'+config+'/'+case+'/result.json')
            for path in sorted(run.rglob('*')):
                if path.is_file() and (path.name=='host.log' or path.name in ('boot.json','internet.json') and path.parent.name=='session' or case=='reference' and path.parent==run and path.suffix=='.log'):
                    copy(path,'validation/internet/'+config+'/'+case+'/'+path.relative_to(run).as_posix())

        for case in ('programs','flappy','components'):
            runs=sorted((a.build/('window-bridge-'+case+'-'+config)).iterdir(),key=lambda path:int(path.name));nt.require(bool(runs),'Missing native window evidence');run=runs[-1];result=json.loads((run/'result.json').read_bytes());nt.require(result['status']=='PASS' and result['case']==case,'Latest native window suite failed')
            copy(run/'result.json','validation/window-bridge/'+config+'/'+case+'/result.json')
            for path in sorted(run.rglob('*')):
                if path.is_file() and (path.name in ('boot.json','window-bridge.json','native-windows.json') and path.parent.name=='session' or path.suffix=='.log' and path.parent.name!='bundle'):
                    copy(path,'validation/window-bridge/'+config+'/'+case+'/'+path.relative_to(run).as_posix())
        runs=sorted((a.build/('native-gui-'+config)).iterdir(),key=lambda path:int(path.name));run=runs[-1];result=json.loads((run/'result.json').read_bytes());nt.require(result['status']=='PASS','Native GUI evidence required');copy(run/'result.json','validation/native-gui/'+config+'/result.json')
        for path in sorted(run.rglob('*')):
            if path.is_file() and (path.name in ('native-gui.json','boot.json') and path.parent.name=='session' or path.name=='host.log'):copy(path,'validation/native-gui/'+config+'/'+path.relative_to(run).as_posix())
        runs=sorted((a.build/('native-user32-integration-'+config)).iterdir(),key=lambda path:int(path.name));run=runs[-1];result=json.loads((run/'result.json').read_bytes());nt.require(result['status']=='PASS','Native USER32 evidence required');copy(run/'result.json','validation/native-user32/'+config+'/result.json')
        for path in sorted(run.rglob('*')):
            if path.is_file() and (path.name in ('native-user32.json','boot.json') and path.parent.name=='session' or path.name=='host.log'):copy(path,'validation/native-user32/'+config+'/'+path.relative_to(run).as_posix())
        runs=sorted((a.build/('native-gdi-integration-'+config)).iterdir(),key=lambda path:int(path.name));run=runs[-1];result=json.loads((run/'result.json').read_bytes());nt.require(result['status']=='PASS','Native GDI evidence required');copy(run/'result.json','validation/native-gdi/'+config+'/result.json')
        for path in sorted(run.rglob('*')):
            if path.is_file() and (path.name in ('native-gdi.json','boot.json') and path.parent.name=='session' or path.name in ('host.log','reference.log')):copy(path,'validation/native-gdi/'+config+'/'+path.relative_to(run).as_posix())
        for suite in ('integration','flappy'):
            runs=sorted((a.build/('native-controls-'+suite+'-'+config)).iterdir(),key=lambda path:int(path.name));run=runs[-1];result=json.loads((run/'result.json').read_bytes());nt.require(result['status']=='PASS','Native controls evidence required');copy(run/'result.json','validation/native-controls/'+config+'/'+suite+'/result.json')
            for path in sorted(run.rglob('*')):
                if path.is_file() and (path.name in ('native-controls.json','boot.json') and path.parent.name=='session' or path.name in ('host.log','reference.log','native-reference-layout.json','native-history.json','native-client.ppm','score-native.ppm')):
                    copy(path,'validation/native-controls/'+config+'/'+suite+'/'+path.relative_to(run).as_posix())
        for suite in ('integration','compatibility','flappy','external'):
            runs=sorted((a.build/('expanded-gdi-'+suite+'-'+config)).iterdir(),key=lambda path:int(path.name));run=runs[-1];result=json.loads((run/'result.json').read_bytes());nt.require(result['status']=='PASS','Expanded GDI evidence required');copy(run/'result.json','validation/expanded-gdi/'+config+'/'+suite+'/result.json')
            for path in sorted(run.rglob('*')):
                if path.is_file() and (path.name in ('expanded-gdi.json','boot.json','analysis.json') and path.parent.name=='session' or path.name in ('host.log','reference.log','native-reference-layout.json','native-history.json') or path.suffix=='.ppm'):
                    copy(path,'validation/expanded-gdi/'+config+'/'+suite+'/'+path.relative_to(run).as_posix())
    for name in ('validation.json','ctest.log','ctest.xml'):copy(a.regression/name,'validation/regression-Release/'+name)
    for i,path in enumerate(a.development_log):copy(path,'validation/development/'+str(i)+'-'+path.name)
    copy(a.build/'internet-generated/coverage.json','validation/internet-export-coverage.json');copy(a.build/'component-generated/coverage.json','validation/component-export-coverage.json');copy(a.build/'desktop-generated/coverage.json','validation/desktop-export-coverage.json');copy(nt.ROOT/'config/crt-references.json','config/crt-references.json')
    for name in ('desktop_runtime.py','crt_runtime.py','win32_runtime.py','native_nt.py','api_set.py','nt_compatibility.py','inspect_gui_compatibility.py','pe_volume.py','component_runtime.py','internet_runtime.py','integrated_runtime.py','native_gui_runtime.py','native_user32_runtime.py','native_gdi_runtime.py','native_controls_runtime.py','smoke_phase14_10e.py','expanded_gdi_runtime.py','gdi_inventory.py','gdi_corpus.py'):copy(nt.ROOT/'tools'/name,'tools/'+name)
    for name in ('DesktopIntegrationTests.py','DesktopFlappyTests.py','ComponentIntegrationTests.py','InternetIntegrationTests.py','InternetLab.py','WindowBridgeIntegrationTests.py','WindowBridgeRuntimeToolsTests.py','NativeGuiIntegrationTests.py','NativeGuiRuntimeToolsTests.py','NativeUserIntegrationTests.py','NativeUserRuntimeToolsTests.py','NativeGdiIntegrationTests.py','NativeGdiRuntimeToolsTests.py','NativeControlsDriver.py','NativeControlsIntegrationTests.py','NativeControlsFlappyTests.py','NativeControlsRuntimeToolsTests.py','ExpandedGdiIntegrationTests.py','ExpandedGdiCompatibilityTests.py','ExpandedGdiFlappyTests.py','ExpandedGdiExternalTests.py','ExpandedGdiRuntimeToolsTests.py'):copy(nt.ROOT/'test'/name,'test/'+name)
    copy(nt.ROOT/'docs/PHASE-14.7.md','PHASE-14.7.md')
    copy(nt.ROOT/'docs/PHASE-14.8.md','PHASE-14.8.md')
    copy(nt.ROOT/'docs/PHASE-14.9.md','PHASE-14.9.md')
    copy(nt.ROOT/'docs/PHASE-12F.md','PHASE-12F.md')
    copy(nt.ROOT/'docs/PHASE-14.10A.md','PHASE-14.10A.md')
    copy(nt.ROOT/'docs/PHASE-14.10B.md','PHASE-14.10B.md')
    copy(nt.ROOT/'docs/PHASE-14.10C.md','PHASE-14.10C.md')
    copy(nt.ROOT/'docs/PHASE-14.10D.md','PHASE-14.10D.md')
    copy(nt.ROOT/'docs/PHASE-14.10E.md','PHASE-14.10E.md')
    for name in ('gdi-api-matrix.json','gdi-corpus.json'):copy(nt.ROOT/'config'/name,'config/'+name)
    copy(nt.ROOT/'config/internet-ordinals.json','config/internet-ordinals.json')
    copy(nt.ROOT/'test/fixtures/internet/certificates.json','test/fixtures/internet/certificates.json')
    for name in ('LICENSE.txt','UPSTREAM.json'):copy(nt.ROOT/'guest/third_party/bearssl'/name,'licenses/BearSSL/'+name)
    (out/'README-12F.txt').write_text('Aegis 12F - MiniNT independent windows on Windows desktop\n\nRequires Windows x64 with WHP/NX and Visual C++ runtime. Debug Host Tests\nthey also require Visual Studio debugging runtime, as in the validated machine.\nThe matrix accepts Windows 10 x64 in the guest, with callbacks running on MiniNT.\nThe native windows receive pixels and return only typed sessage events.\nWindows have prefix [MiniNT]; popups without title receive a blue border.\n\nIt does not include Microsoft input DLLs, Flappy, ISO, disks or VM states.\nProvides legal CRT/scheme and, for Flappy, the executioner original/settings/icon.\n\nOwn application (requires interaction A, text, click and M for dialogue):\npython -B tools/integrated_runtime.py --build . --config Release --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --output app-12f\n.\\Release\\guardian-desktop.exe guest/Release/guardian-kernel-integrated.gk app-12f/runtime.gpv app-12f/manifest.json app-12f/binding.gnt sessao-12f --integrated --seconds 30\n\nFor Flappy, add --application FLAPPY-PATH --asset CAMINHO-SETTINGS\n--asset CAMINHO-ICON a criacao do pacote. Ele continua executando no MiniNT.\nThe --interactive mode remains a unique viewer alternative.\nDo not mix --integrated with --interactive or --script; --window-script and\nTyped script of integrated window tests, explained in PHASE-12F.md.\n\nGuest DPI is virtualized in 96; native presentation supports 96 to 240 DPI.\nCLI sessions last up to 30 seconds. No GPU passthrough, clipboard or drag/drop.\nThe network remains offline, except for explicit 14.9 policy.\n--state permite reiniciar com disco virtual de 64 KiB; snapshots de RAM desta\nThe state is not distributed.\n\nTo check all files and run Release and Debug:\npython -B tools/smoke_phase14_10e.py --package . --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --application FLAPPY-PATH --corpus-binaries BUILT-CORPUS --output NEW-EVIDENCE\n\nRead PHASE-12F.md and previous deliveries for support and its limitations.\nThis text describes 12F mode; see README.txt for 14.10E delivery.\n',encoding='utf-8')
    (out/'README-14.10D.txt').write_text('Aegis 14.10D — Native Controls and GDI / MiniNT x64\n\nRequires Windows x64/WHP/NX and Visual C++ runtime; Debug requires Visual Studio runtime.\nUSER32 and GDI themselves in the guest forward typed commands to the restricted helper. Menus, controls and dialogues use the invited namespace.\nWindows GDI draws text, shapes and bitmaps; callsbacks remain in MiniNT.\nRead PHASE-14.10D.md for contract, limits and comparison with native reference.\nProvides CRT and legal schema; they are not in this ZIP.\npython -B tools/native_controls_runtime.py --build . --config Release --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --output app-gdi\nRelease/guardian-desktop.exe guest/Release/guardian-kernel-native-controls.gk app-gdi/runtime.gpv app-gdi/manifest.json app-gdi/binding.gnt sessao-nova --native-controls --seconds 25\nThe auxiliary must be next to the Guardian-desktop; the manifesto fixes its SHA256.\npython -B tools/smoke_phase14_10e.py --package . --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --application FLAPPY-PATH --corpus-binaries BUILT-CORPUS --output NEW-EVIDENCE\nNo Microsoft inbound DLLs, Flappy, ISOs or personal states. Full USER32/GDI are not declared.\n14.10E has not been initiated.\n',encoding='utf-8')
    (out/'README.txt').write_text("Aegis 14.10E - GDI enlarged in Windows / MiniNT x64 windows\n\nRequires Windows x64/WHP/NX and Visual C++ runtime. Debug requires Visual Studio runtime.\nCurves, paths, regions, transforms, raster and text in native auxiliary; callbacks in MiniNT.\nRead PHASE-14.10E.md and config/gdi-api-matrix.json for accepted clipping and pendencies.\nIt does not include Microsoft input DLLs, Flappy, sources or EXEs from external examples, ISOs or VM states.\nProvides CRT/schema and corpus compiled legal in external folder with Release/Debug and receipts.\npython -B tools/expanded_gdi_runtime.py --build . --config Release --reference CRT-DIRECTORY --schema-reference SCHEMA --output PACOTE-NOVO\nRelease/guardian-desktop.exe guest/Release/guardian-kernel-expanded-gdi.gk PACOTE-NOVO/runtime.gpv PACOTE-NOVO/manifest.json PACOTE-NOVO/binding.gnt SESSAO-NOVA --expanded-gdi --seconds 30\nThe guardian-expanded-gdi-scene.exe gallery and the package creator's standard application.\npython -B tools/smoke_phase14_10e.py --package . --reference CRT-DIRECTORY --schema-reference SCHEMA --application FLAPPY --corpus-binaries BUILT-CORPUS --output NEW-EVIDENCE\nThe auxiliary shall be at the side of the CLI; its SHA256 and linked to the manifest.\n14.10F has not been initiated.\n",encoding='utf-8')
    files={path.relative_to(out).as_posix():nt.sha(path.read_bytes()) for path in sorted(out.rglob('*')) if path.is_file()};(out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.10E','files':files,'acceptance':{'Release':len(REQUIRED),'Debug':len(REQUIRED)},'regressionTests':TOTAL_TESTS,'microsoftInputBinariesIncluded':False,'externalApplicationIncluded':False,'completeWindowsGui':False,'phase12FStarted':True,'arbitraryUser32Passthrough':False,'gpuAcceleration':False,'phase14_8Started':True,'phase14_9Started':True,'phase14_10Started':True,'phase14_10AStarted':True,'phase14_10BStarted':True,'phase14_10CStarted':True,'phase14_10DStarted':True,'phase14_10EStarted':True,'phase14_10FStarted':False,'nativeGdiSubset':True,'nativeControlsSubset':True,'guestNamespaceDialogs':True,'nativeGeometryPolicy':'guest-bounded-rectangles','standardUser32Subset':True,'nativeGuiBackend':'win32-expanded-gdi','expandedGdiSubset':True,'independentCorpusIncluded':False,'guiApiMatrixEntries':411,'hostTls':False,'hostCredentials':False,'completeWindowsNetworking':False,'completeCOM':False,'completeNDR':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}))
if __name__=='__main__':main()
