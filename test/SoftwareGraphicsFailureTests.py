"""Expected API refusals must remain visible while cleanup and transport succeed."""
from pathlib import Path
import argparse,json,re,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import software_graphics_runtime as runtime,nt_compatibility as nt
def main():
    p=argparse.ArgumentParser()
    for key in ('build','reference','schema-reference','evidence'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();build=a.build.resolve()
    out=a.evidence/str(time.time_ns());out.mkdir(parents=True)
    bundle=out/'bundle';runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,build/'guest/software-graphics'/a.config/'guardian-software-graphics-failures.exe')
    run=subprocess.run([str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-software-graphics.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(out/'session'),'--software-graphics','--seconds','30'],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45)
    (out/'host.log').write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr
    found=re.search(r'SOFTWARE_FAILURES checks=(\d+) failures=0',run.stdout);assert found and int(found[1])>=30,run.stdout
    report=json.loads((out/'session/software-graphics.json').read_bytes())
    assert report['completed'] and not report['transportFailed'] and report['helperExited'] and not report['helperKilled']
    assert report['liveAtLastReply']==[0,0,0] and report['gdiLive']==[0,0,0] and not report['bitmapBytes']
    errors=[e for e in report['events'] if e.get('apiHresult',0)<0]
    assert len(errors)>=6 and report['softwareErrors']>=6,report
    trace=json.loads((out/'session/analysis.json').read_bytes());assert trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'))
    (out/'result.json').write_bytes(nt.canonical({'phase':'14.10F','status':'PASS','configuration':a.config,'checks':int(found[1]),'apiFailuresObserved':len(errors),'completed':True,'noResourcesRemaining':True}))
    print(json.dumps({'status':'PASS','checks':int(found[1]),'evidence':str(out)}))
if __name__=='__main__':main()
