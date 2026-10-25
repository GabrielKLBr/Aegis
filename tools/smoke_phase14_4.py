"""Verify the distributed archive and boot its owned Win32 matrix in both builds."""
from pathlib import Path
import argparse,json,subprocess,zipfile
import nt_compatibility as nt

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--package',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    package=a.package.resolve();manifest=json.loads((package/'manifest.json').read_bytes());expected={'manifest.json',*manifest['files']};actual={p.relative_to(package).as_posix() for p in package.rglob('*') if p.is_file()};nt.require(actual==expected and manifest['phase']=='14.4','Package inventory mismatch')
    archive=package.with_name(package.name+'.zip')
    with zipfile.ZipFile(archive) as z:
        nt.require(z.testzip() is None and set(z.namelist())==expected and len(z.namelist())==len(expected),'Archive inventory mismatch')
        for name,digest in manifest['files'].items():nt.require(nt.sha((package/name).read_bytes())==digest==nt.sha(z.read(name)),'Package digest mismatch: '+name)
        nt.require(z.read('manifest.json')==(package/'manifest.json').read_bytes(),'Archive manifest mismatch')
    results=[]
    for config in ('Release','Debug'):
        profile=package/config/'profiles/win10';session=(a.output/config).resolve();command=[str(package/config/'guardian-pe.exe'),str(package/'guest'/config/'guardian-kernel-win32.gk'),str(profile/'runtime.gpv'),str(profile/'manifest.json'),str(profile/'binding.gnt'),str(session)]
        run=subprocess.run(command,capture_output=True,text=True,timeout=45);(a.output/(config+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');nt.require(not run.returncode,'Packaged CLI failed: '+config)
        boot=json.loads((session/'boot.json').read_bytes())['boots'][0];trace=json.loads((session/'analysis.json').read_bytes());nt.require(boot['success'] and 'Win32 14.4 console threads pool child' in boot['console'] and trace['complete'] and not any(trace[k] for k in ('dropped','rejected','pending')),'Packaged matrix failed')
        results.append({'configuration':config,'status':'PASS','events':len(trace['events'])})
    proof={'schema':1,'phase':'14.4','status':'PASS','files':len(expected),'archiveSha256':nt.sha(archive.read_bytes()),'boots':results};(a.output/'smoke.json').write_bytes(nt.canonical(proof));print(json.dumps(proof));return 0
if __name__=='__main__':raise SystemExit(main())
