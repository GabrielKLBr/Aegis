"""Original Flappy, with its own code inside MiniNT and host USER32/GDI drawing."""
from pathlib import Path
import argparse,json,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_controls_runtime as runtime,nt_compatibility as nt
from NativeControlsDriver import Driver
SHA='f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6'
def main():
    p=argparse.ArgumentParser()
    for n in ('build','reference','schema-reference','application','evidence'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True);inputs={str(p.resolve()):nt.sha(nt.regular(p,8<<20)) for p in (a.application,a.application.with_name('settings.txt'),a.application.with_name('icon.ico'))};assert inputs[str(a.application.resolve())]==SHA
    variant=out/'variant';variant.mkdir();settings=variant/'settings.txt';settings.write_text(a.application.with_name('settings.txt').read_text().replace('BIRD_SIZE=20','BIRD_SIZE=28').replace('PIPE_WIDTH=80','PIPE_WIDTH=64').replace('PIPE_SPEED=4','PIPE_SPEED=2'),encoding='ascii');cases=[]
    for i,(config,birdwidth,pipewidth) in enumerate(((a.application.with_name('settings.txt'),20,80),(settings,28,64),(a.application.with_name('settings.txt'),20,80))):
        folder=out/str(i);folder.mkdir()
        ref=subprocess.run([str(build/a.config/'native_controls_layout_reference.exe'),str(birdwidth),str(pipewidth)],capture_output=True,text=True,timeout=15);assert not ref.returncode,ref.stdout+ref.stderr;layout=json.loads(ref.stdout);(folder/'native-reference-layout.json').write_bytes(nt.canonical(layout))
        bundle=folder/'bundle';runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,a.application,assets=[config,a.application.with_name('icon.ico')])
        cmd=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-native-controls.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'session'),'--native-controls','--seconds','15']
        proc=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace');driver=Driver(proc.pid);origin=None;schedule=[(.4,0x100),(.48,0x101),(.9,0x100),(.98,0x101),(1.4,0x100),(1.48,0x101),(1.9,0x100),(1.98,0x101)];keys=[];closed=0;score=False;started=time.monotonic()
        try:
            while proc.poll() is None and time.monotonic()-started<18:
                rows=driver.windows();bird=next((w for w in rows if w['title'].endswith('Bird')),None);score_window=next((w for w in rows if w['title'].endswith('Score')),None)
                if bird and origin is None:origin=time.monotonic();driver.start=origin;driver.history[-1]['seconds']=0
                elapsed=time.monotonic()-origin if origin else 0
                if bird and schedule and elapsed>=schedule[0][0]:
                    _,msg=schedule.pop(0);driver.post(bird['hwnd'],msg,32,57<<16);keys.append({'seconds':elapsed,'message':msg})
                if score_window and elapsed>1 and not score:
                    dark=driver.capture(score_window['hwnd'],folder/'score-native.ppm');score=dark>=12
                if origin and elapsed>=2.5+closed*.45:
                    if bird:driver.post(bird['hwnd'],0x10)
                    elif rows:
                        for row in rows:driver.post(row['hwnd'],0x10)
                    closed+=1
                for row in rows:
                    if not any(row['title'].endswith(t) for t in ('Bird','Score','Top Pipe','Bottom Pipe')):
                        button=driver.u.GetDlgItem(row['hwnd'],1)
                        if button:driver.click(button)
                time.sleep(.025)
            stdout,stderr=proc.communicate(timeout=20)
        finally:
            if proc.poll() is None:proc.kill();proc.communicate()
        (folder/'host.log').write_text(stdout+stderr,encoding='utf-8');(folder/'native-history.json').write_bytes(nt.canonical({'history':driver.history,'keys':keys,'inputs':driver.inputs}))
        assert proc.returncode==0,stdout+stderr
        report=json.loads((folder/'session/native-controls.json').read_bytes());assert report['completed'] and report['helperExited'] and not report['helperKilled'] and report['liveAtLastReply']==[0,0,0] and report['gdiLive']==[0,0,0] and report['bitmapBytes']==0 and report['guestWindowPixelsTransferred']==0 and not report['pixelPresenterFallback'] and not report['hostHandlesInGuest'];assert report['gdiCalls'][9]>0 and report['gdiCalls'][22]>0 and report['gdiCalls'][29]>0
        windows=[(frame['seconds'],row) for frame in driver.history for row in frame['windows']];birds=[(t,w) for t,w in windows if w['title'].endswith('Bird')];pipes=[w for _,w in windows if w['title'].endswith('Top Pipe')];assert birds and pipes and score and len(keys)==8,(len(birds),len(pipes),score,len(keys));assert all(w['width']==layout['bird'] for _,w in birds) and all(w['width']==layout['pipe'] for w in pipes);assert min(w['x'] for w in pipes)<max(w['x'] for w in pipes)
        heights=[w['y'] for t,w in birds if .55<=t<=1.25];assert len(heights)>=2 and any(b<a for a,b in zip(heights,heights[1:])),heights
        trace=json.loads((folder/'session/analysis.json').read_bytes());assert trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'));assert any(e['domain']=='file' and e['operation']=='read' and e['resource'].lower().endswith('settings.txt') and not e['status'] for e in trace['events'])
        cases.append({'case':str(i),'status':'PASS','originalSha256':SHA,'birdWidth':birdwidth,'pipeWidth':pipewidth,'nativeLayout':layout,'nativeHistoryFrames':len(driver.history),'nativeScoreGlyphs':score,'physicalKeyEvents':len(keys)})
    assert inputs=={p:nt.sha(Path(p).read_bytes()) for p in inputs}
    (out/'result.json').write_bytes(nt.canonical({'phase':'14.10D','status':'PASS','configuration':a.config,'cases':cases,'inputs':inputs,'pixelFallback':False}));print(json.dumps({'status':'PASS','cases':len(cases),'evidence':str(out)}))
if __name__=='__main__':main()
