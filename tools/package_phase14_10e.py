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
    (out/'LEIA-ME-12F.txt').write_text('''Aegis 12F - janelas independentes do MiniNT no desktop Windows

Requer Windows x64 com WHP/NX e Visual C++ runtime. Os testes Debug do host
exigem tambem o runtime de depuracao do Visual Studio, como na maquina validada.
A matriz aceita Windows 10 x64 no convidado, com callbacks executados no MiniNT.
As janelas nativas recebem pixels e devolvem somente eventos tipados da sessao.
Janelas tem prefixo [MiniNT]; popups sem titulo recebem uma borda azul.

Nao inclui DLLs Microsoft de entrada, Flappy, ISO, discos ou estados de VM.
Forneca CRT/schema legais e, para o Flappy, o executavel original/settings/icon.

Aplicativo proprio (requer interacao A, texto, clique e M para dialogo):
python -B tools/integrated_runtime.py --build . --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output app-12f
.\\Release\\guardian-desktop.exe guest/Release/guardian-kernel-integrated.gk app-12f/runtime.gpv app-12f/manifest.json app-12f/binding.gnt sessao-12f --integrated --seconds 30

Para o Flappy, acrescente --application CAMINHO-FLAPPY --asset CAMINHO-SETTINGS
--asset CAMINHO-ICON a criacao do pacote. Ele continua executando no MiniNT.
O modo --interactive permanece como alternativa de visualizador unico.
Nao misture --integrated com --interactive ou --script; --window-script e o
roteiro tipado de testes das janelas integradas, explicado em PHASE-12F.md.

DPI do convidado e virtualizado em 96; apresentacao nativa suporta 96 a 240 DPI.
Sessoes do CLI duram ate 30 segundos. Sem GPU passthrough, clipboard ou drag/drop.
A rede permanece offline, salvo politica explicita da 14.9.
--state permite reiniciar com disco virtual de 64 KiB; snapshots de RAM desta
familia grafica continuam recusados. Estados nao sao distribuidos.

Para conferir todos os arquivos e executar Release e Debug:
python -B tools/smoke_phase14_10e.py --package . --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --application CAMINHO-FLAPPY --corpus-binaries CORPUS-COMPILADO --output EVIDENCIA-NOVA

Leia PHASE-12F.md e as entregas anteriores para o suporte e suas limitacoes.
Este texto descreve o modo 12F; veja LEIA-ME.txt para a entrega 14.10E.
''',encoding='utf-8')
    (out/'LEIA-ME-14.10D.txt').write_text('Aegis 14.10D — Controles e GDI nativos / MiniNT x64\n\nRequer Windows x64/WHP/NX e Visual C++ runtime; Debug exige o runtime do Visual Studio.\nUSER32 e GDI proprios no convidado encaminham comandos tipados ao auxiliar restrito. Menus, controles e dialogos usam o namespace convidado.\nGDI do Windows desenha texto, formas e bitmaps; callbacks permanecem no MiniNT.\nLeia PHASE-14.10D.md para contrato, limites e comparacao com referencia nativa.\nForneca CRT e schema legais; nao estao neste ZIP.\npython -B tools/native_controls_runtime.py --build . --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output app-gdi\nRelease/guardian-desktop.exe guest/Release/guardian-kernel-native-controls.gk app-gdi/runtime.gpv app-gdi/manifest.json app-gdi/binding.gnt sessao-nova --native-controls --seconds 25\nO auxiliar deve ficar ao lado do guardian-desktop; o manifesto fixa seu SHA256.\npython -B tools/smoke_phase14_10e.py --package . --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --application CAMINHO-FLAPPY --corpus-binaries CORPUS-COMPILADO --output EVIDENCIA-NOVA\nSem DLLs Microsoft de entrada, Flappy, ISOs ou estados pessoais. USER32/GDI completos nao sao declarados.\nA 14.10E nao foi iniciada.\n',encoding='utf-8')
    (out/'LEIA-ME.txt').write_text('Aegis 14.10E - GDI ampliado em janelas do Windows / MiniNT x64\n\nRequer Windows x64/WHP/NX e Visual C++ runtime. Debug requer o runtime do Visual Studio.\nCurvas, paths, regioes, transformacoes, raster e texto no auxiliar nativo; callbacks no MiniNT.\nLeia PHASE-14.10E.md e config/gdi-api-matrix.json para o recorte aceito e as pendencias.\nNao inclui DLLs Microsoft de entrada, Flappy, fontes ou EXEs dos exemplos externos, ISOs ou estados de VM.\nForneca CRT/schema e corpus compilado legal em pasta externa com Release/Debug e recibos.\npython -B tools/expanded_gdi_runtime.py --build . --config Release --reference PASTA-CRT --schema-reference SCHEMA --output PACOTE-NOVO\nRelease/guardian-desktop.exe guest/Release/guardian-kernel-expanded-gdi.gk PACOTE-NOVO/runtime.gpv PACOTE-NOVO/manifest.json PACOTE-NOVO/binding.gnt SESSAO-NOVA --expanded-gdi --seconds 30\nA galeria guardian-expanded-gdi-scene.exe e o aplicativo padrao do criador do pacote.\npython -B tools/smoke_phase14_10e.py --package . --reference PASTA-CRT --schema-reference SCHEMA --application FLAPPY --corpus-binaries CORPUS-COMPILADO --output EVIDENCIA-NOVA\nO auxiliar deve ficar ao lado do CLI; seu SHA256 e vinculado ao manifesto.\nA 14.10F nao foi iniciada.\n',encoding='utf-8')
    files={path.relative_to(out).as_posix():nt.sha(path.read_bytes()) for path in sorted(out.rglob('*')) if path.is_file()};(out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.10E','files':files,'acceptance':{'Release':len(REQUIRED),'Debug':len(REQUIRED)},'regressionTests':TOTAL_TESTS,'microsoftInputBinariesIncluded':False,'externalApplicationIncluded':False,'completeWindowsGui':False,'phase12FStarted':True,'arbitraryUser32Passthrough':False,'gpuAcceleration':False,'phase14_8Started':True,'phase14_9Started':True,'phase14_10Started':True,'phase14_10AStarted':True,'phase14_10BStarted':True,'phase14_10CStarted':True,'phase14_10DStarted':True,'phase14_10EStarted':True,'phase14_10FStarted':False,'nativeGdiSubset':True,'nativeControlsSubset':True,'guestNamespaceDialogs':True,'nativeGeometryPolicy':'guest-bounded-rectangles','standardUser32Subset':True,'nativeGuiBackend':'win32-expanded-gdi','expandedGdiSubset':True,'independentCorpusIncluded':False,'guiApiMatrixEntries':411,'hostTls':False,'hostCredentials':False,'completeWindowsNetworking':False,'completeCOM':False,'completeNDR':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}))
if __name__=='__main__':main()
