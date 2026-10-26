"""Independent SDK clients inside MiniNT and explicit loopback reference peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import internet_runtime as runtime
from InternetLab import Lab,FIXTURES

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--case',choices=('crypto','sockets','http','tls','sspi','reference'),required=True);a=p.parse_args()
    out=a.evidence/str(time.time_ns());out.mkdir(parents=True,exist_ok=False);build=a.build.resolve();proof=[]
    def guest(label,app,policy=None,assets=(),state=None):
        folder=out/label;folder.mkdir();bundle=folder/'runtime';application=build/'guest/internet'/a.config/('guardian-internet-'+app+'.exe')
        manifest=runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,application,assets=assets)
        command=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-internet.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'session')]
        if policy is not None:
            (folder/'policy.json').write_text(json.dumps(policy,indent=2)+'\n',encoding='utf-8');path=folder/'policy.bin';path.write_bytes(runtime.policy(policy));command+=['--network-policy',str(path)]
        if state:command+=['--state',str(state)]
        run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45);(folder/'host.log').write_text(run.stdout+run.stderr,encoding='utf-8')
        assert run.returncode==0,run.stdout+run.stderr
        boot=json.loads((folder/'session/boot.json').read_bytes())['boots'][0];assert boot['success'] and not boot['timedOut'] and 'PASS' in run.stdout
        network=json.loads((folder/'session/internet.json').read_bytes());assert not network['activeSockets'] and not network['hostResolverUsed'] and not network['hostTlsUsed']
        if network['mode'] in (0,1):assert network['nativeSocketsOpened']==0
        proof.append({'case':label,'status':'PASS','applicationSha256':manifest['applicationSha256'],'networkMode':network['mode'],'networkEvents':len(network['events']),'stateSha256':hashlib.sha256((folder/'session/security-state.bin').read_bytes()).hexdigest()})
        return folder/'session/security-state.bin'
    if a.case=='crypto':
        state=guest('cryptographic-known-answers','crypto');guest('persistent-key-another-session','persist',state=state)
    elif a.case=='sockets':
        cfg=out/'network.cfg';cfg.write_text('7 80 443 81 0\n',encoding='ascii')
        simulation={'mode':'simulated','endpoints':[{'name':'localhost','address':'198.18.0.254','port':7,'transport':'tcp'},{'name':'localhost','address':'198.18.0.254','port':7,'transport':'udp'},{'name':'localhost','address':'198.18.0.254','port':80,'service':'http'}]}
        guest('simulated-sockets','sockets',simulation,[cfg]);guest('simulated-http','simulated',simulation,[cfg]);guest('offline-policy','offline',assets=[cfg])
        with Lab(out/'peers') as lab:
            cfg=lab.config(out/'lab-input');guest('loopback-sockets','sockets',lab.policy(),[cfg])
        assert any('echoBytes' in e for e in lab.events) and any('udpBytes' in e for e in lab.events)
    elif a.case=='http':
        with Lab(out/'peers') as lab:
            cfg=lab.config(out/'input')
            for label,app in (('http-and-chunking','http'),('wininet-http','wininet'),('async-and-cancel','async'),('receive-timeout','timeout')):guest(label,app,lab.policy(),[cfg])
        assert any(e.get('path')=='/echo' and e.get('bodyBytes')==20 for e in lab.events)
    elif a.case in ('tls','sspi'):
        certificates=json.loads(FIXTURES.read_bytes())['certificates']
        scenarios=[('server',0,'root'),('rsa',0,'root'),('expired',1,'root'),('wrong',1,'root'),('client-only',1,'root'),('bad-signature',1,'root'),('server',1,'other')]
        if a.case=='sspi':scenarios=[('server',0,'root'),('server',3,'root')]
        for i,(certificate,expectation,trust) in enumerate(scenarios):
            folder=out/('input-'+str(i));folder.mkdir();root=folder/'root.der';root.write_bytes(bytes.fromhex(certificates[trust]))
            with Lab(folder/'peers',certificate) as lab:
                cfg=lab.config(folder,expectation);state=guest('tls-'+str(i),'tls' if a.case=='tls' else 'sspi',lab.policy(),[cfg,root])
                if not i and a.case=='tls':
                    cfg=lab.config(folder,2);guest('persisted-virtual-trust-store','tls',lab.policy(),[cfg,root],state=state)
            if expectation==1:assert any(e.get('tlsRejected') for e in lab.events),'Invalid TLS peer accepted application traffic'
            else:assert any(e.get('tlsVersion')=='TLSv1.2' for e in lab.events),'Real TLS negotiation absent'
    else:
        with Lab(out/'peers') as lab:
            cfg=lab.config(out/'input').resolve()
            for name in ('sockets','http','timeout','wininet'):
                run=subprocess.run([str(build/a.config/('internet_'+name+'_reference.exe')),str(cfg)],capture_output=True,text=True,cwd=out.resolve(),timeout=30);(out/(name+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr;proof.append({'case':'native-'+name,'status':'PASS'})
    (out/'result.json').write_text(json.dumps({'status':'PASS','phase':'14.9','suite':a.case,'cases':proof},indent=2)+'\n',encoding='utf-8');print(json.dumps({'status':'PASS','case':a.case,'scenarios':len(proof),'evidence':str(out)}))
if __name__=='__main__':main()
