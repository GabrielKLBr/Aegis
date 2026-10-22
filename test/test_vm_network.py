"""Phase 10 policies, real native VM workers and durable network history."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import time


def main():
    parser=argparse.ArgumentParser();parser.add_argument('executable',type=pathlib.Path);parser.add_argument('image',type=pathlib.Path);parser.add_argument('output',type=pathlib.Path);args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);root=args.output/('network-catalog-'+str(time.time_ns())+' espaço');exe=args.executable.resolve();image=args.image.resolve();log=[];checks=0;boots=[]
    def check(ok,message):
        nonlocal checks
        checks+=1
        if not ok:raise RuntimeError(message)
    def run(*command,ok=True):
        # Explicit fixture policy; production automatic reserve stays unchanged.
        if command and command[0]=="create": command=(*command,"--host-reserve-mib","1024")
        p=subprocess.run([str(exe),'--root',str(root),*map(str,command)],capture_output=True,text=True,timeout=35)
        log.append(dict(command=list(map(str,command)),returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
        check((p.returncode==0)==ok,'Unexpected command result: '+str(command)+' '+p.stderr)
        return json.loads(p.stdout) if p.returncode==0 and p.stdout.strip() else None
    def status(id):return run('status',id)
    def settled(id):
        deadline=time.monotonic()+40
        while time.monotonic()<deadline:
            state=status(id)
            if not state['resident'] and state['state'] in ('stopped','fault'):
                check(state['state']=='stopped','Guest failed: '+str(state));return
            time.sleep(.03)
        raise RuntimeError('Guest did not finish')
    def report(id):
        data=json.loads((root/id/'last-boot.json').read_text());row=data['boots'][0]
        check(row['success'] and row['networkReportValid'] and row['networkChecks']==63 and row['networkUserCalls']>=9,'Missing real guest network checks')
        target=args.output/'boots'/root.name;target.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/id/'last-boot.json',target/(str(len(boots))+'-'+id+'.json'));boots.append(row);return row
    def history(id):return {str(p.relative_to(root/id)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/id/'network-history').rglob('*') if p.is_file()}
    proof={'schema':10,'status':'running'}
    try:
        run('create','solo',image,'network','--disposable','--ram-mib','16');check(status('solo')['network']=='offline','Network must default to offline');run('start','solo');settled('solo');check(report('solo')['networkDenied']==6,'Offline policy leaked')
        run('network','solo','simulated');run('start','solo','--paused');run('network','solo','offline',ok=False);run('snapshot','solo','start');run('clone','solo','branch');run('resume','branch');settled('branch');check(report('branch')['networkReceived']==4,'Cloned simulated network failed')
        run('resume','solo');settled('solo');report('solo');old=history('solo');run('restore','solo','start');run('resume','solo');settled('solo');report('solo');now=history('solo');check(all(now.get(p)==h for p,h in old.items()),'Restore erased or rewrote a previous traffic audit')
        run('network','solo','offline');run('restore','solo','start',ok=False);check(not status('solo')['resident'],'Failed policy restore retained a worker');run('start','solo');settled('solo');report('solo')
        for id,node,peer in [('alpha',1,2),('beta',2,1)]:
            # Two workers durably capture traffic and contend for the switch.
            # Use the existing public VM budget for this I/O-heavy scenario.
            run('create',id,image,'network','--ram-mib','16','--timeout-ms','30000','--disposable');run('network',id,'lab','--lab','one','--node',node,'--peer',peer,'--port',40000);run('start',id,'--paused')
        run('clone','alpha','collision');run('start','collision','--paused',ok=False);check(status('alpha')['resident'] and status('beta')['resident'],'Duplicate lab identity disturbed existing VMs')
        run('resume','alpha');run('resume','beta');settled('alpha');settled('beta');check(report('alpha')['networkReceived']==4 and report('beta')['networkReceived']==4,'Lab bytes were not delivered')
        run('create','proxy',image,'network','--disposable')
        for address in ['127.0.0.1','10.0.0.1','192.168.1.1','169.254.169.254']:
            run('network','proxy','proxy','--relay',address,'--relay-port',9000,'--allow','allowed.test,4,1.1.1.1,7',ok=False)
        run('network','proxy','proxy','--relay','8.8.8.8','--relay-port',9000,'--allow','blocked.test,6,0000:0000:0000:0000:0000:ffff:7f00:0001,7',ok=False)
        run('network','proxy','proxy','--relay','8.8.8.8','--relay-port',9000,'--allow','allowed.test,4,1.1.1.1,7');check(status('proxy')['network']=='proxy','Explicit public proxy policy not saved')
        # No external relay contacted: network guest only resolves denied names
        # and tries private addresses in this profile.
        run('start','proxy');settled('proxy');check(report('proxy')['networkSent']==2,'Proxy DNS must stay in the local policy service')
        captures=args.output/'captures'/root.name;captures.mkdir(parents=True,exist_ok=True)
        for id in ['solo','branch','alpha','beta','collision','proxy']:
            if (root/id/'network-history').exists():shutil.copytree(root/id/'network-history',captures/id)
        for row in run('list'):run('delete',row['id'])
        check(run('list')==[],'Managed VMs remained after cleanup')
        proof.update(status='passed',checks=checks,boots=len(boots),offline_default=True,policy_snapshot_binding=True,durable_audit=True,lab_two_workers=True,duplicate_identity_refused=True,private_proxy_refused=True)
    except BaseException as error:
        proof.update(status='failed',checks=checks,error=str(error));raise
    finally:
        for vm in root.iterdir() if root.exists() else []:
            if vm.is_dir() and not vm.name.startswith('.'):
                try:
                    current=status(vm.name)
                    if current['resident']:run('shutdown',vm.name)
                except Exception:pass
        (args.output/'network-manager-validation.json').write_text(json.dumps(proof,indent=2)+'\n');(args.output/'network-commands.json').write_text(json.dumps(log,indent=2)+'\n')
    print(str(checks)+' network manager checks, '+str(len(boots))+' real boots PASS')

if __name__=='__main__':main()
