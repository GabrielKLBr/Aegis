"""Phase 15 only. All destructive operations target fresh, inert test files."""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,shutil,sqlite3,subprocess,time,tempfile
from ctypes import wintypes as w
import validate_frameworks as frozen

ROOT=Path(__file__).resolve().parents[1]

def sha(data):return hashlib.sha256(data).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)
def binaries(build,config):
    return {config+'/'+n:frozen.sha(build/config/n) for n in ('guardian-quarantine.exe','guardian-quarantine-test-driver.exe','guardian-engine-service.exe','avui.exe','av.exe','guardian-scan-worker.exe','guardian-whp-memory.exe','guardian-service-sample.exe','guardian-runtime.exe','guardian-desktop.exe','guardian-native-user-host.exe','guardian-audio-host.exe','guardian-kernel-audio.gk')}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--config',choices=('Release','Debug'),required=True);a=parser.parse_args();build=a.build.resolve();out=a.output.resolve();require(not out.exists(),'Fresh evidence directory required');out.mkdir(parents=True)
    inputs=frozen.inputs();artifacts=binaries(build,a.config);rows=[];exe=build/a.config/'guardian-quarantine.exe';driver=build/a.config/'guardian-quarantine-test-driver.exe'
    def record(name,ok,**extra):
        row={'case':name,'passed':bool(ok),**extra};rows.append(row);(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(row),flush=True);require(ok,name)
    def case(name):
        p=out/name;p.mkdir();return p,p/'vault'
    def call(vault,action='list',args=(),test=False,crash=False):
        process=subprocess.run([str(driver if test else exe),'--root',str(vault),'--action',action,*map(str,args)],capture_output=True,timeout=45)
        if crash:require(process.returncode==77,'Fault driver did not terminate at its checkpoint');return None
        require(process.stdout.strip(),'Quarantine controller returned no report');report=json.loads(process.stdout)
        require(report['type']=='quarantine' and process.returncode==(0 if report['complete'] else 3),'CLI status contradicts report');return report
    def isolate(vault,source,**kw):return call(vault,'isolate',('--input',source),**kw)
    def restore(vault,id,parent,name='restored.bin',**kw):return call(vault,'restore',('--id',id,'--directory',parent,'--filename',name),**kw)
    def payload(p,data=b'INERT_AEGIS_PHASE15_PAYLOAD\0\xff'):p.write_bytes(data);return data
    p,v=case('roundtrip');source=p/'original-ação-🧪.bin';data=payload(source,b'MZ-NOT-AN-EXECUTABLE\0'+b'PHASE15_INERT_MARKER_'*1000);stamp=time.time_ns()-10_000_000_000;os.utime(source,ns=(stamp,stamp));expected_time=source.stat().st_mtime_ns
    report=isolate(v,source);id=report['id'];record('verified-isolation-removes-original',report['complete'] and not source.exists() and report['sha256']==sha(data))
    listing=call(v);record('encrypted-metadata-list',len(listing['entries'])==1 and listing['entries'][0]['source']==str(source) and listing['entries'][0]['state']=='isolated')
    database=(v/'vault.sqlite').read_bytes();record('no-plaintext-payload-or-source-in-database',b'PHASE15_INERT_MARKER_' not in database and source.name.encode('utf-8') not in database and b'MZ-NOT-AN-EXECUTABLE' not in database)
    destination=p/'restored-ação-🧪.bin';report=restore(v,id,p,destination.name);record('restore-exact-bytes-and-timestamp',report['complete'] and destination.read_bytes()==data and destination.stat().st_mtime_ns==expected_time)
    record('restore-retains-recovery-copy',len(call(v)['entries'])==1 and report['vaultCopyRetained'])
    destination.write_bytes(b'Existing unrelated file');report=restore(v,id,p,destination.name);record('collision-never-overwrites',not report['complete'] and destination.read_bytes()==b'Existing unrelated file')
    report=call(v,'erase',('--id',id));record('erase-only-vault-entry',report['complete'] and not call(v)['entries'] and destination.read_bytes()==b'Existing unrelated file' and not report['physicalSecureEraseGuaranteed'])
    audit=call(v,'audit')['events'];record('authenticated-audit-success-and-failure',all(e['seq']==i+1 for i,e in enumerate(audit)) and any(e['action']=='restore-failed' for e in audit) and audit[-1]['action']=='erased')
    for name,blob in (('empty',b''),('large',bytes(range(256))*8192)):
        p,v=case(name);f=p/'input.bin';payload(f,blob);r=isolate(v,f);t=restore(v,r.get('id',''),p);record('roundtrip-'+name,r['complete'] and t['complete'] and (p/'restored.bin').read_bytes()==blob)
    advapi=ctypes.WinDLL('advapi32',use_last_error=True);advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes=[w.LPCWSTR,w.DWORD,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(w.DWORD)];advapi.GetSecurityDescriptorDacl.argtypes=[ctypes.c_void_p,ctypes.POINTER(w.BOOL),ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(w.BOOL)];advapi.SetFileSecurityW.argtypes=[w.LPCWSTR,w.DWORD,ctypes.c_void_p];advapi.GetFileSecurityW.argtypes=[w.LPCWSTR,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD)];kernel_for_acl=ctypes.WinDLL('kernel32');kernel_for_acl.LocalFree.argtypes=[ctypes.c_void_p]
    def acl_bytes(path):
        needed=w.DWORD();advapi.GetFileSecurityW(str(path),4,None,0,ctypes.byref(needed));buffer=ctypes.create_string_buffer(needed.value);require(advapi.GetFileSecurityW(str(path),4,buffer,needed,ctypes.byref(needed)),'Read test ACL');present=w.BOOL();default=w.BOOL();acl=ctypes.c_void_p();require(advapi.GetSecurityDescriptorDacl(buffer,ctypes.byref(present),ctypes.byref(acl),ctypes.byref(default)) and acl,'Test ACL missing');size=ctypes.c_uint16.from_address(acl.value+2).value;return ctypes.string_at(acl,size)
    p,v=case('permissions');f=p/'input.bin';payload(f);original_acl=acl_bytes(f);r=isolate(v,f);restored=restore(v,r['id'],p);record('restore-preserves-dacl-aces',restored['complete'] and acl_bytes(p/'restored.bin')==original_acl)
    # The vault root has a protected DACL with no inherited ACEs.
    needed=w.DWORD();advapi.GetFileSecurityW(str(v),4,None,0,ctypes.byref(needed));buffer=ctypes.create_string_buffer(needed.value);require(advapi.GetFileSecurityW(str(v),4,buffer,needed,ctypes.byref(needed)),'Read vault ACL');advapi.GetSecurityDescriptorControl.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint16),ctypes.POINTER(w.DWORD)];control=ctypes.c_uint16();revision=w.DWORD();require(advapi.GetSecurityDescriptorControl(buffer,ctypes.byref(control),ctypes.byref(revision)),'Vault ACL control');record('vault-dacl-protected',bool(control.value&0x1000))
    p,v=case('journal-failure');f=p/'source.bin';data=payload(f);r=isolate(v,f);r=call(v,'restore',('--id',r['id'],'--directory',p,'--filename','recovery.bin','--fault','restore-journal'),test=True);record('journal-failure-allows-explicit-verified-recovery',not r['complete'] and r['copyRestored'] and not r['historyUpdated'] and r['vaultCopyRetained'] and (p/'recovery.bin').read_bytes()==data and len(call(v)['entries'])==1)
    p,v=case('audit-pages');call(v,'list',('--seed-audit','130'),test=True);page=call(v,'audit');earlier=call(v,'audit',('--before',page['nextBefore']));record('audit-cursor-complete-bounded-pages',len(page['events'])==128 and [e['seq'] for e in earlier['events']+page['events']]==list(range(1,131)) and not earlier['nextBefore'] and page['totalEvents']==130)
    p,v=case('audit-capacity');f=p/'source.bin';data=payload(f);r=isolate(v,f);id=r['id'];call(v,'list',('--seed-audit','8190'),test=True);restored=restore(v,id,p);record('audit-capacity-preserves-explicit-recovery',not restored['complete'] and restored['copyRestored'] and not restored['historyUpdated'] and (p/'restored.bin').read_bytes()==data);f=p/'another.bin';payload(f);r=isolate(v,f);record('audit-capacity-rejects-new-isolation-before-delete',not r['complete'] and f.read_bytes()==data and len(call(v)['entries'])==1)
    p,v=case('cross-volume');f=p/'source.bin';data=payload(f);r=isolate(v,f);other=Path(tempfile.mkdtemp(prefix='aegis-phase15-',dir=Path.home()/'.codex/tmp'));require(other.anchor.lower()!=p.anchor.lower(),'Cross-volume destination must be on another volume');restored=restore(v,r['id'],other);record('restore-across-local-volumes',restored['complete'] and (other/'restored.bin').read_bytes()==data);(other/'restored.bin').unlink();other.rmdir()
    p,v=case('changed');f=p/'input.bin';data=payload(f);r=call(v,'isolate',('--input',f,'--sha256','0'*64));record('detection-hash-mismatch-preserves-original',not r['complete'] and f.read_bytes()==data and not call(v)['entries'])
    p,v=case('same-hash');f=p/'one.bin';g=p/'two.bin';data=payload(f);payload(g,data);one=isolate(v,f);two=isolate(v,g);record('same-content-independent-identities',one['complete'] and two['complete'] and one['id']!=two['id'] and len(call(v)['entries'])==2)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE];kernel.CreateFileW.restype=w.HANDLE;kernel.CloseHandle.argtypes=[w.HANDLE]
    p,v=case('locked');f=p/'locked.bin';data=payload(f);handle=kernel.CreateFileW(str(f),0x40000000,1,None,3,0,None);require(handle!=w.HANDLE(-1).value,'Open test lock')
    try:r=isolate(v,f)
    finally:kernel.CloseHandle(handle)
    record('locked-file-preserved',not r['complete'] and f.read_bytes()==data)
    kernel.SetFileAttributesW.argtypes=[w.LPCWSTR,w.DWORD]
    p,v=case('readonly');f=p/'readonly.bin';data=payload(f);require(kernel.SetFileAttributesW(str(f),1),'Set test read-only');r=isolate(v,f)
    record('delete-denied-retains-original-and-copy',not r['complete'] and f.read_bytes()==data and r.get('recoverableCopy') and len(call(v)['entries'])==1);kernel.SetFileAttributesW(str(f),128)
    p,v=case('hardlink');f=p/'one.bin';data=payload(f);os.link(f,p/'second.bin');r=isolate(v,f);record('hardlinks-rejected',not r['complete'] and f.read_bytes()==data and (p/'second.bin').read_bytes()==data)
    p,v=case('streams');f=p/'streams.bin';data=payload(f);Path(str(f)+':extra').write_bytes(b'Inert alternate stream');r=isolate(v,f);record('alternate-streams-rejected-without-loss',not r['complete'] and f.read_bytes()==data and Path(str(f)+':extra').read_bytes()==b'Inert alternate stream')
    p,v=case('quota');f=p/'too-large.bin'
    with f.open('wb') as stream:stream.truncate((64<<20)+1)
    r=isolate(v,f);record('file-size-limit-preserves-original',not r['complete'] and f.stat().st_size==(64<<20)+1)
    p,v=case('full');f=p/'input.bin';data=payload(f,b'Inert capacity test '*20000);r=call(v,'isolate',('--input',f,'--max-pages','1'),test=True);record('sqlite-full-rolls-back-before-delete',not r['complete'] and f.read_bytes()==data and not call(v)['entries'])
    p,v=case('names');call(v);r=call(v,'erase',('--id','../vault.key'));record('entry-traversal-rejected',not r['complete'] and (v/'vault.key').exists())
    f=p/'input.bin';data=payload(f);r=isolate(v,f);id=r['id']
    for name in ('../escape.bin','..\\escape.bin','NUL.txt','stream:ads','trailing.','trailing '):
        r=restore(v,id,p,name);record('restore-name-'+str(len(rows)),not r['complete'] and not (out/'escape.bin').exists(),inputName=name)
    r=restore(v,id,v,'forbidden.bin');record('restore-into-vault-rejected',not r['complete'] and not (v/'forbidden.bin').exists())
    p,v=case('junction');target=p/'target';target.mkdir();link=p/'link';r=subprocess.run(['cmd','/d','/c','mklink','/J',str(link),str(target)],capture_output=True);require(r.returncode==0,'Create isolated test junction');f=target/'original.bin';data=payload(f);r=isolate(v,link/'original.bin');record('source-junction-rejected',not r['complete'] and f.read_bytes()==data)
    # Crash checkpoints terminate the test process, exercising SQLite recovery and handles.
    for point in ('isolate-read','isolate-prepared','isolate-verified','isolate-removed','isolate-final'):
        p,v=case('crash-'+point);f=p/'original.bin';data=payload(f);call(v,'isolate',('--input',f,'--fault',point,'--crash','1'),test=True,crash=True);items=call(v)['entries'];ok=f.exists() and f.read_bytes()==data
        if items:
            r=restore(v,items[0]['id'],p);ok=ok or (r['complete'] and (p/'restored.bin').read_bytes()==data)
        record('process-crash-'+point,ok and (f.exists() or bool(items)))
    for point in ('restore-intent','restore-flushed','restore-published','restore-final'):
        p,v=case('crash-'+point);f=p/'original.bin';data=payload(f);r=isolate(v,f);call(v,'restore',('--id',r['id'],'--directory',p,'--filename','destination.bin','--fault',point,'--crash','1'),test=True,crash=True)
        if point=='restore-flushed':
            temporary=list(p.glob('.aegis-*.tmp'));require(len(temporary)==1,'Crash did not leave expected private temporary file');h=kernel.CreateFileW(str(temporary[0]),0x20000000,7,None,3,0,None);denied=h==w.HANDLE(-1).value
            if not denied:kernel.CloseHandle(h)
            record('unpublished-restore-denies-execution',denied)
        recovered=restore(v,r['id'],p,'recovered.bin');published=p/'destination.bin';record('process-crash-'+point,recovered['complete'] and (p/'recovered.bin').read_bytes()==data and (not published.exists() or published.read_bytes()==data))
    for point in ('erase-uncommitted','erase-final'):
        p,v=case('crash-'+point);f=p/'original.bin';payload(f);r=isolate(v,f);call(v,'erase',('--id',r['id'],'--fault',point,'--crash','1'),test=True,crash=True);items=call(v)['entries'];record('process-crash-'+point,bool(items)==(point=='erase-uncommitted'))
    def corrupt_case(name,column):
        p,v=case(name);f=p/'source.bin';data=payload(f);r=isolate(v,f);db=sqlite3.connect(v/'vault.sqlite');blob=db.execute('SELECT '+column+' FROM entries WHERE id=?',(r['id'],)).fetchone()[0];damaged=bytearray(blob);damaged[-1]^=1;db.execute('UPDATE entries SET '+column+'=? WHERE id=?',(bytes(damaged),r['id']));db.commit();db.close();return p,v,r['id']
    for column in ('meta','data'):
        p,v,id=corrupt_case('tamper-'+column,column);r=restore(v,id,p);record('authenticated-'+column+'-tamper-rejected',not r['complete'] and not (p/'restored.bin').exists())
    for damage in ('missing','empty','cleared'):
        p,v=case('database-'+damage);f=p/'source.bin';payload(f);isolate(v,f);key=(v/'vault.key').read_bytes()
        if damage=='missing':(v/'vault.sqlite').unlink()
        elif damage=='empty':(v/'vault.sqlite').write_bytes(b'')
        else:
            db=sqlite3.connect(v/'vault.sqlite');db.executescript('DELETE FROM entries;DELETE FROM audit;DELETE FROM info;');db.commit();db.close()
        r=call(v);record('database-'+damage+'-never-reinitialized',not r['complete'] and (v/'vault.key').read_bytes()==key)
    p,v=case('audit-tail');f=p/'source.bin';payload(f);isolate(v,f);db=sqlite3.connect(v/'vault.sqlite');db.execute('DELETE FROM audit WHERE seq=(SELECT max(seq) FROM audit)');db.commit();db.close();r=call(v);record('audit-tail-loss-detected',not r['complete'])
    p,v=case('missing-entry');f=p/'source.bin';payload(f);isolate(v,f);db=sqlite3.connect(v/'vault.sqlite');db.execute('DELETE FROM entries');db.commit();db.close();r=call(v);record('missing-entry-detected-against-audit',not r['complete'])
    p,v=case('key-loss');f=p/'source.bin';payload(f);isolate(v,f);before=(v/'vault.sqlite').read_bytes();(v/'vault.key').unlink();r=call(v);record('lost-key-never-reinitialized',not r['complete'] and not (v/'vault.key').exists() and (v/'vault.sqlite').read_bytes()==before)
    p,v=case('legacy');p.joinpath('old.quarantine').write_bytes(b'Legacy inert data');p.joinpath('old.meta.txt').write_text('untrusted metadata');call(v);record('legacy-files-not-consumed-or-deleted',(p/'old.quarantine').read_bytes()==b'Legacy inert data' and (p/'old.meta.txt').read_text()=='untrusted metadata')
    r=call(v,'list',('--fault','isolate-read'));record('production-no-fault-interface',not r['complete'] and 'Unknown' in r['error'])
    stable=inputs==frozen.inputs() and artifacts==binaries(build,a.config);record('stable-sources-and-binaries',stable)
    proof={'schema':1,'phase':'15','configuration':a.config,'status':'PASS','tests':len(rows),'passed':sum(r['passed'] for r in rows),'stableSourcesAndBinaries':stable,'inputs':inputs,'binaries':artifacts,'earlierPhasesRevalidated':False,'malwareExecuted':False,'realPowerCutTested':False,'scope':'quarantine only; fresh inert files, actual NTFS operations and terminated fault-driver processes'}
    (out/'validation.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:v for k,v in proof.items() if k not in ('inputs','binaries')}));return 0
if __name__=='__main__':raise SystemExit(main())
