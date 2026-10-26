from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import internet_runtime,native_gdi_runtime as runtime,nt_compatibility as nt,pe_volume
def main():
    p=argparse.ArgumentParser()
    for name in ('build','reference','schema-reference','evidence'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();a.build=a.build.resolve()
    with tempfile.TemporaryDirectory(prefix='aegis-native-tools-') as tmp:
        folder=Path(tmp);bundle=folder/'bundle';manifest=runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference);assert pe_volume.verify(bundle)['phase']=='14.10C'
        rejected=False
        try:internet_runtime.bundle(a.build,a.config,folder/'old',a.reference,a.schema_reference,a.build/'guest/native-gdi'/a.config/'guardian-native-gdi-demo.exe',kernel=a.build/'guest'/a.config/'guardian-kernel-integrated.gk',native_gdi=True)
        except (ValueError,AssertionError):rejected=True
        assert rejected
        base=[str(a.build/a.config/'guardian-desktop.exe'),str(a.build/'guest'/a.config/'guardian-kernel-native-gdi.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt')]
        for i,options in enumerate(([],['--native-gdi','--integrated'],['--native-gdi','--interactive'])):
            output=folder/('rejected-'+str(i));run=subprocess.run([*base,str(output),*options],capture_output=True,text=True,timeout=15);assert run.returncode and not output.exists()
        original=(bundle/'manifest.json').read_bytes();binding=(bundle/'binding.gnt').read_bytes();manifest['nativeGuiRuntime']['helperSha256']='0'*64;raw=nt.canonical(manifest);(bundle/'manifest.json').write_bytes(raw);bad=bytearray(binding);bad[48:80]=hashlib.sha256(raw).digest();(bundle/'binding.gnt').write_bytes(bad);output=folder/'wrong-helper';run=subprocess.run([*base,str(output),'--native-gdi'],capture_output=True,text=True,timeout=15);assert run.returncode and not output.exists()
        (bundle/'manifest.json').write_bytes(original);(bundle/'binding.gnt').write_bytes(binding)
        output=folder/'existing';output.mkdir();marker=output/'native-gdi.json';marker.write_bytes(b'previous session');run=subprocess.run([*base,str(output),'--native-gdi'],capture_output=True,text=True,timeout=15);assert run.returncode and marker.read_bytes()==b'previous session' and {p.name for p in output.iterdir()}=={'native-gdi.json'}
    print(json.dumps({'status':'PASS','phase':'14.10C','negativeKernel':1,'negativeModes':3,'negativeHelperBinding':1,'preservedOutput':1}))
if __name__=='__main__':main()
