"""Exercise the managed VM lifecycle against real KVM/WHP guests."""
import argparse
import hashlib
import json
import os
import pathlib
import signal
import shutil
import subprocess
import sys
import time


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('executable',type=pathlib.Path)
    parser.add_argument('image',type=pathlib.Path)
    parser.add_argument('output',type=pathlib.Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    root=args.output/('catalog-'+str(time.time_ns())+' com espaço');root.mkdir()
    exe=args.executable.resolve();image=args.image.resolve();log=[];checks=0;completed=[]
    def check(ok,message):
        nonlocal checks
        checks+=1
        if not ok:raise RuntimeError(message)
    def run(*command,ok=True):
        # Explicit fixture policy; production automatic reserve stays unchanged.
        if command and command[0]=="create": command=(*command,"--host-reserve-mib","1024")
        p=subprocess.run([str(exe),'--root',str(root),*map(str,command)],capture_output=True,text=True,timeout=35)
        log.append({'command':list(map(str,command)),'code':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
        check((p.returncode==0)==ok,'Unexpected VM command outcome: '+str(command)+' '+p.stderr)
        return json.loads(p.stdout) if ok else None
    def state(id):return run('status',id)
    def settled(id):
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            value=state(id)
            if value['state'] in ('stopped','fault'):
                check(value['state']=='stopped','Guest failed: '+value['error']);return value
            time.sleep(.05)
        raise RuntimeError('Guest did not settle: '+id)
    def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    def report(id):
        source=root/id/'last-boot.json';value=json.loads(source.read_text())['boots'][0]
        folder=args.output/'boots';folder.mkdir(exist_ok=True);target=folder/(str(len(completed)+1)+'-'+id+'.json');shutil.copy2(source,target)
        completed.append({'id':id,'boot_id':value['bootId'],'report':str(target),'sha256':digest(target)})
        check(value['success'] and not value['timedOut'] and not value['processes']['liveObjects'] and not value['processes']['liveHandles'] and not value['processes']['liveReferences'],'Guest completion/resource proof missing')
        check('PE dynamic DLL TLS references rollback unload PASS' in value['console'],'Owned application proof missing')
        return value
    proof={'status':'running','phase':9,'root':str(root),'native_windows':os.name=='nt','commands':log,'completed_boots':completed}
    try:
        base=args.output/'imported-base.gvd';base.write_bytes(bytes(65536));base_before=digest(base)
        foreign=args.output/'foreign-sentinel.txt';foreign.write_text('Unrelated user file: preserve byte-for-byte.\n');foreign_before=digest(foreign)
        for bad in ('../outside','con','a/b','UPPER'):
            run('create',bad,image,'storage',ok=False)
        run('create','bad-ram',image,'storage','--ram-mib','1025',ok=False)
        run('create','bad-timeout',image,'storage','--timeout-ms','0',ok=False)
        invalid=args.output/'invalid.gk';invalid.write_bytes(b'bad image')
        run('create','invalid',invalid,'storage',ok=False)
        run('create','alpha',image,'storage','--ram-mib','16','--base',base)
        run('start','alpha','--paused');run('snapshot','alpha','initial')
        run('resume','alpha');paused=run('pause','alpha')
        check(paused['state']=='paused' and paused['virtualNs']>0,'Real pause did not preserve running state')
        time.sleep(.04);check(state('alpha')['virtualNs']==paused['virtualNs'],'Virtual clock advanced while paused')
        run('snapshot','alpha','live');snapshot_hash=digest(root/'alpha/snapshots/live.gvs')
        clone=run('clone','alpha','beta');check(clone['id']=='beta' and clone['state']=='paused' and clone['bootId']==paused['bootId'],'Clone identity or logical checkpoint lost')
        if os.name!='nt':check((root/'alpha/base.gvd').stat().st_ino!=(root/'beta/base.gvd').stat().st_ino,'Clone shares writable file identity')
        # The clone has no resident worker: restore it before resuming the
        # already-resident alpha. Otherwise alpha can legitimately finish while
        # beta allocates/restores its VM. Bracket alpha's running acknowledgement
        # with beta running in the same generation, proving actual overlap
        # without requiring both finite programs to survive two later CLI polls.
        second=run('resume','beta');first=run('resume','alpha');overlap=state('beta')
        proof['concurrent_evidence']={'beta_before':second,'alpha_during':first,'beta_after':overlap}
        check(first['state']=='running' and second['state']=='running' and overlap['state']=='running' and
              first['resident'] and overlap['resident'] and first['workerPid']!=overlap['workerPid'] and
              second['workerPid']==overlap['workerPid'] and second['generation']==overlap['generation'],
              'Two independent VMs were not running concurrently')
        settled('alpha');settled('beta');a=report('alpha');b=report('beta')
        check(a['storage']['sequenceAfter']==b['storage']['sequenceAfter'],'Restored branches lost volume sequence')
        beta_disk=digest(root/'beta/disk.gvo');beta_report=digest(root/'beta/last-boot.json')
        run('start','alpha');settled('alpha');again=report('alpha')
        check(again['storage']['sequenceBefore']==a['storage']['sequenceAfter'] and again['bootId']!=a['bootId'],'Persistent restart did not recover the previous disk')
        restored=run('restore','alpha','live')
        check(restored['state']=='paused' and restored['virtualNs']==paused['virtualNs'] and digest(root/'alpha/current.gvs')==snapshot_hash,'Restore did not recover the exact checkpoint')
        run('shutdown','alpha');run('configure','alpha','--ram-mib','32','--timeout-ms','10000');run('start','alpha','--paused')
        before=digest(root/'alpha/current.gvs');run('restore','alpha','live',ok=False)
        check(state('alpha')['state']=='paused' and digest(root/'alpha/current.gvs')==before,'Failed restore damaged the existing VM')
        old_boot=state('alpha')['bootId'];run('restart','alpha');settled('alpha');check(report('alpha')['bootId']!=old_boot,'Restart did not initialize a new guest');run('shutdown','alpha');time.sleep(.03)
        check(digest(root/'beta/disk.gvo')==beta_disk and digest(root/'beta/last-boot.json')==beta_report,'Neighbor VM changed during restore/configuration')
        run('create','disposable',image,'storage','--disposable','--ram-mib','16','--base',base)
        for iteration in range(2):
            run('start','disposable');settled('disposable');result=report('disposable')
            check(result['storage']['sequenceBefore']==0 and not (root/'disposable/disk.gvo').exists(),'Disposable VM retained a previous disk')
        run('start','beta','--paused');run('snapshot','beta','before-crash');saved=digest(root/'beta/current.gvs')
        previous_disk_size=(root/'beta/disk.gvo').stat().st_size
        run('resume','beta');deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            work=list((root/'beta').glob('.active-*/disk.gvo'))
            if work and work[0].stat().st_size>previous_disk_size+544:break
            time.sleep(.005)
        live=state('beta');check(live['state']=='running' and live['resident'],'Owned worker exited before crash injection')
        os.kill(live['workerPid'],signal.SIGTERM if os.name=='nt' else signal.SIGKILL)
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            recovered=state('beta')
            if recovered['state']=='paused' and not recovered['resident']:break
            time.sleep(.02)
        check(recovered['state']=='paused' and not recovered['resident'] and digest(root/'beta/current.gvs')==saved,'Worker crash lost the last complete generation')
        check(recovered['recoveredDisk'] and (root/'beta/disk.gvo').stat().st_size>previous_disk_size,'Latest persistent records were lost during crash recovery')
        run('clone','beta','warm-branch');run('resume','warm-branch');settled('warm-branch');report('warm-branch');run('delete','warm-branch')
        run('restart','beta');settled('beta');check(report('beta')['storage']['sequenceBefore']>=b['storage']['sequenceAfter'],'Cold restart lost the recovered persistent volume')
        for index in range(5):run('create','limit-'+str(index),image,'storage','--ram-mib','16')
        for index in range(4):run('start','limit-'+str(index),'--paused')
        run('start','limit-4','--paused',ok=False);run('start','limit-0','--paused',ok=False)
        run('configure','limit-0','--ram-mib','64','--timeout-ms','10000',ok=False);run('delete','limit-0',ok=False)
        for index in range(4):run('shutdown','limit-'+str(index))
        time.sleep(.05)
        names=[row['id'] for row in run('list')]
        check(len(names)==8 and len(set(names))==8,'VM catalog identities collided')
        for id in names:run('delete',id)
        check(run('list')==[] and digest(foreign)==foreign_before and digest(base)==base_before,'Cleanup touched foreign files/base or retained managed VMs')
        proof.update(status='passed',checks=checks,concurrent_vms=2,resource_limit_vms=4,worker_crash_recovery=True,persistent_and_disposable=True,neighbor_files_preserved=True)
    except BaseException as error:
        proof.update(status='failed',error=str(error),checks=checks)
        raise
    finally:
        # Only stop workers of this newly created, explicitly owned test catalog.
        try:
            listed=run('list')
            for value in listed:
                if value.get('resident'):run('shutdown',value['id'])
        except BaseException:pass
        (args.output/'manager-validation.json').write_text(json.dumps(proof,indent=2,ensure_ascii=False)+'\n')
    print(str(checks)+' managed VM lifecycle, concurrent execution, snapshot, crash recovery and isolation checks PASS')

if __name__=='__main__':main()
