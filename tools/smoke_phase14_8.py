"""Verify every packaged byte and run both desktop configurations with local legal inputs."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,zipfile

def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--package',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--application',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();package=a.package.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);manifest=json.loads((package/'manifest.json').read_bytes());assert manifest['phase']=='14.8' and not manifest['microsoftInputBinariesIncluded'] and not manifest['externalApplicationIncluded'];expected=set(manifest['files'])|{'manifest.json'};assert {path.relative_to(package).as_posix() for path in package.rglob('*') if path.is_file()}==expected
    for name,digest in manifest['files'].items():assert sha((package/name).read_bytes())==digest,name
    archive=package.with_name(package.name+'.zip');assert archive.exists()
    with zipfile.ZipFile(archive) as z:
        assert set(z.namelist())==expected and z.read('manifest.json')==(package/'manifest.json').read_bytes()
        for name,digest in manifest['files'].items():assert sha(z.read(name))==digest
    for config in ('Release','Debug'):
        for name in ('desktop_core_tests','desktop_viewer_tests','desktop_windows_demo','desktop_windows_threads','component_core_tests','component_windows_client','component_windows_controls','component_windows_rpc'):
            run=subprocess.run([str(package/config/(name+'.exe'))],capture_output=True,text=True,timeout=30);(out/(config+'-'+name+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert run.returncode==0,run.stdout+run.stderr
        for script in ('DesktopIntegrationTests.py','DesktopFlappyTests.py'):
            command=[sys.executable,'-B',str(package/'test'/script),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-'+script[:-3]))]
            if script=='DesktopFlappyTests.py':command+=['--application',str(a.application.resolve())]
            run=subprocess.run(command,capture_output=True,text=True,timeout=180);(out/(config+'-'+script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        for scenario in ('com','rpc','controls'):
            command=[sys.executable,'-B',str(package/'test/ComponentIntegrationTests.py'),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/(config+'-component-'+scenario)),'--case',scenario]
            run=subprocess.run(command,capture_output=True,text=True,timeout=240);(out/(config+'-component-'+scenario+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
    assert {path.relative_to(package).as_posix() for path in package.rglob('*') if path.is_file()}==expected
    proof={'status':'PASS','phase':'14.8','configurations':['Release','Debug'],'files':len(expected),'zipVerified':True,'guestCases':34,'hostCases':16,'microsoftInputsIncluded':False,'externalApplicationIncluded':False,'hostActivation':False,'completeCOM':False,'phase12FStarted':False,'phase14_9Started':False};(out/'result.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
