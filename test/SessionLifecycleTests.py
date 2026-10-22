"""Owned real guests: duration, worker handoff, scripts, long wait and viewer restore."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

vm, gui, graphics, image, output = map(Path, sys.argv[1:])
vm, gui, graphics, image = [p.resolve() for p in (vm, gui, graphics, image)]
output = output.resolve()
output.mkdir(parents=True, exist_ok=True)
root = output / ('catalog-' + str(time.time_ns()))
commands = []
checks = 0


def check(value, message):
    global checks
    checks += 1
    if not value:
        raise AssertionError(message)


def run(*args, ok=True):
    if args[0] == 'create':
        args = (*args, '--host-reserve-mib', '1024')
    p = subprocess.run([str(vm), '--root', str(root), *map(str, args)],
                       capture_output=True, text=True, timeout=40)
    commands.append({'args': list(map(str, args)), 'exit': p.returncode,
                     'stdout': p.stdout, 'stderr': p.stderr})
    check((p.returncode == 0) == ok, str(args) + ': ' + p.stdout + p.stderr)
    return json.loads(p.stdout) if p.returncode == 0 else None


def settled(name, timeout=20):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        s = run('status', name)
        if not s['resident'] and s['state'] in ('paused', 'stopped', 'fault'):
            check(s['state'] != 'fault', str(s))
            return s
        time.sleep(.06)
    raise AssertionError('Session worker did not settle: ' + name)


def frame(name):
    files = list((root / name / 'graphics-history').glob('*/graphics.json'))
    return max((json.loads(p.read_text()) for p in files), key=lambda x: x['activeNs'])


def boot(name):
    result = json.loads((root / name / 'last-boot.json').read_text())['boots'][0]
    check(result['success'] and not result['timedOut'], str(result))
    return result


def launch_gui(directory, *args, ok=True, timeout=50):
    env = dict(os.environ)
    if sys.platform == 'linux':
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    p = subprocess.run([str(gui), str(graphics), str(directory), *map(str, args)],
                       env=env, capture_output=True, text=True, timeout=timeout)
    check((p.returncode == 0) == ok, p.stdout + p.stderr)
    return p


proof = {'status': 'RUNNING', 'phase': '12D', 'wine': False, 'timeMode': 'real'}
try:
    script = output / 'managed.txt'
    script.write_text('200 key 13\n400 click 40 65 1\n600 click 160 145 1\n800 key 71\n1600 key 75\n2800 close\n')
    run('create', 'session', graphics, 'graphics', '--ram-mib', '32')
    run('session-config', 'session', '--duration-ms', '1100', '--checkpoint-ms', '200', '--script', script)
    for args in [('--duration-ms', '0'), ('--duration-ms', '604800001'),
                 ('--duration-ms', '1000', '--time-mode', 'accelerated'),
                 ('--duration-ms', '1000', '--checkpoint-ms', '30001')]:
        run('session-config', 'session', *args, ok=False)
    run('start', 'session')
    first = settled('session')
    check(first['state'] == 'paused' and first['virtualNs'] >= 1100000000, 'Duration was not a restorable pause')
    f = frame('session')
    check(f['scriptConsumed'] == 4 and f['inputs'] == 4, 'First timed script segment differs')
    saved = hashlib.sha256((root / 'session/current.gvs').read_bytes()).hexdigest()
    time.sleep(.15)
    check(hashlib.sha256((root / 'session/current.gvs').read_bytes()).hexdigest() == saved,
          'Offline time mutated the durable checkpoint')
    run('session-config', 'session', '--duration-ms', '1100', '--checkpoint-ms', '200', '--script', script)
    run('resume', 'session')
    run('suspend', 'session')
    suspended = settled('session')
    check(suspended['state'] == 'paused' and not suspended['resident'], 'Suspend retained a resident worker')
    run('resume', 'session')
    middle = settled('session')
    check(middle['state'] == 'paused', 'Continuation did not exhaust its next duration')
    run('clone', 'session', 'branch')
    for name in ('session', 'branch'):
        run('session-config', name, '--duration-ms', '3000', '--checkpoint-ms', '200')
        run('resume', name)
        check(settled(name)['state'] == 'stopped', 'Restored script did not complete')
        b = boot(name)
        f = frame(name)
        check(f['scriptConsumed'] == 6 and f['inputs'] == 6 and len(f['deliveries']) == 6,
              'Input was replayed or omitted across a worker restart')
        check(all(d['deliveredNs'] >= d['dueNs'] for d in f['deliveries']), 'Script input delivered early')
        if name == 'session':
            baseline = b['graphicsHash']
        else:
            check(b['graphicsHash'] == baseline, 'Cloned application state changed')
    run('create', 'wait', image, 'storage', '--ram-mib', '32')
    run('session-config', 'wait', '--duration-ms', '10000', '--checkpoint-ms', '1000')
    run('start', 'wait')
    check(settled('wait', timeout=60)['state'] == 'stopped', 'Waiting application did not finish')
    before = boot('wait')
    check('12D DONE 2' in before['io']['console'], 'Application did not preserve its sleep state')
    run('restart', 'wait')
    settled('wait', timeout=60)
    after = boot('wait')
    check(after['bootId'] != before['bootId'] and after['storage']['sequenceBefore'] == before['storage']['sequenceAfter'],
          'Cold restart lost persistent application storage')
    # Same 12 guest events as the independent phase 11 pixel oracle, with a 31 s deadline.
    long_script = output / 'long.txt'
    long_script.write_text('100 click 30 65 1\n200 key 13\n300 click 40 65 1\n400 key 65\n'
                           '500 click 160 145 1\n600 key 71\n700 blur\n800 key 88\n'
                           '900 click 310 2 1\n1000 click 160 145 1\n1100 key 75\n31000 close\n')
    directory = output / ('long-' + str(time.time_ns()))
    launch_gui(directory, '--script', long_script, '--duration-ms', '1500', '--checkpoint-ms', '300')
    initial = json.loads((directory / 'session.json').read_text())
    check(initial['paused'] and (directory / 'session.gvs').is_file(), 'Graphical session was not checkpointed')
    old = hashlib.sha256((directory / 'session.gvs').read_bytes()).hexdigest()
    launch_gui(directory, '--script', script, '--resume', '--duration-ms', '100', ok=False)
    check(hashlib.sha256((directory / 'session.gvs').read_bytes()).hexdigest() == old, 'Rejected input policy damaged checkpoint')
    launch_gui(directory, '--script', long_script, '--resume', '--duration-ms', '40000', '--checkpoint-ms', '5000')
    from GraphicsApplicationTests import verify
    pixels = verify(directory)
    f = json.loads((directory / 'graphics.json').read_text())
    completed = json.loads((directory / 'session.json').read_text())
    check(completed['completed'] and not (directory / 'session.gvs').exists(), 'Completed session retained a resumable stale checkpoint')
    check(f['activeNs'] >= 31000000000 and len(f['deliveries']) == 12 and
          all(d['deliveredNs'] >= d['dueNs'] for d in f['deliveries']), 'Long guest deadline was skipped')
    check(initial['activeMs'] + completed['activeMs'] >= 31000, 'Real execution was shorter than the requested wait')
    manual = output / ('manual-' + str(time.time_ns()))
    launch_gui(manual, '--interactive', '--duration-ms', '300', '--checkpoint-ms', '100')
    launch_gui(manual, '--interactive', '--resume', '--duration-ms', '300', '--checkpoint-ms', '100')
    check(json.loads((manual / 'session.json').read_text())['paused'] and
          json.loads((manual / 'graphics.json').read_text())['inputs'] >= 1,
          'Native manual viewer failed to restore and clear guest focus')
    proof.update(status='PASS', checks=checks, longSessionNs=f['activeNs'],
                 maximumScriptLatenessNs=f['maximumLatenessNs'], initial=initial, resumed=completed,
                 independentPixels=pixels, workerHandoff=True, coldRestart=True,
                 manualViewerRestore=True, guestOnlyInputs=True)
    print(str(checks) + ' lifecycle checks; actual 31 s guest session, scripts, independent pixels and native viewer restore PASS')
except BaseException as error:
    proof.update(status='FAIL', checks=checks, error=str(error))
    raise
finally:
    try:
        for s in run('list'):
            if s.get('resident'):
                run('shutdown', s['id'])
    except BaseException:
        pass
    (output / 'lifecycle.json').write_text(json.dumps(proof, indent=2) + '\n')
    (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
