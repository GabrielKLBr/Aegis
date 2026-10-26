"""Check owned exports, strict network policy and the GPV2 trust boundary."""
from pathlib import Path
import argparse,json,struct,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import internet_runtime as runtime
import nt_compatibility as nt
import pe_volume


def rejected(action):
    try:action()
    except (ValueError,KeyError,OSError,AssertionError):return
    raise AssertionError('Malformed network input accepted')


def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);a=p.parse_args()
    assert len(runtime.policy({}))==800
    base={'mode':'lab','endpoints':[{'name':'localhost','address':'127.0.0.1','port':443}]}
    assert struct.unpack_from('<4I',runtime.policy(base),8)==(1,800,2,1)
    negatives=[{'unexpected':1},{'mode':'any'},{'requestLimit':True},{'requestLimit':15},{'byteLimit':4095},{'endpoints':base['endpoints']},{'mode':'lab','endpoints':[dict(base['endpoints'][0],address='8.8.8.8')]},{'mode':'authorized','endpoints':base['endpoints']},{'mode':'simulated','endpoints':base['endpoints']},{'mode':'lab','endpoints':base['endpoints']*2}]
    for field,values in {'name':['','-bad','bad-','a..b','UPPER','x'*64],'port':[0,65536,True],'transport':['raw'],'service':['proxy'],'extra':[1]}.items():
        for value in values:negatives.append({'mode':'lab','endpoints':[dict(base['endpoints'][0],**{field:value})]})
    for spec in negatives:rejected(lambda:runtime.policy(spec))
    upstream=nt.ROOT/'guest/third_party/bearssl';pin=json.loads((upstream/'UPSTREAM.json').read_bytes())
    assert pin['commit']=='7bea48e5e850ab4cafbe68d3765cdaba13a86d6f' and not pin['modifiedSources']
    for name,digest in pin['files'].items():assert nt.sha((upstream/name).read_bytes())==digest,name
    with tempfile.TemporaryDirectory(prefix='aegis-internet-tools-') as temp:
        folder=Path(temp);coverage=runtime.exports(folder/'exports');assert coverage['implementedExports']==111 and len(coverage['dlls'])==10
        ordinals=json.loads((nt.ROOT/'config/internet-ordinals.json').read_bytes())
        for dll,names in runtime.DLL_EXPORTS.items():
            exports=nt.InventoryPe(a.build/'guest/internet'/a.config/(dll+'.dll')).export_records();available={name:e for e in exports for name in e['names']};assert set(names.split())==available.keys()
            for name,e in available.items():
                assert e['forwarder']=='guardianinet.'+name
                if name in ordinals.get(dll,{}):assert e['ordinal']==ordinals[dll][name]
        for name in ('network.cfg','root.der'):(folder/name).write_bytes(b'synthetic test input')
        bundle=folder/'bundle';manifest=runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference,assets=[folder/'network.cfg',folder/'root.der']);assert nt.canonical(manifest)==nt.canonical(pe_volume.verify(bundle))
        assert manifest['phase']=='14.9' and not manifest['internetRuntime']['hostTls'] and not manifest['internetRuntime']['hostCredentials']
        data=bytearray((bundle/'runtime.gpv').read_bytes());assert struct.unpack_from('<II',data,8)==(2,pe_volume.EXTENDED_HEADER)
        for offset,value in ((8,1),(12,pe_volume.HEADER),(24,33)):
            bad=bytearray(data);struct.pack_into('<I',bad,offset,value);struct.pack_into('<Q',bad,40,pe_volume.fnv(bad,True));rejected(lambda:pe_volume.validate_volume(bad))
        bad=bytearray(data);bad[200:328]=bad[48:176];struct.pack_into('<Q',bad,40,pe_volume.fnv(bad,True));rejected(lambda:pe_volume.validate_volume(bad))
        bad=bytearray(data);bad[-1]^=1;rejected(lambda:pe_volume.validate_volume(bad))
        (bundle/'runtime.gpv').write_bytes(data[:-1]);rejected(lambda:pe_volume.verify(bundle))
        invalid=folder/'private.dll';invalid.write_bytes(b'data');rejected(lambda:runtime.bundle(a.build,a.config,folder/'bad-assets',a.reference,a.schema_reference,assets=[invalid]))
    print(json.dumps({'phase':'14.9','status':'PASS','dlls':10,'exports':111,'negativePolicies':len(negatives),'upstreamFiles':len(pin['files']),'negativePackages':7}))
if __name__=='__main__':main()
