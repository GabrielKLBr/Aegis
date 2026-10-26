"""Package binding, phase capability and host-only action parser checks."""
from pathlib import Path
import argparse,json,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import integrated_runtime as runtime
import internet_runtime,nt_compatibility as nt,pe_volume


def rejected(action):
    try:action()
    except (ValueError,KeyError,OSError,AssertionError):return
    raise AssertionError('Malformed integrated package accepted')


def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='aegis-12f-tools-') as tmp:
        folder=Path(tmp);bundle=folder/'bundle';manifest=runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference);assert manifest['phase']=='12F' and manifest['phase12FStarted'] and not manifest['presentationRuntime']['hostHandlesInGuest'];assert nt.canonical(pe_volume.verify(bundle))==nt.canonical(manifest)
        kernel=a.build/'guest'/a.config/'guardian-kernel-integrated.gk';old=a.build/'guest'/a.config/'guardian-kernel-internet.gk';app=a.build/'guest/integrated'/a.config/'guardian-integrated-demo.exe'
        rejected(lambda:internet_runtime.bundle(a.build,a.config,folder/'old-kernel',a.reference,a.schema_reference,app,kernel=old,presentation=True))
        original=(bundle/'binding.gnt').read_bytes();bad=bytearray(original);bad[-1]^=1;(bundle/'binding.gnt').write_bytes(bad);rejected(lambda:pe_volume.verify(bundle));(bundle/'binding.gnt').write_bytes(original)
        scripts=['0|12F Main|raw-message|1\n','0|12F Main|down|0|30|0\n','0|12F Main|text|55296\n','0|12F Main|resize|1281|720\n','0|12F Main|scale|600\n','0||close\n','10|12F Main|close\n5|12F Main|close\n','0|12F Main|close|extra\n','30001|12F Main|close\n']
        for index,content in enumerate(scripts):
            script=folder/(str(index)+'.txt');script.write_text(content,encoding='ascii');output=folder/('rejected-'+str(index));command=[str(a.build/a.config/'guardian-desktop.exe'),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(output),'--integrated','--window-script',str(script)];run=subprocess.run(command,capture_output=True,text=True,timeout=15);assert run.returncode and not output.exists(),run.stdout+run.stderr
        script=folder/'requires-mode.txt';script.write_text('0|12F Main|close\n',encoding='ascii');output=folder/'without-mode';run=subprocess.run([str(a.build/a.config/'guardian-desktop.exe'),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(output),'--window-script',str(script)],capture_output=True,text=True,timeout=15);assert run.returncode and not output.exists()
        existing=folder/'existing-output';existing.mkdir();marker=existing/'native-windows.json';preserved=b'previous session report must remain unchanged\n';marker.write_bytes(preserved)
        run=subprocess.run([str(a.build/a.config/'guardian-desktop.exe'),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(existing),'--integrated'],capture_output=True,text=True,timeout=15)
        assert run.returncode and marker.read_bytes()==preserved and {x.name for x in existing.iterdir()}=={'native-windows.json'},'Pre-existing output was modified'
    print(json.dumps({'status':'PASS','phase':'12F','negativePackages':2,'negativeHostScripts':len(scripts)+1,'negativeExistingOutputs':1,'rawUser32Passthrough':False}))
if __name__=='__main__':main()
