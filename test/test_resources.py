"""Owned kernel only: resource reporting, real larger RAM and lifecycle checkpoints."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time


def main():
    exe, kernel, output = map(pathlib.Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    root = output / ('catalog-' + str(time.time_ns()))
    log = []
    proof = {'status': 'running', 'commands': log}

    def run(*args, ok=True, catalog=None):
        # Explicit fixture policy; production automatic reserve stays unchanged.
        if args and args[0]=="create": args=(*args,"--host-reserve-mib","1024")
        p = subprocess.run([str(exe), '--root', str(catalog or root), *map(str, args)], capture_output=True, text=True, timeout=180)
        log.append({'catalog': str(catalog or root), 'args': list(map(str, args)), 'exit': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr})
        if (p.returncode == 0) != ok:
            raise RuntimeError(str(args) + ': ' + p.stderr)
        return json.loads(p.stdout) if ok else p.stderr

    def check(value, message):
        if not value:
            raise RuntimeError(message)

    try:
        run('create', 'large', kernel, 'basic', '--ram-mib', '32', '--storage-limit-mib', '512')
        host = run('resources', 'large')['host']
        # Another catalog can be inspecting/admitting resources at the same time.
        # Hold only the short admission transaction, never a running VM's lease.
        pool = (pathlib.Path(os.environ['LOCALAPPDATA']) / 'Guardian' / 'microvm-resources-v1'
                if sys.platform == 'win32' else
                pathlib.Path(tempfile.gettempdir()) / ('guardian-resources-' + str(os.getuid())))
        with (pool / 'admission.lock').open('r+b') as guard:
            if sys.platform != 'win32':
                import fcntl
                fcntl.flock(guard.fileno(), fcntl.LOCK_EX)
            pending = subprocess.Popen([str(exe), '--root', str(root), 'resources', 'large'],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                pending.wait(timeout=.3)
                exited_while_locked = True
            except subprocess.TimeoutExpired:
                exited_while_locked = False
        try:
            stdout, stderr = pending.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            pending.kill()
            pending.communicate()
            raise
        log.append({'args': ['resources', 'large'], 'contendedAdmission': True,
                    'exit': pending.returncode, 'stdout': stdout, 'stderr': stderr})
        check(not exited_while_locked and pending.returncode == 0,
              'Concurrent resource inspection failed instead of waiting: ' + stderr)
        check(json.loads(stdout)['ram']['requestedMiB'] == 32, 'Contended inspection changed configuration')
        proof['contendedAdmissionWaited'] = True
        # Leave additional margin for ordinary host activity during this test.
        capacity = (host['availableRamBytes'] - host['memoryReserveBytes'] - (128 << 20)) // (8 << 20)
        ram = min(128, capacity - 16)
        check(ram > 64, 'Host lacks budget to test RAM above the previous 64 MiB limit')
        proof['largerRamMiB'] = ram
        run('resource-config', 'large', '--ram-mib', ram)
        resources = run('resources', 'large')
        check(resources['ram']['requestedMiB'] == resources['ram']['admittedMiB'] == ram, 'Request and admission disagree')
        check(resources['ram']['provisionedMiB'] == 0 and resources['cpu']['provisioned'] == 0, 'Stopped VM claims provisioned resources')
        # Keep the admitted RAM while testing an unsupported CPU configuration;
        # otherwise host memory pressure can reject 128 MiB before the SMP check.
        error = run('resource-config', 'large', '--ram-mib', ram, '--vcpus', '2', ok=False)
        check('SMP requires' in error, 'Unsupported SMP was not diagnosed')
        run('resource-config', 'large', '--ram-mib', '1025', ok=False)
        run('resource-config', 'large', '--host-reserve-mib', '512', ok=False)
        check(run('resources', 'large')['ram']['admittedMiB'] == ram, 'Incomplete resource command changed RAM')
        run('start', 'large', '--paused')
        resources = run('resources', 'large')
        check(resources['ram']['provisionedMiB'] == ram and resources['cpu']['provisioned'] == 1, 'Larger RAM was not provisioned')
        run('snapshot', 'large', 'larger')
        run('resource-config', 'large', '--ram-mib', '32', ok=False)
        run('shutdown', 'large')
        run('restore', 'large', 'larger')
        check(run('resources', 'large')['ram']['provisionedMiB'] == ram, 'Larger RAM checkpoint failed to restore')
        run('resume', 'large')
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            status = run('status', 'large')
            if status['state'] in ('stopped', 'fault'):
                check(status['state'] == 'stopped' and not status['error'], 'Owned kernel did not complete')
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Owned kernel timed out')
        run('resource-config', 'large', '--ram-mib', '16')
        error = run('restore', 'large', 'larger', ok=False)
        check('configured RAM bound' in error and run('status', 'large')['state'] == 'stopped', 'Oversized restore allocated or changed the VM before admission')
        check((root / 'large/snapshots/larger.gvs').exists(), 'Rejected restore erased its checkpoint')
        run('create', 'shared', kernel, 'basic', '--ram-mib', '16')
        run('start', 'shared', '--paused')
        other = output / ('other-catalog-' + str(time.time_ns()))
        run('create', 'other', kernel, 'basic', '--ram-mib', '16', catalog=other)
        before = run('resources', 'shared')['globalReservations']['workers']
        run('start', 'other', '--paused', catalog=other)
        check(run('resources', 'shared')['globalReservations']['workers'] >= before + 1, 'Different catalogs failed to share reservations')
        run('shutdown', 'other', catalog=other)
        run('shutdown', 'shared')
        run('create', 'quota', kernel, 'basic', '--volume-quota-kib', '2', '--overlay-limit-kib', '64')
        disk = run('resources', 'quota')['disk']
        check(disk['allocationQuotaBytes'] == 2048 and disk['overlayLimitBytes'] == 65536, 'Storage limits were not persisted')
        run('storage-config', 'quota', '--volume-quota-kib', '1', '--overlay-limit-kib', '64', ok=False)
        run('storage-config', 'quota', '--volume-quota-kib', '2', '--overlay-limit-kib', '128')
        run('resource-config', 'quota', '--maximum')
        maximum = run('resources', 'quota')['ram']
        check(maximum['maximum'] and 16 <= maximum['admittedMiB'] <= 1024 and maximum['requestedMiB'] >= maximum['admittedMiB'], 'Maximum mode fabricated full-host capacity')
        run('create', 'space', kernel, 'basic', '--ram-mib', '32', '--storage-limit-mib', '64')
        run('start', 'space', '--paused')
        error = run('snapshot', 'space', 'too-large', ok=False)
        check('storage quota exhausted' in error, 'Snapshot bypassed aggregate storage quota')
        check(run('status', 'space')['state'] == 'paused', 'Quota failure destroyed the paused session')
        run('shutdown', 'space')
        run('storage-config', 'space', '--volume-quota-kib', '16', '--overlay-limit-kib', '2048', '--storage-limit-mib', '128')
        run('start', 'space', '--paused')
        run('snapshot', 'space', 'fits')
        disk = run('resources', 'space')['disk']
        check(disk['storageAllocatedBytes'] <= disk['storageLimitBytes'] and disk['storageAvailableBytes'] > 0, 'Snapshot accounting exceeded storage limit')
        run('drop-snapshot', 'space', 'fits')
        check(run('resources', 'space')['disk']['storageAvailableBytes'] > disk['storageAvailableBytes'], 'Dropping a snapshot failed to reclaim quota')
        run('shutdown', 'space')
        proof['status'] = 'PASS'
    finally:
        if 'other' in locals():
            try:
                run('shutdown', 'other', catalog=other)
            except Exception as error:
                log.append({'cleanup': 'other', 'error': str(error)})
        for name in ('large', 'quota', 'space', 'shared'):
            try:
                run('shutdown', name)
            except Exception as error:
                log.append({'cleanup': name, 'error': str(error)})
        (output / 'resources.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Resource admission, real larger RAM boot/checkpoint/restore and persisted quota PASS')


if __name__ == '__main__':
    main()
