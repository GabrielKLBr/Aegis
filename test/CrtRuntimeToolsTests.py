from pathlib import Path
import argparse,json,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import crt_runtime as c
import nt_compatibility as nt
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);a=p.parse_args()
    records=c.references(a.reference);assert len(records)==9
    app=nt.InventoryPe(a.build/'guest/crt'/a.config/'guardian-crt.exe');imports=app.imports()
    assert 'msvcp140.dll' in imports and any('cout' in n for n in imports['msvcp140.dll'])
    with tempfile.TemporaryDirectory(prefix='aegis-crt-tools-') as folder:
        root=Path(folder);data=c.exports(root/'exports',a.reference)
        assert data['exports']>=260 and 'CopyFileW' in data['failClosedImports'] and 'AcquireSRWLockExclusive' not in data['failClosedImports']
        (root/'ucrtbase.dll').write_bytes(b'not a valid runtime')
        try:c.references(root,['ucrtbase.dll'])
        except (ValueError,RuntimeError):pass
        else:raise AssertionError('Malformed CRT accepted')
        try:c.bundle(a.build,a.config,root/'win7',a.reference,None,profile='win7')
        except (ValueError,RuntimeError):pass
        else:raise AssertionError('Unsupported runtime / NT combination accepted')
    print(json.dumps({'status':'PASS','phase':'14.5','pinnedReferences':len(records),'exports':data['exports'],'failClosedImports':len(data['failClosedImports'])}))
if __name__=='__main__':main()
