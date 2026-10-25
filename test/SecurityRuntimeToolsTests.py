from pathlib import Path
import argparse,json,struct,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import security_runtime as runtime
import nt_compatibility as nt
import pe_volume
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);a=p.parse_args();base=a.build/'guest/security'/a.config;coverage=json.loads((a.build/'security-generated/coverage.json').read_bytes())
    exports=nt.InventoryPe(base/'advapi32.dll').export_records();names={n for e in exports for n in e['names']};assert names==set(coverage['exports']) and len(names)>=80 and all(e['type']=='code' for e in exports)
    for dll in ('sechost.dll','userenv.dll'):
        table=nt.InventoryPe(base/dll).export_records();assert table and all(e['forwarder']=='advapi32.'+e['names'][0] for e in table)
    assert all(set(nt.InventoryPe(base/dll).imports())<={'kernel32.dll','advapi32.dll'} for dll in ('advapi32.dll','userenv.dll','sechost.dll'))
    with tempfile.TemporaryDirectory(prefix='aegis-security-tools-') as temp:
        out=Path(temp)/'bundle';manifest=runtime.bundle(a.build,a.config,out,a.reference,a.schema_reference);pe_volume.verify(out);assert manifest['phase']=='14.6' and manifest['securityRuntime']['ownedAdvapi32'] and not manifest['securityRuntime']['completeAdvapi32']
        assert len(manifest['files'])<=16 and manifest['targetNt']==nt.PROFILES['win10'][1:]
        raw=bytearray((out/'runtime.gpv').read_bytes());raw[-1]^=1
        try:pe_volume.validate_volume(raw)
        except ValueError:pass
        else:raise AssertionError('Tampered runtime accepted')
        # A 14.5 kernel cannot be relabelled as an accepted security kernel.
        spec={'phase':'14.6','profile':'win10','root':'c:\\app\\test.exe','files':[{'path':'c:\\app\\test.exe','source':str(base/'guardian-security.exe'),'kind':'pe'}]}
        try:pe_volume.build(spec,a.build/'guest'/a.config/'guardian-kernel-crt.gk',Path(temp)/'wrong-kernel')
        except ValueError as error:assert 'Security kernel required' in str(error)
        else:raise AssertionError('Security capability marker was not enforced')
    print(json.dumps({'status':'PASS','phase':'14.6','exports':len(names),'ownedForwardersVerified':2,'legalInputCodeExecutedOnHost':False}))
if __name__=='__main__':main()
