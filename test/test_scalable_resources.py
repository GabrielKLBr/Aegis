"""Real resource kernel lifecycle with independent, physically charged disks."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def main():
    exe, kernel, output = map(lambda p: Path(p).resolve(), sys.argv[1:])
    runroot = output / str(time.time_ns())
    catalog = runroot / 'catalog'
    first, second = runroot / 'discos ç', runroot / 'migrated'
    first.mkdir(parents=True)
    second.mkdir()
    unrelated = first / 'personal.txt'
    unrelated.write_text('preserve this user file', encoding='utf-8')
    log = []

    def run(*args, ok=True):
        result = subprocess.run([str(exe), '--root', str(catalog), *map(str, args)],
                                capture_output=True, text=True, encoding='utf-8', timeout=90)
        log.append({'args': list(map(str, args)), 'exit': result.returncode,
                    'stdout': result.stdout, 'stderr': result.stderr})
        if (result.returncode == 0) != ok:
            raise RuntimeError(f'{args}: {result.stdout} {result.stderr}')
        return json.loads(result.stdout) if ok else result.stderr

    def check(value, message):
        if not value:
            raise RuntimeError(message)

    def finished(name):
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            value = run('status', name)
            if value['state'] in ('stopped', 'fault') and not value['resident']:
                check(value['state'] == 'stopped' and not value['error'], str(value))
                return
            time.sleep(.05)
        raise RuntimeError('Resource guest did not finish')

    try:
        run('create', 'source', kernel, 'resources', '--vcpus', 2, '--ram-mib', 32,
            '--disk-folder', first, '--disk-capacity-gib', 2, '--disk-growth-mib', 32,
            '--host-reserve-mib', 1024, '--timeout-ms', 30000, '--storage-limit-mib', 256)
        before = run('resources', 'source')
        folder = Path(before['disk']['directory'])
        check(folder.parent == first and folder != first, 'Did not create an owned child folder')
        check(before['cpu']['admitted'] == 2 and before['cpu']['provisioned'] == 0,
              'Resource admission/provisioning mismatch')
        run('start', 'source', '--paused')
        live = run('resources', 'source')
        check(live['cpu']['provisioned'] == 2, 'Did not provision two real processors')
        check(live['disk']['storageLogicalBytes'] > 2 << 30 and
              live['disk']['storageChargedBytes'] < 64 << 20, 'Sparse logical bytes charged as physical allocation')
        run('snapshot', 'source', 'initial')
        run('disk-folder', 'source', second, ok=False)
        run('resume', 'source')
        finished('source')
        report = json.loads((catalog / 'source/last-boot.json').read_text())['boots'][0]
        check(report['resource']['vcpus'] == 2 and report['resource']['fileBytes'] == 5 << 20,
              'Guest SMP/file workload failed')
        check((folder / 'disk.gvs').is_file(), 'Guest disk did not persist')
        measured=run('resources','source')['disk']
        check(measured['guestSpaceKnown'] and measured['volumeAllocatedBytes']>5<<20 and measured['volumeFreeBytes']==(2<<30)-measured['volumeAllocatedBytes'] and 0<measured['availableBytes']<measured['volumeFreeBytes'],'Guest space/capacity/quota accounting diverged')
        run('disk-config', 'source', '--volume-quota-mib', 2048, '--disk-growth-mib', 4, ok=False)
        run('disk-config', 'source', '--volume-quota-mib', 2048, '--disk-growth-mib', 64)
        run('disk-folder', 'source', second)
        migrated = Path(run('resources', 'source')['disk']['directory'])
        check(migrated.parent == second and not folder.exists(), 'Migration did not commit/clean owned source')
        check(unrelated.read_text() == 'preserve this user file', 'Migration changed unrelated parent content')
        run('start', 'source')
        finished('source')
        run('clone', 'source', 'branch')
        clone = Path(run('resources', 'branch')['disk']['directory'])
        check(clone != migrated and clone.parent == second, 'Clone reused source disk location')
        run('start', 'branch')
        finished('branch')
        run('restore', 'source', 'initial')
        run('resume', 'source')
        finished('source')
        # A killed worker must preserve its owned disk generation and recover.
        run('start', 'source', '--paused')
        status = run('status', 'source')
        if os.name == 'nt':
            subprocess.run(['taskkill', '/PID', str(status['workerPid']), '/F'], check=True, capture_output=True)
        else:
            os.kill(status['workerPid'], signal.SIGKILL)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            status = run('status', 'source')
            if not status['resident']:
                break
            time.sleep(.05)
        check(status['state'] == 'paused' and status['recoveredDisk'], 'Killed worker recovery failed')
        run('resume', 'source')
        finished('source')
        # A full aggregate quota must not block the command that frees quota.
        run('create', 'tight', kernel, 'resources', '--vcpus', 1, '--ram-mib', 16,
            '--disk-growth-mib', 32, '--storage-limit-mib', 40,
            '--host-reserve-mib', 1024, '--timeout-ms', 30000)
        run('start', 'tight', '--paused')
        run('snapshot', 'tight', 'saved')
        run('resume', 'tight')
        deadline=time.monotonic()+40
        while time.monotonic()<deadline:
            failure=run('status','tight')
            if failure['state']=='fault' and not failure['resident']:
                break
            time.sleep(.1)
        else:
            raise RuntimeError('The aggregate quota did not reject the disk replacement')
        time.sleep(.15)
        failure=run('status','tight')
        check(failure['state']=='fault' and 'quota' in failure['error'].lower(),'Quota failure prevented status/recovery control')
        retained=run('resources','tight')['disk']
        check(retained['backingAllocatedBytes']>5<<20,'Full quota discarded the active recoverable disk')
        run('drop-snapshot','tight','saved')
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            repaired=run('status','tight')
            if repaired['recoveredDisk'] and repaired['state']=='paused':break
            time.sleep(.05)
        else:
            raise RuntimeError('Freeing quota did not recover the retained disk')
        run('shutdown','tight')
        run('disk-config','tight','--volume-quota-mib',2048,'--disk-growth-mib',32,'--storage-limit-mib',96)
        run('start','tight')
        finished('tight')
        run('delete','tight')
        run('delete', 'branch')
        run('delete', 'source')
        check(not clone.exists() and not migrated.exists(), 'Deleted VM left a live disk location')
        check(unrelated.read_text() == 'preserve this user file', 'Deletion changed unrelated parent content')
        proof = {'status': 'PASS', 'commands': log}
    except Exception:
        proof = {'status': 'FAIL', 'commands': log}
        raise
    finally:
        for name in ('source', 'branch', 'tight'):
            try:
                run('shutdown', name)
            except Exception:
                pass
        (runroot / 'evidence.json').write_text(json.dumps(proof, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print('Real SMP, scalable volume, independent folder, migration, clone, checkpoint and recovery PASS')


if __name__ == '__main__':
    main()
