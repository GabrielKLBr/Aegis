"""Check the signed phase-18 portable payload and its virtualization-independent loader."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess
def imports(path):
    data=path.read_bytes();u16=lambda at:struct.unpack_from('<H',data,at)[0];u32=lambda at:struct.unpack_from('<I',data,at)[0]
    pe=u32(0x3c);assert data[pe:pe+4]==b'PE\0\0';optional=pe+24;assert u16(optional)==0x20b
    sections=[];table=optional+u16(pe+20)
    for i in range(u16(pe+6)):
        at=table+40*i;sections.append((u32(at+12),max(u32(at+8),u32(at+16)),u32(at+20)))
    def offset(rva):
        if rva<u32(optional+60):return rva
        for start,length,raw in sections:
            if start<=rva<start+length:return raw+rva-start
        raise ValueError('Invalid PE directory RVA')
    def names(index,stride,name_offset):
        rva=u32(optional+112+8*index)
        if not rva:return set()
        at=offset(rva);result=set()
        for _ in range(1024):
            descriptor=data[at:at+stride]
            if not any(descriptor):return result
            name_rva=u32(at+name_offset);start=offset(name_rva);end=data.index(b'\0',start,start+256);result.add(data[start:end].decode('ascii').lower());at+=stride
        raise ValueError('PE import quota')
    return names(1,20,12),names(13,32,4)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.package.resolve();manifest=json.loads((root/'aegis.manifest.json').read_bytes());assert manifest['kind']=='portable' and manifest['schema']==1
    for name,item in manifest['files'].items():
        path=root/name;assert path.is_file() and path.stat().st_size==item['bytes'];assert hashlib.sha256(path.read_bytes()).hexdigest()==item['sha256']
    result=subprocess.run([str(root/'engines/guardian-distribution.exe'),'verify',str(root),'portable'],capture_output=True,timeout=45,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));assert result.returncode==0,result.stderr
    forbidden={'ntdll.dll','kernel32.dll','kernelbase.dll','ntoskrnl.exe','winload.efi'};assert not {Path(n).name.lower() for n in manifest['files']} & forbidden
    assert not (root/'engines/runtime').exists() and not list(root.glob('*.msi'))
    for name in ['guardian-engine-service.exe','guardian-scan-worker.exe']:
        eager,delayed=imports(root/'engines'/name);assert 'winhvplatform.dll' not in eager and 'winhvplatform.dll' in delayed,(name,eager,delayed)
    if manifest['configuration']=='Release':
        for folder in ['runtime','engines']:
            for name in ['msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll']:assert (root/folder/name).is_file()
    assert (root/'notices/components.json').is_file() and len(list((root/'notices').rglob('*LICENSE*')))+len(list((root/'notices').rglob('*license*')))>0
    proof={'phase':'18','status':'PASS','files':len(manifest['files']),'identity':json.loads(result.stdout)['identity'],'memberHashes':'PASS','nativeSignatureVerification':'PASS','noGuestWindowsReferences':'PASS','portableLayout':'PASS','WHPDelayedImports':'PASS','componentNotices':'PASS','releaseCrt':'PASS' if manifest['configuration']=='Release' else 'developer prerequisite'};a.output.write_text(json.dumps(proof,indent=2),'utf-8');print(json.dumps(proof))
if __name__=='__main__':main()
