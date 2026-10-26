"""Compare the same SDK GDI program in MiniNT and native Windows."""
from pathlib import Path
import argparse,json,re,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_gdi_runtime as runtime
import nt_compatibility as nt
import pe_volume
from NativeUserIntegrationTests import own_helper
def result(text):
    found=re.findall(r'^GDI_RESULT .+$',text,re.M)
    assert len(found)==1,text
    return found[0].strip()
def main():
    p=argparse.ArgumentParser()
    for name in ('build','reference','schema-reference','evidence'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True);cases=[]
    reference=subprocess.run([str(build/a.config/'native_gdi_reference.exe')],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=20);(out/'reference.log').write_text(reference.stdout+reference.stderr,encoding='utf-8');assert not reference.returncode,reference.stdout+reference.stderr;expected=result(reference.stdout)
    for label,mode,seconds,passed in [('normal-0','demo',25,True),('normal-1','demo',25,True),('guest-crash','crash',5,False),('guest-deadline','hang',1,False),('helper-death','hang',5,False)]:
        folder=out/label;folder.mkdir();bundle=folder/'bundle';session=folder/'session';manifest=runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,build/'guest/native-gdi'/a.config/('guardian-native-gdi-'+mode+'.exe'));assert manifest['phase']=='14.10C' and manifest['nativeGuiRuntime']['protocol']==3;pe_volume.verify(bundle)
        command=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-native-gdi.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--native-gdi','--seconds',str(seconds)]
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
        try:
            killed=own_helper(process.pid,'kill','guardian-native-gdi-host.exe') if label=='helper-death' else None
            stdout,stderr=process.communicate(timeout=seconds+18)
        finally:
            if process.poll() is None:process.kill();process.communicate()
        (folder/'host.log').write_text(stdout+stderr,encoding='utf-8');report=json.loads((session/'native-gdi.json').read_bytes());assert report['helperExited'] and report['restrictions']==31 and report['inheritedHandles']==2 and report['processLimit']==1 and not report['hostHandlesInGuest'];assert report['hostNt'][0]>=10 and report['targetNt']==[10,0,19045] and report['logicalDpi']==96
        if passed:
            assert process.returncode==0,stdout+stderr
            actual=result(stdout);assert actual==expected,(expected,actual)
            boot=json.loads((session/'boot.json').read_bytes())['boots'][0];trace=json.loads((session/'analysis.json').read_bytes());assert boot['success'] and trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'));assert report['completed'] and not report['helperKilled'] and report['helperExitCode']==0 and report['gdiLive']==[0,0,0] and report['bitmapBytes']==0 and report['liveAtLastReply']==[0,0,0];assert report['guestWindowPixelsTransferred']==0 and report['guestPixelsTransferred']>=256;assert report['uploadedBitmapBytes']>=1024 and report['downloadedBitmapBytes']>=160*112*4
            for op in tuple(range(1,38)):assert report['gdiCalls'][op]>0,(op,report['gdiCalls'])
        else:
            assert process.returncode and not report['completed'],stdout+stderr
            if killed:assert report['helperPid']==killed and report['helperExitCode']==19
        cases.append({'case':label,'status':'PASS','completed':report['completed'],'nativeCalls':report['gdiCalls'],'referenceMatched':passed})
    proof={'phase':'14.10C','status':'PASS','configuration':a.config,'reference':expected,'cases':cases,'nativeGdi':True,'completeWindowsGui':False};(out/'result.json').write_bytes(nt.canonical(proof));print(json.dumps({'status':'PASS','cases':len(cases),'evidence':str(out)}))
if __name__=='__main__':main()
