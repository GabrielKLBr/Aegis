"""Reject mode/kernel/helper mismatches before opening output or audio devices."""
from pathlib import Path
import argparse,json,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audio_runtime as runtime,nt_compatibility as nt,pe_volume
def main():
    p=argparse.ArgumentParser()
    for key in ('build','reference','schema-reference'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();a.build=a.build.resolve()
    with tempfile.TemporaryDirectory(prefix='aegis-audio-tools-') as directory:
        folder=Path(directory);bundle=folder/'bundle';manifest=runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference);assert pe_volume.verify(bundle)['phase']=='14.10G'
        base=[str(a.build/a.config/'guardian-desktop.exe'),str(a.build/'guest'/a.config/'guardian-kernel-audio.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt')]
        modes=([],['--software-graphics'],['--audio','--expanded-gdi'],['--audio','--integrated'],['--audio','--interactive'],['--audio','--native-controls'])
        for i,options in enumerate(modes):
            output=folder/str(i);run=subprocess.run([*base,str(output),*options],capture_output=True,text=True,timeout=15);assert run.returncode and not output.exists(),run.stdout+run.stderr
        manifest['nativeGuiRuntime']['helperSha256']='0'*64;raw=nt.canonical(manifest);(bundle/'manifest.json').write_bytes(raw);binding=bytearray((bundle/'binding.gnt').read_bytes());binding[48:80]=bytes.fromhex(nt.sha(raw));(bundle/'binding.gnt').write_bytes(binding)
        output=folder/'wrong-helper';run=subprocess.run([*base,str(output),'--audio'],capture_output=True,text=True,timeout=15);assert run.returncode and not output.exists()
    print(json.dumps({'phase':'14.10G','status':'PASS','negativeModes':len(modes),'helperBindingFailures':1}))
if __name__=='__main__':main()
