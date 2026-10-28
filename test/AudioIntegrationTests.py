"""Same SDK/independent executables run natively and inside MiniNT."""
from pathlib import Path
import argparse,json,shutil,subprocess,sys,time,wave
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audio_runtime as runtime,nt_compatibility as nt,pe_volume,audio_corpus
def main():
    p=argparse.ArgumentParser()
    for key in ('build','reference','schema-reference','evidence'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--config',required=True);p.add_argument('--external',type=Path);a=p.parse_args();build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True)
    cases=[]
    asset=out/'tone.wav'
    with wave.open(str(asset),'wb') as wav:wav.setparams((2,2,48000,480,'NONE','not compressed'));wav.writeframes(bytes(1920))
    def run(command,log,cwd=None):
        result=subprocess.run(command,cwd=cwd,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45)
        log.write_text(result.stdout+result.stderr,encoding='utf-8');assert not result.returncode,result.stdout+result.stderr
        return result.stdout
    for name,app in [('sdk',build/'guest/audio'/a.config/'guardian-audio-client.exe'),('independent',a.external.resolve() if a.external else build/'external/audio'/a.config/'whisky-audio-test.exe')]:
        if name=='independent':audio_corpus.verify(app)
        folder=out/name;folder.mkdir();native=folder/'native';native.mkdir();shutil.copyfile(app,native/app.name);digest=nt.sha(app.read_bytes())
        shutil.copyfile(asset,native/asset.name)
        stdout=run([str(native/app.name)],folder/'native.log',native)
        assert 'failures=0' in stdout if name=='sdk' else '"status":"ok"' in stdout
        bundle=folder/'bundle';runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,app,(asset,));assert pe_volume.verify(bundle)['phase']=='14.10G'
        session=folder/'session'
        stdout=run([str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-audio.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--audio','--seconds','30'],folder/'guest.log')
        assert 'failures=0' in stdout if name=='sdk' else '"status":"ok"' in stdout
        report=json.loads((session/'audio.json').read_bytes());assert report['completed'] and report['helperExitCode']==0 and not report['helperKilled'] and not report['transportFailed']
        assert report['gdiLive']==[0,0,0] and report['bitmapBytes']==0 and report['audioSubmittedBytes']>0 and not report['hostCapture']
        assert report['audioBackendsUsed']==(3 if name=='sdk' else 1)
        assert nt.sha(app.read_bytes())==digest==nt.sha((native/app.name).read_bytes())
        cases.append({'case':name,'sha256':digest,'backends':report['audioBackendsUsed'],'submittedBytes':report['audioSubmittedBytes']})
    # Exercise policies through real SDK imports, never through custom app calls.
    folder=out/'negative';folder.mkdir();bundle=folder/'bundle';runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,build/'guest/audio'/a.config/'guardian-audio-failures.exe',(asset,))
    stdout=run([str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-audio.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'session'),'--audio','--seconds','30'],folder/'guest.log')
    assert 'failures=0' in stdout
    report=json.loads((folder/'session/audio.json').read_bytes());assert report['completed'] and report['helperExitCode']==0 and not report['helperKilled'] and not report['transportFailed']
    assert report['gdiLive']==[0,0,0] and report['bitmapBytes']==0 and not report['hostCapture']
    proof={'phase':'14.10G','status':'PASS','configuration':a.config,'cases':cases,'negativeGuestCase':True,'earlierPhasesRevalidated':False,'sameNativeAndGuestBinaries':True,'capture':False,'mediaFoundation':False}
    (out/'result.json').write_bytes(nt.canonical(proof));print(json.dumps(proof))
if __name__=='__main__':main()
