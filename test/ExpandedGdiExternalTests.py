"""Run pinned, independently compiled SDK programs unchanged in native Windows and MiniNT."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import expanded_gdi_runtime as runtime,gdi_corpus,gdi_inventory,nt_compatibility as nt
from NativeControlsDriver import Driver
def pixels(path):
    magic,dimensions,maximum,rgb=path.read_bytes().split(b'\n',3)
    width,height=map(int,dimensions.split());assert magic==b'P6' and maximum==b'255' and len(rgb)==width*height*3
    return {'width':width,'height':height,'sha256':hashlib.sha256(rgb).hexdigest()}
def run_window(command,application,folder,size,guest,expected=None):
    folder.mkdir();proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
    driver=Driver(proc.pid,helper='guardian-expanded-gdi-host.exe',direct=not guest);title={'bezier':'Bezier Splines','clover':'Draw a Clover'}[application];samples=[];result=[];stdout=stderr=''
    try:
        started=time.monotonic();hwnd=None
        while proc.poll() is None and time.monotonic()-started<18:
            rows=driver.windows();window=next((w for w in rows if w['title'].endswith(title)),None)
            if window:hwnd=window['hwnd'];break
            time.sleep(.03)
        if hwnd is None:
            stdout,stderr=proc.communicate(timeout=15);raise AssertionError('Independent window missing: '+stdout+stderr)
        driver.resize(hwnd,*size)
        phases=2 if application=='bezier' and size[0]>400 else 1
        for phase in range(phases):
            if phase:
                driver.post(hwnd,0x200,1,100|(70<<16));driver.post(hwnd,0x200,2,180|(130<<16))
            target=expected[phase] if expected else None;last=None;stable=0;deadline=time.monotonic()+18
            while proc.poll() is None and time.monotonic()<deadline:
                time.sleep(.18)
                dark=driver.capture(hwnd,folder/('client-'+str(phase)+'.ppm'),presented=True);frame=pixels(folder/('client-'+str(phase)+'.ppm'))
                samples.append({'phase':phase,'seconds':time.monotonic()-started,'frame':frame,'darkPixels':dark})
                if dark>50 and (frame==target if target else frame==last):stable+=1
                else:stable=0
                last=frame
                if stable>=2:break
            assert stable>=2,('Independent native pixels differ',application,size,guest,target,last)
            result.append(last)
        driver.post(hwnd,0x10);stdout,stderr=proc.communicate(timeout=18)
        assert not proc.returncode,stdout+stderr
    finally:
        if proc.poll() is None:
            if 'hwnd' in locals() and hwnd:
                try:driver.post(hwnd,0x10);stdout,stderr=proc.communicate(timeout=15)
                except (AssertionError,subprocess.TimeoutExpired):proc.kill();stdout,stderr=proc.communicate()
            else:proc.kill();stdout,stderr=proc.communicate()
        elif not stdout and not stderr:stdout,stderr=proc.communicate()
        (folder/'host.log').write_text(stdout+stderr,encoding='utf-8')
        (folder/'native-history.json').write_bytes(nt.canonical({'history':driver.history,'inputs':driver.inputs,'captures':samples}))
    (folder/'host.log').write_text(stdout+stderr,encoding='utf-8')
    if len(result)==2:assert result[0]!=result[1],'Bezier mouse interaction did not change pixels'
    return result
def main():
    p=argparse.ArgumentParser()
    for n in ('build','reference','schema-reference','evidence'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--corpus-binaries',type=Path);p.add_argument('--config',required=True);a=p.parse_args();build=a.build.resolve();corpus=(a.corpus_binaries or build/'external/gdi')/a.config;out=a.evidence/str(time.time_ns());out.mkdir(parents=True);cases=[];inputs={};receipts={}
    for application in ('bezier','clover','whatsize'):
        for variant in ('Unicode','ANSI'):
            binary,receipt=gdi_corpus.verify(corpus,application,variant);inputs[str(binary.resolve())]=nt.sha(binary.read_bytes());receipts[(application,variant)]=(binary,receipt)
    for application in ('bezier','clover'):
        for variant in ('Unicode','ANSI'):
            binary,receipt=receipts[(application,variant)]
            for size in ((360,280),(560,400)):
                name=application+'-'+variant+'-'+str(size[0]);folder=out/name;folder.mkdir();expected=run_window([str(binary)],application,folder/'reference',size,False)
                bundle=folder/'bundle';runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,binary)
                session=folder/'session';command=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-expanded-gdi.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--expanded-gdi','--seconds','30']
                actual=run_window(command,application,folder/'guest',size,True,expected);assert actual==expected
                report=json.loads((session/'expanded-gdi.json').read_bytes());trace=json.loads((session/'analysis.json').read_bytes())
                assert report['completed'] and report['helperExited'] and not report['helperKilled'] and report['liveAtLastReply']==[0,0,0] and report['gdiLive']==[0,0,0] and not report['bitmapBytes']
                assert report['nativeGuiBackend']=='win32-expanded-gdi' and not report['pixelPresenterFallback'] and report['guestWindowPixelsTransferred']==0 and not report['hostHandlesInGuest'] and report['gdiCalls'][0]>0
                assert trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'))
                cases.append({'case':name,'status':'PASS','nativeAndGuestUseIdenticalBinary':True,'binarySha256':receipt['binarySha256'],'sourceSha256':receipt['sourceSha256'],'pixels':actual,'targetNt':report['targetNt'],'hostNt':report['hostNt']})
    rejected=[]
    for variant in ('Unicode','ANSI'):
        binary,receipt=receipts[('whatsize',variant)];missing=[r for r in gdi_inventory.inspect(binary,build,a.config) if not r['available']]
        expected='wsprintfW' if variant=='Unicode' else 'wsprintfA';assert any(r['function']==expected and r['status']=='pending' for r in missing),missing
        refused=False;output=out/('pending-'+variant)
        try:runtime.bundle(build,a.config,output,a.reference,a.schema_reference,binary)
        except ValueError as error:refused=expected in str(error)
        assert refused and not (output/'runtime.gpv').exists(),'Pending API must not become a successful no-op'
        rejected.append({'application':'whatsize','variant':variant,'pendingImports':missing,'executed':False})
    assert inputs=={name:nt.sha(Path(name).read_bytes()) for name in inputs}
    (out/'result.json').write_bytes(nt.canonical({'phase':'14.10E','status':'PASS','configuration':a.config,'cases':cases,'rejected':rejected,'inputs':inputs,'externalSourcesAndBinariesRedistributed':False,'pixelFallback':False}))
    print(json.dumps({'status':'PASS','cases':len(cases),'rejectedImports':len(rejected),'evidence':str(out)}))
if __name__=='__main__':main()

