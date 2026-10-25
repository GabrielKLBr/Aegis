from pathlib import Path
import argparse,json,shutil,subprocess,tempfile
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
    a.evidence.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));results=[]
    for name in ('guardian-crt.exe','guardian-crt-fh4.exe'):
        # Only our executable is copied. Windows supplies its installed KnownDLL
        # and Visual C++ runtimes; supplied guest reference DLLs never run here.
        source=a.build/'guest/crt'/a.config/name;target=out/name;shutil.copyfile(source,target)
        r=subprocess.run([str(target.resolve()),'hello world','--child'],cwd=out,capture_output=True,text=True,timeout=20)
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        assert r.returncode==0,r.stdout+r.stderr
        assert 'argc=3 sum=15 original C++' in r.stdout and 'CRT exceptions TLS files locale math PASS' in r.stdout
        assert r.stdout.index('CRT atexit')<r.stdout.index('CRT global destructor') and not (out/'crt-data.txt').exists()
        results.append({'application':name,'status':'PASS','reference':'installed Windows and VC runtimes; guest input DLLs not executed on host'})
    proof={'status':'PASS','phase':'14.5','configuration':a.config,'results':results};(out/'result.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
