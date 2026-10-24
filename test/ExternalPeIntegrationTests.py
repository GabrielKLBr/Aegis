from pathlib import Path
import json
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import pe_volume
import nt_compatibility as nt

exe,kernel,core,b,config,evidence=map(Path,sys.argv[1:])
config=str(config)
evidence.mkdir(exist_ok=True,parents=True)
import tempfile
folder=Path(tempfile.mkdtemp(prefix='run-',dir=evidence))
reports=[]
for profile in nt.PROFILES:
    bundle=folder/profile
    manifest=pe_volume.sample(b,config,kernel,bundle,profile)
    pe_volume.verify(bundle)
    parsed=subprocess.run([str(core),str(bundle/'runtime.gpv')],capture_output=True,text=True,timeout=45)
    assert parsed.returncode==0,parsed.stdout+parsed.stderr
    run=subprocess.run([str(exe),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/(profile+'-session'))],capture_output=True,text=True,timeout=90)
    (folder/(profile+'-cli.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
    assert run.returncode==0,run.stdout+run.stderr
    row=json.loads((folder/(profile+'-session')/'boot.json').read_bytes())['boots'][0]
    assert row['success'] and 'PE14.2 large external image' in row['console'] and row['ntCompatibility']['targetProfile']==nt.PROFILES[profile][0]
    assert not row['timedOut'] and not row['memoryFault'] and row['processes']['liveHandles']==row['processes']['liveProcesses']==0
    reports.append({'profile':profile,'success':True,'rootBytes':manifest['files'][0]['bytes'],'files':len(manifest['files'])})
    if profile=='win10':
        data=bytearray((bundle/'runtime.gpv').read_bytes());data[-1]^=1
        bad=folder/'tampered.gpv';bad.write_bytes(data)
        rejected=subprocess.run([str(exe),str(kernel),str(bad),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'tamper-session')],capture_output=True,text=True,timeout=10)
        assert rejected.returncode!=0 and not (folder/'tamper-session').exists()
(folder/'result.json').write_bytes(nt.canonical({'schema':1,'status':'PASS','profiles':reports,'phase14_3Started':False}))
print('Three external volume/NT profiles execute a >2 MiB owned PE; tamper rejected before session PASS')
