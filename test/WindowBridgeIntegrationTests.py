"""Real MiniNT clients presented as independent native Windows HWNDs."""
from pathlib import Path
import argparse,json,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import integrated_runtime as runtime
import nt_compatibility as nt
import pe_volume

PROGRAM_INPUT='''200|12F Main|focus
300|12F Main|down|65|30|1
360|12F Main|up|65|30|0
450|12F Main|text|233
550|12F Main|button|30|70|1
610|12F Main|button|30|70|0
750|12F Main|move|240|160
900|12F Main|resize|480|300
1100|12F Main|scale|144
1300|12F Main|scale|96
1500|12F Main|down|77|50|0
1560|12F Modal|up|77|50|0
1900|12F Modal|down|13|28|0
1960|@focused|up|13|28|0
2200|12F Main|focus
2400|12F Main|close
'''
FLAPPY_INPUT='''400|Bird|down|32|57|0
480|Bird|up|32|57|0
900|Bird|down|32|57|0
980|Bird|up|32|57|0
1400|Bird|down|32|57|0
1480|Bird|up|32|57|0
1900|Bird|down|32|57|0
1980|Bird|up|32|57|0
'''+''.join(f'{ms}|@focused|close\n' for ms in range(2400,6900,500))


def execute(build,config,bundle,session,script=None,state=None,seconds=12,expected=True):
    command=[str(build/config/'guardian-desktop.exe'),str(build/'guest'/config/'guardian-kernel-integrated.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--integrated','--seconds',str(seconds)]
    if script:command+=['--window-script',str(script)]
    if state:command+=['--state',str(state)]
    run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=seconds+20)
    session.with_suffix('.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    native=json.loads((session/'native-windows.json').read_bytes())
    assert native['created'] and native['created']==native['destroyed'] and not native['liveWindows'],native
    if expected:
        assert not run.returncode,run.stdout+run.stderr
        boot=json.loads((session/'boot.json').read_bytes())['boots'][0];bridge=json.loads((session/'window-bridge.json').read_bytes());desktop=json.loads((session/'desktop.json').read_bytes());trace=json.loads((session/'analysis.json').read_bytes())
        assert boot['success'] and not boot['timedOut'] and not native['failure']
        assert not any(boot['processes'][k] for k in ('liveProcesses','liveObjects','liveHandles','liveReferences'))
        assert bridge['completed'] and not bridge['liveGuestWindows'] and not bridge['staging'] and not bridge['hostWindowHandlesInGuest']
        assert trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'))
        assert desktop['completed'] and not desktop['history'][-1]['windows']
        return boot,bridge,native,desktop
    assert run.returncode,run.stdout+run.stderr
    return native


def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--application',type=Path);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--case',choices=('programs','flappy','components'),required=True);a=p.parse_args();build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True,exist_ok=False);proof=[]
    def prepare(label,app,script,assets=(),extras=()):
        folder=out/label;folder.mkdir();bundle=folder/'bundle';manifest=runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,app,extras,assets);assert manifest['phase']=='12F';pe_volume.verify(bundle);path=None
        if script is not None:path=folder/'native-input.txt';path.write_text(script,encoding='ascii')
        return folder,bundle,path
    if a.case=='programs':
        sessions=[];state=None
        for i in range(2):
            folder,bundle,script=prepare('normal-'+str(i),build/'guest/integrated'/a.config/'guardian-integrated-demo.exe',PROGRAM_INPUT)
            boot,bridge,native,desktop=execute(build,a.config,bundle,folder/'session',script,state)
            assert 'modal=1 PASS' in boot['console'],boot['console'];assert native['created']>=3 and native['geometryEvents']>=2 and native['dpiChanges']==2 and native['scriptConsumed']>=15
            assert any(w['title']=='12F Modal' and w['owner'] for f in native['history'] for w in f['windows'])
            assert any(w['title']=='12F Main' and not w['enabled'] for f in native['history'] for w in f['windows'])
            assert any(w['title']=='12F Main' and w['dpi']==144 for f in native['history'] for w in f['windows'])
            assert any(w['title']=='12F Main' and w['x']==240 and w['y']==160 for f in native['history'] for w in f['windows'])
            sessions.append(native['session']);state=folder/'session/security-state.bin';proof.append({'case':'normal-'+str(i),'status':'PASS','nativeWindows':native['created'],'inputs':bridge['inputs'],'frames':bridge['frames']})
        assert sessions[0]!=sessions[1],'Cold start reused session namespace'
        for label,app,text,seconds in (('guest-crash','crash',None,5),('host-deadline','hang',None,3),('bridge-failure','hang','500|12F Main|fail\n',5)):
            folder,bundle,script=prepare(label,build/'guest/integrated'/a.config/('guardian-integrated-'+app+'.exe'),text)
            native=execute(build,a.config,bundle,folder/'session',script,seconds=seconds,expected=False)
            if label=='bridge-failure':assert native['failure']
            if label=='host-deadline':
                # Include cold-start rendering within the deadline, and require
                # the actual watchdog rather than accepting another failure.
                stopped=json.loads((folder/'session/boot.json').read_bytes())['boots'][0]
                assert stopped['timedOut'] and not stopped['success'] and native['initialFrameSynchronized']
            proof.append({'case':label,'status':'PASS','nativeWindowsReclaimed':native['destroyed']})
        folder,bundle,_=prepare('viewer-fallback',build/'guest/desktop'/a.config/'guardian-desktop-threads.exe',None)
        command=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-integrated.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'session'),'--interactive','--seconds','8']
        run=subprocess.run(command,capture_output=True,text=True,timeout=25);(folder/'host.log').write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode and 'callback count=9 PASS' in run.stdout,run.stdout+run.stderr
        desktop=json.loads((folder/'session/desktop.json').read_bytes());assert desktop['completed'] and not desktop['history'][-1]['windows'] and not (folder/'session/native-windows.json').exists();proof.append({'case':'viewer-fallback','status':'PASS'})
    elif a.case=='flappy':
        assert a.application;hashes={str(path.resolve()):nt.sha(nt.regular(path,8<<20)) for path in (a.application,a.application.with_name('settings.txt'),a.application.with_name('icon.ico'))};assert hashes[str(a.application.resolve())]=='f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6'
        variant=out/'variant';variant.mkdir();alternate=variant/'settings.txt';text=a.application.with_name('settings.txt').read_text();alternate.write_text(text.replace('BIRD_SIZE=20','BIRD_SIZE=28').replace('PIPE_WIDTH=80','PIPE_WIDTH=64').replace('PIPE_SPEED=4','PIPE_SPEED=2'),encoding='ascii')
        for i,(settings,bird,pipe) in enumerate(((a.application.with_name('settings.txt'),20,80),(alternate,28,64),(a.application.with_name('settings.txt'),20,80))):
            folder,bundle,script=prepare('flappy-'+str(i),a.application,FLAPPY_INPUT,[settings,a.application.with_name('icon.ico')]);boot,bridge,native,desktop=execute(build,a.config,bundle,folder/'session',script)
            titles={w['title'] for f in native['history'] for w in f['windows']};assert {'Bird','Score','Top Pipe','Bottom Pipe'}<=titles,titles
            assert any(sum(w['visible'] for w in f['windows'])>=4 for f in native['history'])
            birds=[(f['ns'],w) for f in desktop['history'] for w in f['windows'] if w['title']=='Bird'];pipes=[w for f in desktop['history'] for w in f['windows'] if w['title']=='Top Pipe'];assert birds and pipes and all(w['width']==bird for _,w in birds) and all(w['width']==pipe for w in pipes)
            assert min(w['x'] for w in pipes)<max(w['x'] for w in pipes)
            assert bridge['inputs']>=9 and native['scriptConsumed']>=9
            key_time=next(e['ns'] for e in bridge['timeline'] if e['kind']==3);after=[w['y'] for ns,w in birds if key_time+150000000<=ns<=key_time+1200000000];assert len(after)>=2 and any(b<a for a,b in zip(after,after[1:])), 'Native physical input must produce an upward flap'
            assert list((folder/'session').glob('native-window-*.ppm'))
            proof.append({'case':'flappy-'+str(i),'status':'PASS','originalExecutableSha256':hashes[str(a.application.resolve())],'birdWidth':bird,'pipeWidth':pipe,'nativeWindows':native['created'],'frames':bridge['frames']})
        assert hashes=={path:nt.sha(Path(path).read_bytes()) for path in hashes}
    else:
        for cancel in (False,True):
            key=27 if cancel else 13;script=f'500|@focused|text|120\n700|@focused|down|8|14|0\n760|@focused|up|8|14|0\n1200|@focused|down|{key}|28|0\n1280|@focused|up|{key}|28|0\n'
            app=build/'guest/components'/a.config/('guardian-component-controls-cancel.exe' if cancel else 'guardian-component-controls.exe');folder,bundle,path=prepare('dialog-'+str(int(cancel)),app,script,extras=[build/'guest/components'/a.config/'guardian-component-child.exe']);boot,bridge,native,desktop=execute(build,a.config,bundle,folder/'session',path)
            assert 'Component controls dialogs shell PASS' in boot['console']
            assert any(any(w['owner'] for w in f['windows']) and any(not w['enabled'] for w in f['windows']) for f in native['history'])
            proof.append({'case':'dialog-'+str(int(cancel)),'status':'PASS','nativeWindows':native['created'],'inputs':bridge['inputs']})
    (out/'result.json').write_bytes(nt.canonical({'schema':1,'phase':'12F','configuration':a.config,'case':a.case,'status':'PASS','cases':proof,'guestCodeExecutedOnHost':False,'hostHandlesInGuest':False}));print(json.dumps({'status':'PASS','case':a.case,'scenarios':len(proof),'evidence':str(out)}))
if __name__=='__main__':main()
