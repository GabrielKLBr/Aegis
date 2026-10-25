from pathlib import Path
import argparse,json,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import security_runtime as runtime
import nt_compatibility as nt
import pe_volume
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True);folder=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));rows=[];checkpoint=None
    for index,(app,marker) in enumerate([('guardian-security.exe','security registry tokens ACL profiles SCM events PASS'),('guardian-security-read.exe','security reboot persistence PASS'),('guardian-security-orphan.exe','security creator exit lifetime PASS'),('guardian-security-crash.exe','security service crash recovery PASS')]):
        bundle=folder/str(index);runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference,app);pe_volume.verify(bundle);session=folder/(str(index)+'-session');cmd=[str(a.build/a.config/'guardian-pe.exe'),str(a.build/'guest'/a.config/'guardian-kernel-security.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session)]
        if index==1:cmd.append(str(checkpoint))
        run=subprocess.run(cmd,capture_output=True,text=True,timeout=45);(folder/(str(index)+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');boot=json.loads((session/'boot.json').read_bytes())['boots'][0];trace=json.loads((session/'analysis.json').read_bytes())
        assert run.returncode==0 and boot['success'] and marker in boot['console'],run.stdout+run.stderr
        assert not boot['timedOut'] and not boot['memoryFault'] and not boot['cpu']['kernelFaults']
        assert not any(boot['processes'][k] for k in ('liveProcesses','liveObjects','liveHandles','liveReferences'))
        assert not any(trace[k] for k in ('pending','dropped','rejected'))
        state=session/'security-state.bin';assert state.stat().st_size==65536
        if index==0:
            checkpoint=state;assert any(e['domain']=='registry' and e['operation']=='write' and not e['status'] for e in trace['events'])
            assert any(e['domain']=='process' and e['operation']=='create' and e['related'] for e in trace['events'])
        rows.append({'application':app,'status':'PASS','consoleMarker':marker,'events':len(trace['events']),'checkpointSha256':nt.sha(state.read_bytes())})
    # Neither short nor corrupt persisted state is silently reset or exported as success.
    for name,data in [('short',bytes(7)),('corrupt',bytes([0xA5])*65536)]:
        source=folder/(name+'.bin');source.write_bytes(data);cmd=[str(a.build/a.config/'guardian-pe.exe'),str(a.build/'guest'/a.config/'guardian-kernel-security.gk'),str(folder/'1/runtime.gpv'),str(folder/'1/manifest.json'),str(folder/'1/binding.gnt'),str(folder/(name+'-session')),str(source)]
        refused=subprocess.run(cmd,capture_output=True,text=True,timeout=45);assert refused.returncode and not (folder/(name+'-session')/'security-state.bin').exists()
    (folder/'result.json').write_bytes(nt.canonical({'schema':1,'phase':'14.6','configuration':a.config,'status':'PASS','cases':rows,'negativeStateCases':2,'completeWindowsAdvapi32':False}));print(json.dumps({'status':'PASS','phase':'14.6','cases':len(rows),'negativeStateCases':2}))
if __name__=='__main__':main()
