"""Same unmodified SDK executable in native Windows and MiniNT, including resized client pixels."""
from pathlib import Path
import argparse,json,re,shutil,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import software_graphics_runtime as runtime,nt_compatibility as nt,pe_volume
from NativeControlsDriver import Driver

def pixels(path):
    magic,size,maximum,data=path.read_bytes().split(b'\n',3)
    width,height=map(int,size.split());assert magic==b'P6' and maximum==b'255' and len(data)==width*height*3
    return {'width':width,'height':height,'sha256':nt.sha(data)}

def window_run(command,folder,guest,title,size=None,cwd=None,expected=None,presented=False):
    folder.mkdir();process=subprocess.Popen(command,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
    driver=Driver(process.pid,helper='guardian-software-graphics-host.exe',direct=not guest)
    stdout=stderr='';hwnd=None;samples=[];last=None;stable=0
    try:
        start=time.monotonic()
        while process.poll() is None and time.monotonic()-start<35:
            rows=driver.windows();failure=next((r for r in rows if r['title'].endswith('Software Graphics FAIL')),None)
            if failure:driver.post(failure['hwnd'],0x10);break
            ready=next((r for r in rows if r['title'].endswith(title)),None)
            if ready:
                hwnd=ready['hwnd'];assert ready['dpi']==96,'Native comparison requires 96 physical DPI'
                if size:
                    driver.resize(hwnd,*size);driver.post(hwnd,0x102,0xE1);size=None;time.sleep(.15)
                dark=driver.capture(hwnd,folder/'client.ppm',presented=presented);frame=pixels(folder/'client.ppm')
                samples.append(frame)
                stable=stable+1 if dark>20 and frame==(expected or last) else 0;last=frame
                if stable>=2:driver.post(hwnd,0x10);break
            time.sleep(.12)
        stdout,stderr=process.communicate(timeout=18)
        assert process.returncode==0,stdout+stderr
        assert stable>=2,('Client output did not stabilize or differs',guest,expected,last)
        return last,stdout
    finally:
        if process.poll() is None:process.kill();stdout,stderr=process.communicate()
        (folder/'host.log').write_text(stdout+stderr,encoding='utf-8')
        (folder/'history.json').write_bytes(nt.canonical({'history':driver.history,'inputs':driver.inputs,'samples':samples}))

def check_session(folder,mask):
    report=json.loads((folder/'software-graphics.json').read_bytes());trace=json.loads((folder/'analysis.json').read_bytes())
    assert report['completed'] and report['helperExited'] and not report['helperKilled'] and report['helperExitCode']==0
    assert not report['transportFailed'] and not report['hostHandlesInGuest'] and report['guestWindowPixelsTransferred']==0
    assert report['gdiLive']==[0,0,0] and report['liveAtLastReply']==[0,0,0] and report['bitmapBytes']==0
    assert report['softwareBackendsUsed']==mask and report['softwareErrors']==0,report
    assert trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'))
    assert not report['fullDirectX'] and not report['shadersAvailable'] and not report['hardwareAcceleration']
    return {k:report[k] for k in ('softwareCalls','softwarePresents','softwareBackendsUsed','hostNt','targetNt')}

def main():
    p=argparse.ArgumentParser()
    for key in ('build','reference','schema-reference','evidence'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--config',required=True);p.add_argument('--external',type=Path);a=p.parse_args()
    build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True);cases=[]
    app=build/'guest/software-graphics'/a.config/'guardian-software-graphics-scene.exe'
    assets=tuple(build/'software-assets'/a.config/n for n in ('image.png','image.bmp','image.jpg'))
    source_hash=nt.sha(app.read_bytes());isolated=out/'native-input';isolated.mkdir();native=isolated/app.name;shutil.copyfile(app,native)
    for asset in assets:shutil.copyfile(asset,isolated/asset.name)
    assert source_hash==nt.sha(native.read_bytes()) and not list(isolated.glob('*.dll'))
    for label,size in (('normal',None),('resize-input',(440,340))):
        folder=out/label;folder.mkdir();expected,native_log=window_run([str(native)],folder/'native',False,'14.10F Software Graphics ready',size,cwd=isolated)
        bundle=folder/'bundle';runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,app,assets);assert pe_volume.verify(bundle)['phase']=='14.10F'
        session=folder/'session';command=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-software-graphics.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--software-graphics','--seconds','30']
        actual,guest_log=window_run(command,folder/'guest',True,'14.10F Software Graphics ready',size,expected=expected)
        fingerprint=lambda log:re.findall(r'^SOFTWARE_(?:WIC|D3D) .+$',log,re.M)
        assert len(fingerprint(native_log))==4 and fingerprint(native_log)==fingerprint(guest_log),(native_log,guest_log)
        assert re.search(r'SOFTWARE_SCENE checks=\d+ paints=\d+ PASS',guest_log),guest_log
        cases.append({'case':label,'status':'PASS','sameBinarySha256':source_hash,'pixels':actual,'fingerprints':fingerprint(guest_log),'backend':check_session(session,15)})
    if a.external:
        binary=a.external.resolve();receipt=json.loads(binary.with_suffix('.exe.source.json').read_bytes())
        assert nt.sha(binary.read_bytes())==receipt['binarySha256'] and receipt['phase']=='14.10F'
        folder=out/'independent-hello';folder.mkdir()
        expected,_=window_run([str(binary)],folder/'native',False,'Direct2D Demo Application',(460,340),presented=True)
        bundle=folder/'bundle';runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,binary)
        session=folder/'session';command=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-software-graphics.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--software-graphics','--seconds','30']
        actual,_=window_run(command,folder/'guest',True,'Direct2D Demo Application',(460,340),expected=expected,presented=True)
        cases.append({'case':'Microsoft-Direct2DHelloWorld','status':'PASS','sameBinarySha256':receipt['binarySha256'],'pixels':actual,'backend':check_session(session,3),'sourceReceipt':receipt})
    assert nt.sha(app.read_bytes())==source_hash
    result={'phase':'14.10F','status':'PASS','configuration':a.config,'cases':cases,'nativeGuestIdenticalExecutable':True,'nativeReferencesHaveNoGuestDlls':True,'fullDirectX':False}
    (out/'result.json').write_bytes(nt.canonical(result));print(json.dumps({'status':'PASS','cases':len(cases),'evidence':str(out)}))
if __name__=='__main__':main()
