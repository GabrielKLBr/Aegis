"""Compare expanded native GDI pixels/metrics with the same SDK scene; exercise host window input."""
from pathlib import Path
import argparse,json,re,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import expanded_gdi_runtime as runtime,nt_compatibility as nt
from NativeControlsDriver import Driver
def fingerprint(text):
    found=re.findall(r'^EXPANDED_(?:METRICS|RESULT) .+$',text,re.M)
    assert len(found)==2 and 'failures=0' in found[1],text
    return found
def main():
    p=argparse.ArgumentParser()
    for n in ('build','reference','schema-reference','evidence'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True);rows=[]
    native=subprocess.run([str(build/a.config/'expanded_gdi_reference.exe')],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
    (out/'reference.log').write_text(native.stdout+native.stderr,encoding='utf-8');assert not native.returncode,native.stdout+native.stderr;expected=fingerprint(native.stdout)
    for label in ('normal','input-resize'):
        folder=out/label;folder.mkdir();bundle=folder/'bundle';runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,build/'guest/expanded-gdi'/a.config/'guardian-expanded-gdi-scene.exe')
        cmd=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-expanded-gdi.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'session'),'--expanded-gdi','--seconds','30']
        proc=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace');driver=Driver(proc.pid,helper='guardian-expanded-gdi-host.exe');ready=None;captured=False;started=time.monotonic();stage=0
        try:
            while proc.poll() is None and time.monotonic()-started<35:
                windows=driver.windows();window=next((w for w in windows if w['title'].endswith('14.10E GDI Gallery ready')),None)
                if window:
                    if ready is None:ready=time.monotonic()
                    if label=='input-resize' and stage==0:
                        driver.resize(window['hwnd'],620,420);driver.post(window['hwnd'],0x102,ord('A'));driver.post(window['hwnd'],0x102,0xE1);driver.click(window['hwnd']);stage=1;ready=time.monotonic()
                    if time.monotonic()-ready>.5:
                        captured=driver.capture(window['hwnd'],folder/'native-client.ppm')>20
                        driver.post(window['hwnd'],0x10);break
                time.sleep(.025)
            stdout,stderr=proc.communicate(timeout=25)
        finally:
            if proc.poll() is None:proc.kill();proc.communicate()
        (folder/'host.log').write_text(stdout+stderr,encoding='utf-8');(folder/'native-history.json').write_bytes(nt.canonical({'history':driver.history,'inputs':driver.inputs}))
        assert not proc.returncode,stdout+stderr;assert fingerprint(stdout)==expected,(expected,stdout);assert captured
        paint=re.search(r'EXPANDED_INPUT keys=(\d+) clicks=(\d+) paints=(\d+)',stdout);assert paint and 1<=int(paint.group(3))<=6,stdout
        if label=='input-resize':assert 'EXPANDED_INPUT keys=2 clicks=1' in stdout,stdout
        report=json.loads((folder/'session/expanded-gdi.json').read_bytes());trace=json.loads((folder/'session/analysis.json').read_bytes())
        assert report['completed'] and report['helperExited'] and not report['helperKilled'] and not report['hostHandlesInGuest']
        assert report['nativeGuiBackend']=='win32-expanded-gdi' and report['guestWindowPixelsTransferred']==0 and not report['pixelPresenterFallback']
        assert report['liveAtLastReply']==[0,0,0] and report['gdiLive']==[0,0,0] and report['bitmapBytes']==0
        assert report['gdiCalls'][0]>=80 and trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'))
        rows.append({'case':label,'status':'PASS','pixelsAndMetricsMatched':True,'nativeClientCaptured':True,'extendedCalls':report['gdiCalls'][0],'hostNt':report['hostNt'],'targetNt':report['targetNt']})
    (out/'result.json').write_bytes(nt.canonical({'phase':'14.10E','status':'PASS','configuration':a.config,'reference':expected,'cases':rows,'pixelFallback':False}));print(json.dumps({'status':'PASS','cases':len(rows),'evidence':str(out)}))
if __name__=='__main__':main()

