"""Phase 19A acceptance against already installed services; no elevation or setup.

Use an empty output directory and the inert ProgramData sentinels prepared by
the test administrator. This does not change protection policy or system settings.
"""
from pathlib import Path
import argparse, ctypes as c, hashlib, json, socket, struct, subprocess, threading
from ctypes import wintypes as w

MAGIC = 0x53474541
k = c.WinDLL('kernel32', use_last_error=True)
class Overlapped(c.Structure):
    _fields_ = [('Internal', c.c_size_t), ('InternalHigh', c.c_size_t),
                ('Offset', w.DWORD), ('OffsetHigh', w.DWORD), ('hEvent', w.HANDLE)]
k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
k.CreateFileW.restype = w.HANDLE
k.CreateEventW.argtypes = [c.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR]
k.CreateEventW.restype = w.HANDLE
k.ReadFile.argtypes = k.WriteFile.argtypes = [w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.POINTER(Overlapped)]
k.GetOverlappedResult.argtypes = [w.HANDLE, c.POINTER(Overlapped), c.POINTER(w.DWORD), w.BOOL]
k.CancelIoEx.argtypes = [w.HANDLE, c.POINTER(Overlapped)]
k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
k.CloseHandle.argtypes = [w.HANDLE]
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.GetNamedPipeServerProcessId.argtypes = [w.HANDLE, c.POINTER(w.ULONG)]

def transfer(handle, data, write):
    buffer = c.create_string_buffer(data if write else data * b'\0')
    total = len(data) if write else data
    at = 0
    while at < total:
        event = k.CreateEventW(None, True, False, None)
        assert event
        io = Overlapped(); io.hEvent = event
        count = w.DWORD()
        try:
            ok = (k.WriteFile if write else k.ReadFile)(handle, c.byref(buffer, at), total-at, c.byref(count), c.byref(io))
            if not ok:
                assert c.get_last_error() == 997, c.get_last_error()
                if k.WaitForSingleObject(event, 7000) != 0:
                    k.CancelIoEx(handle, c.byref(io)); k.GetOverlappedResult(handle, c.byref(io), c.byref(count), True)
                    raise TimeoutError('Owned IPC test deadline')
                assert k.GetOverlappedResult(handle, c.byref(io), c.byref(count), False), c.get_last_error()
            assert 0 < count.value <= total-at
            at += count.value
        finally:
            k.CloseHandle(event)
    return buffer.raw[:total]

