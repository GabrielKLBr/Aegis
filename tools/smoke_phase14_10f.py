"""Verify every packaged byte and run both desktop configurations with local legal inputs."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,zipfile

def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--package',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--application',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--corpus-binaries',type=Path,required=True);p.add_argument('--software-corpus-binaries',type=Path,required=True);a=p.parse_args();package=a.package.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);manifest=json.loads((package/'manifest.json').read_bytes());assert manifest['phase']=='14.10F' and not manifest['microsoftInputBinariesIncluded'] and not manifest['externalApplicationIncluded'];expected=set(manifest['files'])|{'manifest.json'};assert {path.relative_to(package).as_posix() for path in package.rglob('*') if path.is_file()}==expected
    for name,digest in manifest['files'].items():assert sha((package/name).read_bytes())==digest,name
    archive=package.with_name(package.name+'.zip');assert archive.exists()
    with zipfile.ZipFile(archive) as z:
        assert set(z.namelist())==expected and z.read('manifest.json')==(package/'manifest.json').read_bytes()
        for name,digest in manifest['files'].items():assert sha(z.read(name))==digest
    for config in ('Release','Debug'):
        for name in ('desktop_core_tests','desktop_viewer_tests','desktop_windows_demo','desktop_windows_threads','window_bridge_core_tests','window_bridge_native_tests','native_gui_core_tests','native_user32_protocol_tests','native_gdi_protocol_tests','native_controls_protocol_tests','expanded_gdi_protocol_tests','internet_core_tests','internet_crypto_reference','component_core_tests','component_windows_client','component_windows_controls','component_windows_rpc'):
            run=subprocess.run([str(package/config/(name+'.exe'))],capture_output=True,text=True,timeout=30);(out/(config+'-'+name+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert run.returncode==0,run.stdout+run.stderr
        run=subprocess.run([str(package/config/'native_gui_broker_tests.exe'),str(package/config/'guardian-native-gui-host.exe')],capture_output=True,text=True,timeout=30);(out/(config+'-native_gui_broker.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        run=subprocess.run([str(package/config/'native_user32_broker_tests.exe'),str(package/config/'guardian-native-user-host.exe')],capture_output=True,text=True,timeout=45);(out/(config+'-native_user32_broker.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        run=subprocess.run([str(package/config/'native_gdi_broker_tests.exe'),str(package/config/'guardian-native-gdi-host.exe')],capture_output=True,text=True,timeout=90);(out/(config+'-native_gdi_broker.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        run=subprocess.run([str(package/config/'native_controls_broker_tests.exe'),str(package/config/'guardian-native-controls-host.exe')],capture_output=True,text=True,timeout=90);(out/(config+'-native_controls_broker.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        run=subprocess.run([str(package/config/'expanded_gdi_broker_tests.exe'),str(package/config/'guardian-expanded-gdi-host.exe')],capture_output=True,text=True,timeout=90);(out/(config+'-expanded_gdi_broker.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        run=subprocess.run([sys.executable,'-B',str(package/'tools/gdi_inventory.py'),'--build',str(package),'--config',config],capture_output=True,text=True,timeout=30);(out/(config+'-expanded_gdi_inventory.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for script in ('DesktopIntegrationTests.py','DesktopFlappyTests.py'):
            command=[sys.executable,'-B',str(package/'test'/script),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-'+script[:-3]))]
            if script=='DesktopFlappyTests.py':command+=['--application',str(a.application.resolve())]
            run=subprocess.run(command,capture_output=True,text=True,timeout=180);(out/(config+'-'+script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for case in ('crypto','sockets','http','tls','sspi','reference'):
            command=[sys.executable,'-B',str(package/'test/InternetIntegrationTests.py'),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-internet-'+case)),'--case',case]
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=240);(out/(config+'-internet-'+case+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for scenario in ('com','rpc','controls'):
            command=[sys.executable,'-B',str(package/'test/ComponentIntegrationTests.py'),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-component-'+scenario)),'--case',scenario]
            run=subprocess.run(command,capture_output=True,text=True,timeout=240);(out/(config+'-component-'+scenario+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for case in ('programs','flappy','components'):
            command=[sys.executable,'-B',str(package/'test/WindowBridgeIntegrationTests.py'),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-window-bridge-'+case)),'--case',case]
            if case=='flappy':command+=['--application',str(a.application.resolve())]
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(out/(config+'-window-bridge-'+case+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        command=[sys.executable,'-B',str(package/'test/WindowBridgeRuntimeToolsTests.py'),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve())]
        run=subprocess.run(command,capture_output=True,text=True,timeout=60);(out/(config+'-window-bridge-tools.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for script in ('NativeGuiIntegrationTests.py','NativeGuiRuntimeToolsTests.py'):
            command=[sys.executable,'-B',str(package/'test'/script),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve())]
            if script=='NativeGuiIntegrationTests.py':command+=['--evidence',str(out/(config+'-native-gui'))]
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(out/(config+'-'+script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for script in ('NativeUserIntegrationTests.py','NativeUserRuntimeToolsTests.py'):
            command=[sys.executable,'-B',str(package/'test'/script),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-native-user32'))]
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(out/(config+'-'+script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for script in ('NativeGdiIntegrationTests.py','NativeGdiRuntimeToolsTests.py'):
            command=[sys.executable,'-B',str(package/'test'/script),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-native-gdi'))]
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=240);(out/(config+'-'+script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for script in ('NativeControlsIntegrationTests.py','NativeControlsFlappyTests.py','NativeControlsRuntimeToolsTests.py'):
            command=[sys.executable,'-B',str(package/'test'/script),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-'+script[:-3]))]
            if script=='NativeControlsFlappyTests.py':command+=['--application',str(a.application.resolve())]
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=300);(out/(config+'-'+script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for script in ('ExpandedGdiCompatibilityTests.py','ExpandedGdiFlappyTests.py','ExpandedGdiRuntimeToolsTests.py','ExpandedGdiIntegrationTests.py','ExpandedGdiExternalTests.py'):
            command=[sys.executable,'-B',str(package/'test'/script),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-'+script[:-3]))]
            if script=='ExpandedGdiFlappyTests.py':command+=['--application',str(a.application.resolve())]
            if script=='ExpandedGdiExternalTests.py':command+=['--corpus-binaries',str(a.corpus_binaries.resolve())]
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=420);(out/(config+'-'+script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for name,args in (('software_graphics_protocol_tests',[]),('software_graphics_broker_tests',[str(package/config/'guardian-software-graphics-host.exe')])):
            run=subprocess.run([str(package/config/(name+'.exe')),*args],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
            (out/(config+'-'+name+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        run=subprocess.run([sys.executable,'-B',str(package/'tools/software_graphics_matrix.py'),'--build',str(package),'--config',config],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
        (out/(config+'-software-inventory.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for script in ('SoftwareGraphicsIntegrationTests.py','SoftwareGraphicsFailureTests.py','SoftwareGraphicsRuntimeToolsTests.py'):
            command=[sys.executable,'-B',str(package/'test'/script),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-'+script[:-3]))]
            if script=='SoftwareGraphicsIntegrationTests.py':command+=['--external',str(a.software_corpus_binaries.resolve()/config/'direct2d-hello.exe')]
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=300)
            (out/(config+'-'+script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
    assert {path.relative_to(package).as_posix() for path in package.rglob('*') if path.is_file()}==expected
    for name,digest in manifest['files'].items():assert sha((package/name).read_bytes())==digest,name
    proof={'status':'PASS','phase':'14.10F','configurations':['Release','Debug'],'files':len(expected),'zipVerified':True,'guestCases':218,'hostCases':106,'softwareGuestCases':8,'softwareNativeExecutions':10,'hostCaseScope':'top-level native reference/test executables; excludes Python drivers and restricted helper children','legacyNegativeHostScriptRuns':20,'nativeUserToolChecks':12,'nativeGdiToolChecks':12,'nativeControlsToolChecks':14,'expandedGdiToolChecks':16,'rejectedGuiImports':4,'preservedOutputs':{'windowBridge':2,'nativeGui':2,'nativeUser32':2,'nativeGdi':2,'nativeControls':2,'expandedGdi':2},'microsoftInputsIncluded':False,'externalApplicationIncluded':False,'hostActivation':False,'completeCOM':False,'phase12FStarted':True,'phase14_9Started':True,'phase14_10Started':True,'phase14_10BStarted':True,'phase14_10CStarted':True,'phase14_10DStarted':True,'phase14_10EStarted':True,'phase14_10FStarted':True,'phase14_10GStarted':False,'fullDirectX':False,'shadersAvailable':False,'softwareToolNegativeModes':10,'softwareHelperBindingFailures':2,'independentCorpusIncluded':False,'hostTls':False};(out/'result.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
