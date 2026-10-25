from pathlib import Path
import argparse,json,subprocess,sys,zipfile,hashlib
def sha(raw):return hashlib.sha256(raw).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--package',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    package=a.package.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);manifest=json.loads((package/'manifest.json').read_bytes());assert manifest['phase']=='14.5' and not manifest['microsoftInputBinariesIncluded']
    for name,digest in manifest['files'].items():assert sha((package/name).read_bytes())==digest,name
    archive=package.with_name(package.name+'.zip')
    if archive.exists():
        with zipfile.ZipFile(archive) as z:
            assert set(z.namelist())==set(manifest['files'])|{'manifest.json'}
            assert z.read('manifest.json')==(package/'manifest.json').read_bytes()
            for name,digest in manifest['files'].items():assert sha(z.read(name))==digest
    for config in ('Release','Debug'):
        run=subprocess.run([sys.executable,str(package/'test/CrtIntegrationTests.py'),'--build',str(package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve()),'--evidence',str(out/config)],capture_output=True,text=True,timeout=150)
        (out/(config+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert run.returncode==0,run.stdout+run.stderr
    proof={'status':'PASS','phase':'14.5','configurations':['Release','Debug'],'files':len(manifest['files'])+1,'zipVerified':archive.exists(),'guestCases':14,'microsoftInputsIncluded':False};(out/'result.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
