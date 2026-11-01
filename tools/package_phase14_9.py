"""Package the owned desktop runtime and stable phase 14.9 evidence only."""
from pathlib import Path
import argparse,json,shutil,zipfile
import nt_compatibility as nt
from validate_phase14_9 import REQUIRED,TOTAL_TESTS
COMPONENT_APPS=('client','client-crash','client-timeout','client-persist','controls','controls-cancel','rpc','child','server')
from component_runtime import DLLS as COMPONENT_DLLS
APPS=('demo','threads','settings','nativeprobe')
DLLS=('ntdll.dll','kernelbase.dll','kernel32.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--debug',type=Path,required=True);p.add_argument('--regression',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    for config,suite,folder in (('Release','acceptance',a.release),('Debug','acceptance',a.debug),('Release','regression',a.regression)):
        proof=json.loads((folder/'validation.json').read_bytes());covers=suite=='acceptance' and config=='Release' and proof['suite']=='regression';expected=TOTAL_TESTS if proof['suite']=='regression' else len(REQUIRED)
        nt.require(proof['phase']=='14.9' and proof['status']=='PASS' and (proof['suite']==suite or covers) and proof['configuration']==config and proof['tests']==expected and not proof['failures'] and not proof['skipped'] and proof['stableSourcesAndBinaries'],'Stable accepted evidence required')
        if suite=='acceptance':nt.require(REQUIRED<=set(proof['testNames']) if covers else REQUIRED==set(proof['testNames']),'Acceptance identities changed')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((a.build/name).read_bytes())==digest,'Binary changed: '+name)
        for name,digest in proof['flappyInputs'].items():nt.require(nt.sha(Path(name).read_bytes())==digest,'External input changed')
    out=a.output;archive=out.with_name(out.name+'.zip');nt.require(not out.exists() and not archive.exists(),'New package path required');out.mkdir(parents=True)
    def copy(source,name):
        dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-desktop','guardian-whp-memory','desktop_core_tests','desktop_viewer_tests','desktop_windows_demo','desktop_windows_threads','internet_core_tests','internet_crypto_reference','internet_sockets_reference','internet_http_reference','internet_timeout_reference','internet_wininet_reference','component_core_tests','component_windows_client','component_windows_controls','component_windows_rpc'):copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-desktop.gk','guest/'+config+'/guardian-kernel-desktop.gk')
        copy(a.build/'guest/crt'/config/'guardian-runtime-api.dll','guest/crt/'+config+'/guardian-runtime-api.dll')
        copy(a.build/'guest'/config/'guardian-kernel-components.gk','guest/'+config+'/guardian-kernel-components.gk')
        copy(a.build/'guest/security'/config/'advapi32.dll','guest/security/'+config+'/advapi32.dll')
        for name in (*(dll+'.dll' for dll in COMPONENT_DLLS),*(('guardian-component-'+app+'.exe') for app in COMPONENT_APPS),'guardian-component-inproc.dll'):copy(a.build/'guest/components'/config/name,'guest/components/'+config+'/'+name)
        for name in (*DLLS,*(('guardian-desktop-'+name+'.exe') for name in APPS)):copy(a.build/'guest/desktop'/config/name,'guest/desktop/'+config+'/'+name)
        copy(a.build/'guest'/config/'guardian-kernel-internet.gk','guest/'+config+'/guardian-kernel-internet.gk')
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

    for name in ('validation.json','ctest.log','ctest.xml'):copy(a.regression/name,'validation/regression-Release/'+name)
    copy(a.build/'internet-generated/coverage.json','validation/internet-export-coverage.json');copy(a.build/'component-generated/coverage.json','validation/component-export-coverage.json');copy(a.build/'desktop-generated/coverage.json','validation/desktop-export-coverage.json');copy(nt.ROOT/'config/crt-references.json','config/crt-references.json')
    for name in ('desktop_runtime.py','crt_runtime.py','win32_runtime.py','native_nt.py','api_set.py','nt_compatibility.py','inspect_gui_compatibility.py','pe_volume.py','component_runtime.py','internet_runtime.py','smoke_phase14_9.py'):copy(nt.ROOT/'tools'/name,'tools/'+name)
    for name in ('DesktopIntegrationTests.py','DesktopFlappyTests.py','ComponentIntegrationTests.py','InternetIntegrationTests.py','InternetLab.py'):copy(nt.ROOT/'test'/name,'test/'+name)
    copy(nt.ROOT/'docs/PHASE-14.7.md','PHASE-14.7.md')
    copy(nt.ROOT/'docs/PHASE-14.8.md','PHASE-14.8.md')
    copy(nt.ROOT/'docs/PHASE-14.9.md','PHASE-14.9.md')
    copy(nt.ROOT/'config/internet-ordinals.json','config/internet-ordinals.json')
    copy(nt.ROOT/'test/fixtures/internet/certificates.json','test/fixtures/internet/certificates.json')
    for name in ('LICENSE.txt','UPSTREAM.json'):copy(nt.ROOT/'guest/third_party/bearssl'/name,'licenses/BearSSL/'+name)
    (out/'README.txt').write_text('Aegis 14.9 - MiniNT x64 network and encryption\n\nRequires Windows x64, WHP and NX active and Visual C++ runtime on host.\nIt contains kernels/DLLs itself, customers and tests of the 14.7 phases to 14.9.\nThe standard and offline mode. TLS 1.2 performs within the guest.\nThe test certificados/chaves are synthetic and intentionally public.\nNever install these certificates as reliable on Windows.\n\nIt does not include Microsoft input DLLs, Flappy, ISO, disks or VM states.\nUse CRT/schema references cool locations and the original Flappy with settings/icon.\n\nExample offline, in a new folder, from the root of this package:\npython -B tools/internet_runtime.py bundle --build . --config Release --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --output internet\n.\\Release\\guardian-desktop.exe guest/Release/guardian-kernel-internet.gk internet/runtime.gpv internet/manifest.json internet/binding.gnt sessao\n\nThe destination policy can be generated from JSON with the policy subcommand.\nAdd --network-policy policy.bin to use-la; read PHASE-14.9.md.\n--state accepts the virtual disk persisted; RAM snapshots are not accepted\nby transporting 14.9. The state may contain guest keys.\n\nComplete package check, creating another evidence folder:\npython -B tools/smoke_phase14_9.py --package . --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --application FLAPPY-PATH --output NEW-EVIDENCE\n\nThe windows are still in the 14.7 viewer; 12F has not been started.\nRead PHASE-14.9.md and previous documents for quotas and limitations.\nThe 14.10 phase is not part of this delivery.\n',encoding='utf-8')
    files={path.relative_to(out).as_posix():nt.sha(path.read_bytes()) for path in sorted(out.rglob('*')) if path.is_file()};(out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.9','files':files,'acceptance':{'Release':len(REQUIRED),'Debug':len(REQUIRED)},'regressionTests':TOTAL_TESTS,'microsoftInputBinariesIncluded':False,'externalApplicationIncluded':False,'completeWindowsGui':False,'phase12FStarted':False,'phase14_8Started':True,'phase14_9Started':True,'phase14_10Started':False,'hostTls':False,'hostCredentials':False,'completeWindowsNetworking':False,'completeCOM':False,'completeNDR':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}))
if __name__=='__main__':main()
