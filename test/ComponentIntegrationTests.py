"""Exercise standard Windows-import applications and separate guest COM/RPC servers."""
from pathlib import Path
import argparse,json,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import component_runtime as runtime
import nt_compatibility as nt
import pe_volume

def execute(build,config,bundle,session,script=None,state=None):
    command=[str(build/config/'guardian-desktop.exe'),str(build/'guest'/config/'guardian-kernel-components.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--seconds','20']
    if script:command+=['--script',str(script)]
    if state:command+=['--state',str(state)]
    run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45)
    session.with_suffix('.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    assert run.returncode==0,run.stdout+run.stderr
    boot=json.loads((session/'boot.json').read_bytes())['boots'][0]
    trace=json.loads((session/'analysis.json').read_bytes());desktop=json.loads((session/'desktop.json').read_bytes())
    assert boot['success'] and not boot['timedOut'] and not boot['memoryFault'] and not boot['cpu']['kernelFaults'],boot
    assert not any(boot['processes'][key] for key in ('liveProcesses','liveObjects','liveHandles','liveReferences')),boot['processes']
    assert trace['complete'] and not any(trace[key] for key in ('dropped','rejected','pending'))
    assert desktop['completed'] and desktop['history'][-1]['windows']==[]
    assert any(e['resource']=='Component IPC' for e in trace['events']) if 'component-client' in str(bundle) or 'component-rpc' in str(bundle) else True
    return boot,trace,desktop

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--case',choices=('com','rpc','controls'),required=True);a=p.parse_args()
    a.evidence.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));apps=a.build/'guest/components'/a.config;cases=[]
    names={'com':'client','rpc':'rpc','controls':'controls'};name=names[a.case]
    variants=('', 'persist','crash','timeout') if a.case=='com' else ('','cancel') if a.case=='controls' else ('',)
    for index,variant in enumerate(variants):
        # Independently compiled standard-import clients select error/cancel scenarios.
        application=apps/('guardian-component-'+name+'.exe')
        if variant:application=apps/('guardian-component-'+name+'-'+variant+'.exe')
        extras=[apps/'guardian-component-inproc.dll',apps/'guardian-component-server.exe'] if a.case=='com' else [apps/'guardian-component-child.exe'] if a.case=='controls' else []
        bundle=out/(str(index)+'-component-'+name);runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference,None if a.case=='com' and not variant else application,extras);pe_volume.verify(bundle)
        script=None
        if a.case=='controls':
            script=out/(str(index)+'-input.txt');key=27 if variant=='cancel' else 13
            script.write_text(f'500 text 120\n700 down 8 14 0\n760 up 8 14 0\n1200 down {key} 28 0\n1280 up {key} 28 0\n',encoding='ascii')
        boot,trace,desktop=execute(a.build,a.config,bundle,out/(str(index)+'-session'),script,out/'0-session/security-state.bin' if variant=='persist' else None)
        marker={'com':'Component client PASS','rpc':'Component local RPC PASS','controls':'Component controls dialogs shell PASS'}[a.case]
        assert marker in boot['console'],boot['console']
        if a.case=='com':
            assert 'Component inproc and STA marshaling PASS' in boot['console'] and 'Component outproc automation errors shutdown PASS' in boot['console']
            assert any(e['domain']=='process' and e['operation']=='create' and not e['status'] for e in trace['events'])
        if a.case=='rpc':assert 'RPC server shutdown PASS' in boot['console']
        if a.case=='controls':
            assert desktop['scriptConsumed']>=4
            assert any(len(frame['windows'])>=8 for frame in desktop['history'])
            assert 'Component Shell child PASS' in boot['console']
            painted=False
            for capture in (out/(str(index)+'-session')).glob('frame-*.json'):
                snapshot=json.loads(capture.read_bytes())
                edits=[w for w in snapshot['windows'] if w['title']=='C:\\App\\note.txt']
                if not edits:continue
                pixels=capture.with_suffix('.ppm').read_bytes().split(b'\n',3)[3];edit=edits[0];x,y=edit['clientX'],edit['clientY']
                black=sum(pixels[((y+dy)*1280+x+dx)*3:((y+dy)*1280+x+dx)*3+3]==bytes(3) for dy in range(20) for dx in range(180))
                painted=painted or black>30
            assert painted,'File dialog edit text was not visibly drawn'
        cases.append({'variant':variant or 'normal','status':'PASS','events':len(trace['events']),'frames':desktop['frames'],'console':boot['console']})
    (out/'result.json').write_bytes(nt.canonical({'phase':'14.8','status':'PASS','configuration':a.config,'case':a.case,'scenarios':cases,'hostActivation':False,'completeCOM':False}));print(json.dumps({'status':'PASS','case':a.case,'scenarios':len(cases)}))
if __name__=='__main__':main()
