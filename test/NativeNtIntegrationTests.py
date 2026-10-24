"""Only the MiniNT guest executes the supplied native DLL and test application."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_nt
import nt_compatibility as nt
import pe_volume

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--original',type=Path)
    a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True);folder=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));reports=[]
    for profile in (['win10'] if a.original else nt.PROFILES):
        bundle=folder/profile;manifest=native_nt.bundle(a.build,a.config,bundle,profile,a.original);pe_volume.verify(bundle)
        exe=a.build/a.config/'guardian-pe.exe';kernel=a.build/'guest'/a.config/'guardian-kernel-native-nt.gk'
        result=subprocess.run([str(exe),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/(profile+'-session'))],capture_output=True,text=True,timeout=45)
        (folder/(profile+'-cli.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stdout+result.stderr
        report=json.loads((folder/(profile+'-session')/'boot.json').read_bytes())['boots'][0]
        trace=json.loads((folder/(profile+'-session')/'analysis.json').read_bytes())
        assert report['success'] and 'Native NT matrix' in report['console'] and not report['timedOut'] and not report['memoryFault']
        assert report['ntCompatibility']['targetProfile']==nt.PROFILES[profile][0] and report['processes']['freeBefore']==report['processes']['freeAfter']+21
        assert trace['complete'] and not trace['dropped'] and not trace['rejected'] and not trace['pending']
        assert any(e['object']==8191 and e['status']==0xC0000002 and e['stage']=='result' for e in trace['events'])
        assert any(e['object']==134 and e['status']==0xC0000002 and e['stage']=='result' for e in trace['events'])
        assert any(e['domain']=='file' and e['operation']=='write' and e['stage']=='result' and not e['status'] for e in trace['events'])
        reports.append({'profile':profile,'original':bool(a.original),'fileVersion':manifest['nativeRuntime']['fileVersion'],'dllSha256':manifest['nativeRuntime']['dllSha256'],'events':len(trace['events']),'success':True})
    proof={'schema':1,'phase':'14.3','status':'PASS','configuration':a.config,'reports':reports,'windowsBinaryCompatibility':False,'phase14_4Started':False}
    (folder/'result.json').write_bytes(nt.canonical(proof));print(json.dumps(proof));return 0
if __name__=='__main__':raise SystemExit(main())
