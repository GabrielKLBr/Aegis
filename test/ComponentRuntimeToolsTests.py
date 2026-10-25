"""Read-only inspection of owned exports and versioned volume boundaries."""
from pathlib import Path
import argparse,json,struct,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import component_runtime as runtime
import nt_compatibility as nt
import pe_volume

def rejected(action):
    try:action()
    except (ValueError,KeyError,OSError,AssertionError):return
    raise AssertionError('Malformed component package accepted')

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='aegis-component-tools-') as temp:
        folder=Path(temp);result=runtime.exports(folder/'exports');assert result['dlls']==10 and result['implementedExports']>=97
        for dll,names in runtime.DLL_EXPORTS.items():
            exports=nt.InventoryPe(a.build/'guest/components'/a.config/(dll+'.dll')).export_records();available={name for e in exports for name in e['names']};assert set(names.split())<=available
            if dll not in ('combase','shell32'):assert all(e['forwarder'] and e['forwarder'].lower().startswith(('combase.','shell32.')) for e in exports)
        bundle=folder/'bundle';manifest=runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference,a.build/'guest/components'/a.config/'guardian-component-controls.exe',extras=[a.build/'guest/components'/a.config/'guardian-component-child.exe']);assert nt.canonical(manifest)==nt.canonical(pe_volume.verify(bundle))
        assert manifest['phase']=='14.8' and not manifest['componentRuntime']['hostActivation']
        data=bytearray((bundle/'runtime.gpv').read_bytes());version,header=struct.unpack_from('<II',data,8);assert version==2 and header==pe_volume.EXTENDED_HEADER and len(manifest['files'])>16
        for offset,value in ((8,1),(12,pe_volume.HEADER),(24,33)):
            bad=bytearray(data);struct.pack_into('<I',bad,offset,value);struct.pack_into('<Q',bad,40,pe_volume.fnv(bad,True));rejected(lambda:pe_volume.validate_volume(bad))
        bad=bytearray(data);bad[pe_volume.HEADER:pe_volume.HEADER+128]=bad[48:48+128];struct.pack_into('<Q',bad,40,pe_volume.fnv(bad,True));rejected(lambda:pe_volume.validate_volume(bad))
        (bundle/'runtime.gpv').write_bytes(data[:-1]);rejected(lambda:pe_volume.verify(bundle))
        invalid=folder/'private.dll';invalid.write_bytes(b'data');rejected(lambda:runtime.bundle(a.build,a.config,folder/'bad-assets',a.reference,a.schema_reference,assets=[invalid]))
    print(json.dumps({'phase':'14.8','status':'PASS','dlls':10,'negativePackages':6}))
if __name__=='__main__':main()
