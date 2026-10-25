from pathlib import Path
import argparse,json,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import win32_runtime as w
import pe_volume
import nt_compatibility as nt

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
    a.evidence.mkdir(parents=True,exist_ok=True);folder=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));results=[]
    for profile in nt.PROFILES:
        bundle=folder/profile;manifest=w.bundle(a.build,a.config,bundle,profile,a.reference);pe_volume.verify(bundle)
        app=nt.InventoryPe(a.build/'guest/win32'/a.config/profile/'guardian-win32.exe');imports=app.imports()
        assert 'kernel32.dll' in imports and 'api-ms-win-core-memory-l1-1-0.dll' in imports
        assert not any(f['path'].endswith('api-ms-win-core-memory-l1-1-0.dll') for f in manifest['files'])
        session=folder/(profile+'-session');command=[str(a.build/a.config/'guardian-pe.exe'),str(a.build/'guest'/a.config/'guardian-kernel-win32.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session)]
        run=subprocess.run(command,capture_output=True,text=True,timeout=45);(folder/(profile+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
        boot=json.loads((session/'boot.json').read_bytes())['boots'][0];trace=json.loads((session/'analysis.json').read_bytes());events=trace['events']
        assert boot['success'] and 'Win32 14.4 console threads pool child' in boot['console'] and not boot['timedOut'] and not boot['memoryFault']
        assert boot['ntCompatibility']['targetProfile']==nt.PROFILES[profile][0]
        assert boot['processes']['freeBefore']==boot['processes']['freeAfter']+21
        assert trace['complete'] and not any(trace[k] for k in ('dropped','rejected','pending'))
        children={e['related'] for e in events if e['domain']=='process' and e['operation']=='create' and e['stage']=='result' and not e['status'] and e['related']}
        assert len(children)==1 and any(e['pid'] in children and e['domain']=='file' and e['operation']=='read' and e['stage']=='result' and not e['status'] for e in events)
        assert any(e['domain']=='file' and e['operation']=='write' and e['stage']=='result' and not e['status'] for e in events)
        results.append({'profile':profile,'status':'PASS','schemaVersion':manifest['win32Runtime']['apiSet']['schemaVersion'],'childPids':sorted(children),'events':len(events),'modules':list(imports)})
        if profile=='win10':
            bad=bytearray((bundle/'runtime.gpv').read_bytes());bad[-1]^=1;path=folder/'tampered.gpv';path.write_bytes(bad);invalid=command.copy();invalid[2]=str(path);invalid[-1]=str(folder/'tamper-session')
            refusal=subprocess.run(invalid,capture_output=True,text=True,timeout=10);assert refusal.returncode and not (folder/'tamper-session').exists()
    proof={'schema':1,'phase':'14.4','configuration':a.config,'status':'PASS','profiles':results,'windowsBinaryCompatibility':False,'phase14_5Started':False};(folder/'result.json').write_bytes(nt.canonical(proof));print(json.dumps(proof));return 0
if __name__=='__main__':raise SystemExit(main())
