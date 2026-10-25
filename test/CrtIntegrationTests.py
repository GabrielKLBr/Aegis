from pathlib import Path
import argparse,json,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import crt_runtime as c
import nt_compatibility as nt
import pe_volume
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
    a.evidence.mkdir(parents=True,exist_ok=True);folder=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));results=[]
    cases=[('guardian-crt-probe.exe','original UCRT allocation math stdio',True),('guardian-crt.exe','CRT exceptions TLS files locale math PASS',True),('guardian-crt-fh4.exe','CRT exceptions TLS files locale math PASS',True),('guardian-crt-launcher.exe','quoted argv child PASS',True),('guardian-crt-legacy.exe','original MSVCRT PASS',True),('guardian-crt-unsupported.exe','CRT unsupported API: CopyFileW',False),('guardian-crt-rethrow.exe','',False)]
    for index,(app,marker,success) in enumerate(cases):
        bundle=folder/str(index);extra={'runtimes':('msvcrt.dll',)} if 'legacy' in app else {}
        manifest=c.bundle(a.build,a.config,bundle,a.reference,a.schema_reference,app,**extra);pe_volume.verify(bundle)
        session=folder/(str(index)+'-session')
        command=[str(a.build/a.config/'guardian-pe.exe'),str(a.build/'guest'/a.config/'guardian-kernel-crt.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session)]
        run=subprocess.run(command,capture_output=True,text=True,timeout=45);(folder/(str(index)+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
        boot=json.loads((session/'boot.json').read_bytes())['boots'][0];trace=json.loads((session/'analysis.json').read_bytes());events=trace['events']
        assert boot['success']==success and marker in boot['console'],run.stdout+run.stderr
        assert not boot['timedOut'] and not boot['memoryFault'] and not boot['cpu']['kernelFaults']
        assert not any(trace[k] for k in ('pending','dropped','rejected'))
        if success:
            assert trace['complete']
            assert not run.returncode and boot['processes']['freeBefore']==boot['processes']['freeAfter']+21
            assert not any(boot['processes'][k] for k in ('liveProcesses','liveObjects','liveHandles','liveReferences'))
            if app in ('guardian-crt.exe','guardian-crt-fh4.exe','guardian-crt-launcher.exe'):
                text=boot['console'];assert text.index('CRT atexit')<text.index('CRT global destructor') and 'original C++' in text and 'CRT MSVCP iostream' in text
                assert any(e['domain']=='file' and e['operation']=='write' and e['stage']=='result' and not e['status'] for e in events)
            if app=='guardian-crt-launcher.exe':assert 'argc=3' in boot['console'] and any(e['domain']=='process' and e['operation']=='create' and e['stage']=='result' and e['related'] for e in events)
            if app=='guardian-crt-probe.exe':assert {21,22}<={e['object'] for e in events if e['resource']=='Win32 service' and not e['status']}
        else:
            assert not trace['complete'] and any(f['classification']=='inconclusive' for f in trace['findings'])
            assert any(e['domain']=='process' and e['operation']=='exit' for e in events) or boot['stopStatus']
        results.append({'application':app,'expectedSuccess':success,'status':'PASS','events':len(events),'stopStatus':boot['stopStatus'],'references':manifest['crtRuntime']['originalReferences']})
        if index==0:
            damaged=bytearray((bundle/'runtime.gpv').read_bytes());damaged[-1]^=1;path=folder/'tampered.gpv';path.write_bytes(damaged);bad=command.copy();bad[2]=str(path);bad[-1]=str(folder/'tamper-session')
            refusal=subprocess.run(bad,capture_output=True,text=True,timeout=10);assert refusal.returncode and not (folder/'tamper-session').exists()
    proof={'schema':1,'phase':'14.5','configuration':a.config,'status':'PASS','cases':results,'fullCrtCompatibility':False,'phase14_6Started':False}
    (folder/'result.json').write_bytes(nt.canonical(proof));print(json.dumps({'status':'PASS','phase':'14.5','configuration':a.config,'cases':len(results),'positive':5,'negative':2}))
if __name__=='__main__':main()
