"""Only the owned reference driver executes, inside the real WHP/KVM guest."""
import hashlib
import json
import pathlib
import subprocess
import sys
import time


def main():
    exe, kernel, fixtures, output = map(pathlib.Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    root = output / ('catalog-' + str(time.time_ns()))
    log = []
    checks = 0
    proof = {'status': 'running', 'commands': log, 'root': str(root)}

    def check(ok, message):
        nonlocal checks
        checks += 1
        if not ok:
            raise RuntimeError(message)

    def run(*args, ok=True):
        # Explicit fixture policy; production automatic reserve stays unchanged.
        if args and args[0]=="create": args=(*args,"--host-reserve-mib","1024")
        p = subprocess.run([str(exe), '--root', str(root), *map(str, args)], capture_output=True, text=True, timeout=40)
        log.append({'args': list(map(str, args)), 'exit': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr})
        check((p.returncode == 0) == ok, 'Unexpected command outcome: ' + str(args) + p.stderr)
        return json.loads(p.stdout) if ok else None

    def settled(name, expected='stopped'):
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            value = run('status', name)
            if value['state'] in ('stopped', 'fault'):
                check(value['state'] == expected, 'Wrong guest result: ' + str(value))
                return value
            time.sleep(.03)
        raise RuntimeError('Guest did not settle')

    def audit(name):
        rows = run('drivers', name)
        check(len(rows) == 1, 'Driver catalog lost a package')
        return rows[0]

    def report(name):
        value = json.loads((root / name / 'last-boot.json').read_text(encoding='utf-8'))['boots'][0]
        (output / (name + '-boot.json')).write_text(json.dumps(value, indent=2), encoding='utf-8')
        return value

    try:
        run('create', 'probe', kernel, 'hardware')
        run('hardware-config', 'probe', '--nt', 'win11', '--cpu', 'Guardian selected CPU label', '--board', 'Selected motherboard')
        run('driver-import', 'probe', 'reference', fixtures / 'guardian-hardware-probe.inf', 'win11', 'board')
        a = audit('probe')
        check(not a['configured'] and not a['audit']['installed'] and not a['audit']['functional'], 'Import claimed execution')
        run('driver-install', 'probe', 'reference')
        a = audit('probe')
        check(a['configured'] and not a['audit']['installed'], 'Staging claimed execution')
        run('start', 'probe', '--paused')
        run('snapshot', 'probe', 'before-entry')
        run('driver-install', 'probe', 'reference', ok=False)
        run('hardware-config', 'probe', '--cpu', 'Changed', ok=False)
        run('clone', 'probe', 'branch')
        run('resume', 'probe')
        settled('probe')
        r = report('probe')
        check(r['success'] and r['driverReportValid'] and r['driverOperations'] == 4 and r['driverUnloaded'], 'Actual driver operation proof absent')
        a = audit('probe')['audit']
        check(a['installed'] and a['initialized'] and a['functional'] and a['unloaded'] and a['observedBoot'] == r['bootId'], 'Manager audit differs from guest')
        check(not audit('branch')['audit']['functional'], 'Clone inherited unexecuted success proof')
        run('resume', 'branch')
        settled('branch')
        check(report('branch')['success'] and audit('branch')['audit']['functional'], 'Clone did not execute staged driver')
        run('restore', 'probe', 'before-entry')
        run('resume', 'probe')
        settled('probe')
        check(report('probe')['success'], 'Named snapshot did not restore driver package')
        # The guest source files are immutable copies. Changing the original
        # import directory cannot change the installed descriptor/checkpoint.
        installed = root / 'probe' / 'installed.gdi'
        before = hashlib.sha256(installed.read_bytes()).hexdigest()
        check(hashlib.sha256((root / 'branch' / 'installed.gdi').read_bytes()).hexdigest() == before, 'Clone changed staged package')
        bad = output / ('bad-package-' + root.name)
        bad.mkdir()
        (bad / 'guardian-hardware-probe.sys').write_bytes((fixtures / 'guardian-hardware-probe-failing.sys').read_bytes())
        (bad / 'probe.inf').write_bytes((fixtures / 'guardian-hardware-probe.inf').read_bytes())
        run('create', 'failed', kernel, 'hardware')
        run('hardware-config', 'failed', '--nt', 'win11')
        run('driver-import', 'failed', 'reference', bad / 'probe.inf', 'win11', 'board')
        run('driver-install', 'failed', 'reference')
        run('start', 'failed')
        settled('failed', 'fault')
        r = report('failed')
        a = audit('failed')['audit']
        check(not r['success'] and a['installed'] and not a['initialized'] and not a['functional'] and a['guestError'] == 0xC0000001, 'Failed entry was masked')
        proof['status'] = 'passed'
    finally:
        for name in ('probe', 'branch', 'failed'):
            if (root / name).exists():
                try:
                    run('shutdown', name)
                except Exception as error:
                    log.append({'cleanup_error': str(error)})
                    proof['status'] = 'failed'
        proof['checks'] = checks
        (output / 'validation.json').write_text(json.dumps(proof, indent=2), encoding='utf-8')
    check(proof['status'] == 'passed', 'Manager test or cleanup failed')
    print(str(checks) + ' driver manager checks PASS')


if __name__ == '__main__':
    main()
