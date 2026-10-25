"""End-to-end desktop programs, guest-only callbacks and durable guest settings."""
from pathlib import Path
import argparse,json,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import desktop_runtime as runtime
import nt_compatibility as nt
import pe_volume

def execute(build,config,bundle,session,script=None,state=None):
    command=[str(build/config/'guardian-desktop.exe'),str(build/'guest'/config/'guardian-kernel-desktop.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--seconds','12']
    if script:command+=['--script',str(script)]
    if state:command+=['--state',str(state)]
    run=subprocess.run(command,capture_output=True,text=True,timeout=35)
    session.with_suffix('.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    assert run.returncode==0,run.stdout+run.stderr
    boot=json.loads((session/'boot.json').read_bytes())['boots'][0];trace=json.loads((session/'analysis.json').read_bytes());desktop=json.loads((session/'desktop.json').read_bytes())
    assert boot['success'] and not boot['timedOut'] and not boot['memoryFault'] and not boot['cpu']['kernelFaults']
    assert not any(boot['processes'][key] for key in ('liveProcesses','liveObjects','liveHandles','liveReferences'))
    assert trace['complete'] and not any(trace[key] for key in ('dropped','rejected','pending'))
    assert desktop['completed'] and desktop['frames']>=1 and desktop['history'][-1]['windows']==[]
    assert (session/'security-state.bin').stat().st_size==65536
    assert all(item['deliveredNs']>=item['dueNs'] for item in desktop['deliveries'])
    return boot,trace,desktop

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True);folder=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));rows=[];checkpoint=None
    script=folder/'input.txt';script.write_text('500 down 65 30 1\n580 up 65 30 0\n700 text 233\n900 move 120 130 0\n1400 close\n',encoding='ascii')
    for index,(name,marker) in enumerate([('demo','chars=2 PASS'),('threads','callback count=9 PASS'),('nativeprobe','contexts, synchronization, handles and affinity PASS'),('settings','settings value=1 PASS'),('settings','settings value=2 PASS')]):
        bundle=folder/str(index);application=a.build/'guest/desktop'/a.config/('guardian-desktop-'+name+'.exe');runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference,application);pe_volume.verify(bundle);session=folder/(str(index)+'-session');boot,trace,desktop=execute(a.build,a.config,bundle,session,script if name=='demo' else None,checkpoint if index==4 else None);assert marker in boot['console'],boot['console']
        if index==3:checkpoint=session/'security-state.bin'
        if name=='demo':
            assert desktop['scriptConsumed']==5
            assert any(w['parent'] for frame in desktop['history'] for w in frame['windows'])
            assert any(w['x']==90 and w['width']==460 for frame in desktop['history'] for w in frame['windows'])
        if name=='settings':assert any(e['domain']=='file' and e['operation']=='write' and not e['status'] for e in trace['events'])
        rows.append({'application':name,'status':'PASS','marker':marker,'events':len(trace['events']),'frames':desktop['frames'],'checkpointSha256':nt.sha((session/'security-state.bin').read_bytes())})
    for name,data in [('short',bytes(7)),('corrupt',bytes([0xA5])*65536)]:
        source=folder/(name+'.bin');source.write_bytes(data);bundle=folder/'3';session=folder/(name+'-session');run=subprocess.run([str(a.build/a.config/'guardian-desktop.exe'),str(a.build/'guest'/a.config/'guardian-kernel-desktop.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--state',str(source)],capture_output=True,text=True,timeout=35);assert run.returncode and not (session/'security-state.bin').exists()
    (folder/'result.json').write_bytes(nt.canonical({'schema':1,'phase':'14.7','configuration':a.config,'status':'PASS','cases':rows,'negativeStates':2,'completeWindowsGui':False}));print(json.dumps({'status':'PASS','cases':len(rows),'negativeStates':2}))
if __name__=='__main__':main()