def exchange(kind=1, operation=1, version=1, path='C:\\owned19a.txt', chars=None,
             error=True, expected_pid=None, observer=None):
    handle = k.CreateFileW(r'\\.\pipe\Aegis.Engine.v1.AegisAntivirus', 0x12008b,
                           0, None, 3, 0x40000000 | 0x100000 | 0x20000, None)
    assert handle != c.c_void_p(-1).value, c.get_last_error()
    try:
        if expected_pid is not None:
            pid = w.ULONG()
            assert k.GetNamedPipeServerProcessId(handle, c.byref(pid)) and pid.value == expected_pid
        encoded = path.encode('utf-16-le')
        header = struct.pack('<6I', MAGIC, version, kind, operation,
                             len(encoded)//2 if chars is None else chars, 1000 if error else 60000)
        transfer(handle, header, True)
        # Headers rejected before their path must not leave unsolicited bytes
        # queued while the server is delivering its terminal error.
        if encoded and chars is None and version == 1 and kind == 1:
            transfer(handle, encoded, True)
        values = []
        while True:
            magic, wire_version, frame_type, size = struct.unpack('<4I', transfer(handle, 16, False))
            assert magic == MAGIC and wire_version == 1 and frame_type in (1,2,3) and 0 < size <= 2 << 20
            value = json.loads(transfer(handle, size, False)); values.append(value)
            if observer: observer(value)
            if frame_type != 1:
                assert frame_type == (3 if error else 2), value
                if error: assert isinstance(value['error'], str)
                return values
    finally:
        k.CloseHandle(handle)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package', type=Path, required=True)
    p.add_argument('--guest', type=Path, required=True)
    p.add_argument('--network-guest', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--installed-configuration', choices=['Release','Debug'], required=True,
                   help='Configuration installed and verified by the test administrator')
    a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
    # These are negative authorization tests. Refuse an elevated test runner
    # before issuing any policy or quarantine command.
    adv = c.WinDLL('advapi32', use_last_error=True)
    adv.OpenProcessToken.argtypes = [w.HANDLE,w.DWORD,c.POINTER(w.HANDLE)]
    adv.GetTokenInformation.argtypes = [w.HANDLE,w.DWORD,c.c_void_p,w.DWORD,c.POINTER(w.DWORD)]
    token = w.HANDLE(); elevated = w.DWORD(); returned = w.DWORD()
    assert adv.OpenProcessToken(c.c_void_p(-1),8,c.byref(token))
    try:
        assert adv.GetTokenInformation(token,20,c.byref(elevated),c.sizeof(elevated),c.byref(returned))
        assert not elevated.value, 'Run service permission negatives from an ordinary user context'
    finally: k.CloseHandle(token)
    cli = a.package.resolve()/'engines/guardian-engine-service.exe'
    cases = []
    def call(args, success=True, exit_code=None):
        result = subprocess.run([str(cli), *map(str, args)], capture_output=True, timeout=100,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        values = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        diagnostic = ({k:v for k,v in values[-1].items() if k in
                       ['complete','error','ran','loaded','faulted','console','exitCode','issues','unresolved']}
                      if values else {})
        assert ((result.returncode == 0) == success if exit_code is None else result.returncode == exit_code), (args[0], result.returncode, result.stderr.decode('utf-8', 'replace'), diagnostic)
        return values if success else json.loads(result.stderr)
    def request(name, text):
        file = a.output/(name+'.json'); file.write_text(text, 'utf-8'); return file.resolve()
    before = call(['--status'])[-1]
    assert all(before[key]['running'] for key in ('antivirus', 'sandbox'))
    policy = call(['--protection-status'])[-1]['policy']
    for name, args in [('wrong protocol version', {'version':2}), ('wrong service kind', {'kind':2,'operation':2}),
                       ('internal operation on public pipe', {'operation':25}), ('zero path quota', {'chars':0,'path':''}),
                       ('oversize path quota', {'chars':32768,'path':''}), ('relative path', {'path':'owned.txt'}),
                       ('embedded NUL path', {'path':'C:\\owned\0.txt'})]:
        exchange(**args); cases.append(name)
    for name, text, fragment in [
        ('deep privileged request', '{"schema":1,"n":'+ '['*10000+'0'+']'*10000+'}', 'complexity'),
        ('duplicate privileged fields', '{"schema":1,"after":0,"after":1}', 'Duplicate'),
        ('oversize privileged request', '{"schema":1,"n":"'+'x'*65536+'"}', 'limit'),
    ]:
        error = call(['--protection-events', request(name, text)], False)
        assert fragment.lower() in error['error'].lower(), error
        cases.append(name)
    denied = call(['--protection-events', str(a.output.resolve()/'request.json')+':stream'], False)
    assert 'path' in denied['error'].lower(); cases.append('privileged ADS path rejected')
    for name, command, value in [
        ('ordinary user cannot change protection policy','--protection-config',{'schema':1}),
        ('ordinary user cannot restore quarantined content','--q-restore',{'schema':2,'id':'0'*32}),
        ('ordinary user cannot erase quarantined content','--q-erase',{'schema':2,'id':'0'*32}),
    ]:
        denied = call([command,request(name,json.dumps(value))],False)
        assert 'Administrator authorization required' in denied['error'], denied
        cases.append(name)
    sample = a.output/'sample.bin'; sample.write_bytes(b'AEGIS_SCANNER_TEST_POSITIVE_V1')
    values = call(['--scan', sample.resolve()]); identity = next(v for v in values if v.get('type')=='worker')
    assert not identity['elevated'] and not identity['administrator'] and identity['privilegesRestricted']
    assert identity['imageLoadRestricted'] and identity['extensionPointsDisabled']
    assert values[-1]['complete'] and values[-1]['threatsFound'] == 1 and values[-1]['filesScanned'] == 1
    cases.append('scanner coordinator and child retain harmless positive detection')
    hash_file = a.output/'hash-source.bin'
    with hash_file.open('wb') as stream: stream.seek((32 << 20)-1); stream.write(b'\0')
    lookup = request('lookup', json.dumps({'schema':1,'source':str(hash_file.resolve()),'online':False,'providers':['circl']}))
    def memory_access(value):
        if value.get('type') != 'worker': return
        handle = k.OpenProcess(0x20 | 0x40, False, value['pid'])
        code = c.get_last_error()
        if handle: k.CloseHandle(handle)
        assert not handle and code == 5, 'Worker process must deny injection/handle duplication'
    values = exchange(operation=16, path=str(lookup), error=False,
                      expected_pid=before['antivirus']['pid'], observer=memory_access)
    identity = next(v for v in values if v.get('type')=='worker')
    assert identity['childProcessCreationBlocked'] and identity['desktopRestricted'] and identity['imageLoadRestricted']
    assert values[-1]['complete'] and not values[-1]['online']; cases.append('leaf policy enforced without network lookup')
    cases.append('live worker process denies injection and handle duplication')
    protected = Path(r'C:\ProgramData\AegisIsolation19A\protected-file.txt')
    assert protected.exists()
    try: protected.read_bytes()
    except PermissionError: pass
    else: raise AssertionError('Protected sentinel readable by test user')
    values = call(['--scan', protected], exit_code=3); assert values[-1]['inconclusive'] and not values[-1]['complete'] and not values[-1]['threatsFound']
    cases.append('service cannot read administrator-only sentinel through user worker')
    sentinel = Path(r'C:\ProgramData\AegisIsolation19A\host-sentinel.txt')
    host_digest = hashlib.sha256(sentinel.read_bytes()).hexdigest()
    listener = socket.socket(); listener.bind(('127.0.0.1',46191)); listener.listen(2); listener.settimeout(.1)
    stop = threading.Event(); accepted = []
    def receive():
        while not stop.is_set():
            try:
                connection,_ = listener.accept(); accepted.append(True); connection.close()
            except socket.timeout: pass
    thread = threading.Thread(target=receive); thread.start()
    try:
        values = call(['--sandbox', a.guest.resolve()]); report = values[-1]
        assert report['complete'] and report['ran'] and 'ISOLATION_GUEST_FILES_PASS' in report['console'], report
        assert not report['inputExecutedOnHost']
        assert hashlib.sha256(sentinel.read_bytes()).hexdigest() == host_digest
        unavailable = call(['--sandbox', a.network_guest.resolve()], exit_code=3)[-1]
        assert not unavailable['complete'] and not unavailable['ran'] and not unavailable['inputExecutedOnHost']
        assert unavailable['unresolved'] and all(name.startswith('ws2_32.dll!') for name in unavailable['unresolved'])
        assert not accepted
    finally:
        stop.set(); thread.join(2); listener.close()
    cases.extend(['guest cannot read host sentinel', 'guest write remains in virtual namespace',
                  'unavailable Winsock guest refused without host execution or listener connection'])
    after = call(['--status'])[-1]
    assert after == before and call(['--protection-status'])[-1]['policy'] == policy
    cases.append('service identities and protection policy unchanged during requests')
    proof = {'phase':'19A','status':'PASS','cases':cases,'tests':len(cases),
             'clientConfiguration':json.loads((a.package/'aegis.manifest.json').read_bytes())['configuration'],
             'installedServiceConfiguration':a.installed_configuration,
             'networkGuestOutcome':'unavailable Winsock imports refused before execution; not an end-to-end socket compatibility pass',
             'realMalwareExecuted':False,'hostListenerConnections':len(accepted)}
    (a.output/'results.json').write_text(json.dumps(proof, indent=2)+'\n','utf-8'); print(json.dumps(proof))
if __name__ == '__main__': main()
