"""Phase 15 service boundary and scanner handoff, using only inert owned files."""
from pathlib import Path
import argparse,json,subprocess,winreg
import validate_frameworks as frozen
import validate_quarantine as quarantine

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve();assert not out.exists();out.mkdir(parents=True);exe=build/'Release/guardian-engine-service.exe';rows=[];before=frozen.inputs();binary=frozen.sha(exe)
    def record(name,ok):
        rows.append({'case':name,'passed':bool(ok)});(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows[-1]),flush=True);assert ok,name
    def control(*args):
        r=subprocess.run([str(exe),*map(str,args)],capture_output=True,timeout=30);assert not r.returncode,r.stderr;return json.loads(r.stdout) if r.stdout.strip() else None
    def call(name,action,request=None):
        args=[str(exe),action]
        if request is not None:
            path=out/(name+'-request.json');path.write_text(json.dumps({'schema':2,**request}));args.append(str(path))
        r=subprocess.run(args,capture_output=True,timeout=65);values=[json.loads(line) for line in r.stdout.splitlines() if line.strip()];(out/(name+'.json')).write_text(json.dumps(values,indent=2)+'\n');assert values and 'complete' in values[-1],r.stderr;return values
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,'SYSTEM\\CurrentControlSet\\Services\\AegisAntivirus') as key:image=winreg.QueryValueEx(key,'ImagePath')[0]
    installed=Path(image.split('"')[1]);record('current-protected-service-installed',frozen.sha(installed)==binary and frozen.sha(installed.parent/'guardian-quarantine.exe')==frozen.sha(build/'Release/guardian-quarantine.exe'))
    control('--start','antivirus');baseline=call('baseline','--q-list')[-1];ids={e['id'] for e in baseline['entries']}
    source=out/'inert-positive.txt';source.write_bytes(b'AEGIS_SCANNER_TEST_POSITIVE_V1\nInert phase 15 service fixture.');expected=frozen.sha(source)
    scanned=subprocess.run([str(exe),'--scan',str(source)],capture_output=True,timeout=40)
    scan=[json.loads(line) for line in scanned.stdout.splitlines() if line.strip()];(out/'scanner.json').write_text(json.dumps(scan,indent=2)+'\n');det=next(d for frame in scan for d in frame.get('detections',[]));record('scanner-detection-hash-handoff',det.get('sha256')==expected and scan[-1]['threatsFound']==1)
    result=call('isolate','--q-isolate',{'source':str(source),'sha256':expected,'threat':det['threat'],'reason':det['reason']});identity=result[0];item=result[-1];id=item.get('id')
    record('quarantine-worker-remains-restricted',not identity['elevated'] and identity['restricted'] and identity['privilegesRestricted'] and not identity['administrator'])
    record('service-verified-isolation',item['complete'] and not source.exists() and item.get('originalRemoved') and id not in ids)
    listing=call('listed','--q-list')[-1];record('service-ui-list-contract',any(e['id']==id and e['state']=='isolated' and e['sha256']==expected for e in listing['entries']))
    restored=out/'restored.txt';r=call('restore','--q-restore',{'id':id,'directory':str(out),'filename':restored.name})[-1];record('service-restore-authorized-destination',r['complete'] and frozen.sha(restored)==expected and r.get('vaultCopyRetained'))
    collision=call('collision','--q-restore',{'id':id,'directory':str(out),'filename':restored.name})[-1];record('service-collision-fails-closed',not collision['complete'] and frozen.sha(restored)==expected)
    erased=call('erase','--q-erase',{'id':id})[-1];remaining=call('remaining','--q-list')[-1];record('service-erase-preserves-other-data',erased['complete'] and {e['id'] for e in remaining['entries']}==ids and frozen.sha(restored)==expected)
    audit=call('audit','--q-audit')[-1];events=[e for e in audit['events'] if e['entry']==id];record('service-authenticated-history',audit['complete'] and [e['action'] for e in events]==['prepared','isolated','restore-pending','restored-copy','restore-failed','erased'] and all(e.get('sha256')==expected for e in events))
    legacy=out/'legacy-positive.txt';legacy.write_bytes(b'AEGIS_SCANNER_TEST_POSITIVE_V1\nInert legacy adapter fixture.');legacy_sha=frozen.sha(legacy);cleaned=subprocess.run([str(build/'Release/av.exe'),'clean',legacy.name],cwd=out,capture_output=True,timeout=50);(out/'legacy-clean.log').write_bytes(cleaned.stdout+cleaned.stderr);legacy_rows=[e for e in call('legacy-list','--q-list')[-1]['entries'] if e['source']==str(legacy)];record('legacy-relative-clean-preserves-detection-hash',cleaned.returncode==2 and not legacy.exists() and len(legacy_rows)==1 and legacy_rows[0]['sha256']==legacy_sha);call('legacy-erase','--q-erase',{'id':legacy_rows[0]['id']})
    paged=call('audit-cursor','--q-audit',{'before':events[0]['seq']})[-1];record('service-history-pagination-contract',paged['complete'] and all(e['seq']<events[0]['seq'] for e in paged['events']) and len(paged['events'])<=128)
    control('--stop','antivirus');stopped=subprocess.run([str(exe),'--q-list'],capture_output=True,timeout=15);control('--start','antivirus');record('stopped-engine-no-quarantine-fallback',stopped.returncode!=0 and control('--status')['antivirus']['running'])
    record('stable-service-and-sources',binary==frozen.sha(exe) and before==frozen.inputs())
    proof={'phase':'15','status':'PASS','tests':len(rows),'passed':sum(r['passed'] for r in rows),'sourceStable':True,'serviceSha256':binary,'inputs':frozen.inputs(),'binaries':quarantine.binaries(build,'Release'),'earlierPhasesRevalidated':False,'scope':'quarantine service operations and confirmed scanner detection only'};(out/'validation.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:v for k,v in proof.items() if k not in ('inputs','binaries')}))
if __name__=='__main__':main()
