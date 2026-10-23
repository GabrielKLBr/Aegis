"""Harmless phase-14 corpus; never execute a submitted file on the host."""
import io
import json
import pathlib
import subprocess
import struct
import sys
import tarfile
import uuid
import zipfile

exe = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve() / str(uuid.uuid4())
root.mkdir(parents=True)
config = root / 'scanner.json'
settings = {'schema': 1, 'state': str(root / 'state'), 'offline': True}
config.write_text(json.dumps(settings), encoding='utf-8')

def run(path, expected, complete, code=None):
    p = subprocess.run([str(exe), 'scan', str(path), '--config', str(config), '--json'], capture_output=True, text=True, encoding='utf-8', timeout=30)
    j = json.loads(p.stdout)
    r = j['results'][0]
    assert r['classification'] == expected, (path, p.returncode, r, p.stderr)
    assert r['complete'] == complete, r
    if code is not None:
        assert p.returncode == code, (p.returncode, r)
    return r

marker = b'AEGIS_SCANNER_TEST_' + b'POSITIVE_V1'
clean = root / 'documento limpo.txt'
clean.write_text('Documento de teste sem ameacas.\n', encoding='utf-8')
run(clean, 'unknown', True, 0)
positive = root / 'test-marker.txt'
positive.write_bytes(marker)
run(positive, 'detected', True, 1)
late = root / 'late-marker.bin'
late.write_bytes(b'\0' * (5 << 20) + marker)
run(late, 'detected', True, 1)

def zip_bytes(name, data):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr(name, data)
    return buf.getvalue()

archive = root / 'nested.msix'
archive.write_bytes(zip_bytes('inner.zip', zip_bytes('test.txt', marker)))
r = run(archive, 'detected', True, 1)
assert len(r['members']) == 3, r
canary = root / 'private-canary.txt'
canary.write_bytes(b'UNCHANGED')
bad = root / 'traversal.zip'
bad.write_bytes(zip_bytes('../private-canary.txt', marker))
run(bad, 'inconclusive', False, 3)
assert canary.read_bytes() == b'UNCHANGED'
tarpath = root / 'links.tar'
with tarfile.open(tarpath, 'w') as t:
    entry = tarfile.TarInfo('link')
    entry.type = tarfile.SYMTYPE
    entry.linkname = str(canary)
    t.addfile(entry)
run(tarpath, 'inconclusive', False, 3)
broken = root / 'broken.zip'
broken.write_bytes(b'PK\x03\x04bad')
run(broken, 'inconclusive', False, 3)
opaque = root / 'installer.msi'
opaque.write_bytes(b'opaque fixture')
run(opaque, 'inconclusive', False, 3)
settings['limits'] = {'expandedBytes': 1024, 'depth': 1}
config.write_text(json.dumps(settings), encoding='utf-8')
bomb = root / 'bounded.zip'
bomb.write_bytes(zip_bytes('large.txt', b'a' * 100_000))
run(bomb, 'inconclusive', False, 3)
run(archive, 'inconclusive', False, 3)
run(root / 'absent', 'inconclusive', False, 3)
settings.pop('limits')
settings['queueUnknown'] = True
config.write_text(json.dumps(settings), encoding='utf-8')
r = run(clean, 'unknown', True, 0)
assert r['sandbox']['state'] == 'queued'
r2 = run(clean, 'unknown', True, 0)
assert r2['sandbox']['duplicate']
p = subprocess.run([str(exe), 'sandbox-next', '--config', str(config)], capture_output=True, text=True, encoding='utf-8', timeout=30)
assert p.returncode == 3 and not json.loads(p.stdout)['complete'], (p.stdout, p.stderr)

# Owned, CRT-free x64 program: ExitProcess(0), with a synthetic guest import.
# This executable is submitted only to the MiniNT worker, never to the host OS.
pe = bytearray(0x600)
pe[:2] = b'MZ'
struct.pack_into('<I', pe, 0x3c, 0x80)
pe[0x80:0x84] = b'PE\0\0'
struct.pack_into('<HHIIIHH', pe, 0x84, 0x8664, 1, 0, 0, 0, 0xf0, 0x22)
opt = 0x98
struct.pack_into('<H', pe, opt, 0x20b)
struct.pack_into('<I', pe, opt + 4, 0x400)
struct.pack_into('<II', pe, opt + 16, 0x1000, 0x1000)
struct.pack_into('<Q', pe, opt + 24, 0x200000)
struct.pack_into('<II', pe, opt + 32, 0x1000, 0x200)
struct.pack_into('<HH', pe, opt + 40, 6, 0)
struct.pack_into('<II', pe, opt + 56, 0x2000, 0x200)
struct.pack_into('<H', pe, opt + 68, 3)
struct.pack_into('<QQQQ', pe, opt + 72, 0x100000, 0x1000, 0x100000, 0x1000)
struct.pack_into('<I', pe, opt + 108, 16)
struct.pack_into('<II', pe, opt + 112 + 8, 0x1100, 40)
sec = 0x188
pe[sec:sec + 8] = b'.text\0\0\0'
struct.pack_into('<IIII', pe, sec + 8, 0x400, 0x1000, 0x400, 0x200)
struct.pack_into('<I', pe, sec + 36, 0x60000020)
pe[0x200:0x20e] = b'\x48\x83\xec\x28\x31\xc9\xff\x15\x54\x01\x00\x00\xeb\xfe'
struct.pack_into('<IIIII', pe, 0x300, 0x1140, 0, 0, 0x1180, 0x1160)
struct.pack_into('<Q', pe, 0x340, 0x1190)
struct.pack_into('<Q', pe, 0x360, 0x1190)
pe[0x380:0x38d] = b'kernel32.dll\0'
pe[0x390:0x39e] = b'\0\0ExitProcess\0'
owned = root / 'owned-exit.exe'
owned.write_bytes(pe)
run(owned, 'unknown', True, 0)
p = subprocess.run([str(exe), 'sandbox-next', '--config', str(config)], capture_output=True, text=True, encoding='utf-8', timeout=30)
behavior = json.loads(p.stdout)
if sys.platform == 'win32':
    assert p.returncode == 0 and behavior['complete'] and behavior['ran'] and behavior['exitCode'] == 0, (p.stdout, p.stderr)
    assert behavior['backend'] == 'MiniNT-WHP-legacy' and not behavior['unresolved'], behavior
else:
    assert p.returncode == 3 and not behavior['complete'], behavior

# Read the guest's own PEB/command line and compare every UTF-16 code unit,
# including the terminator. The host's absolute spool path must never appear.
virtual_path = 'C:\\Samples\\sample.exe\0'.encode('utf-16le')
privacy_code = bytearray(b'\x48\x83\xec\x28')
privacy_code += b'\x65\x48\x8b\x04\x25\x60\0\0\0'  # GS:PEB
privacy_code += b'\x48\x8b\x40\x20\x48\x8b\x70\x78'  # parameters, CommandLine.Buffer
privacy_code += b'\x48\x8d\x3d' + struct.pack('<i', 0x1200 - (0x1000 + len(privacy_code) + 7))
privacy_code += b'\xb9' + struct.pack('<I', len(virtual_path) // 2)
privacy_code += b'\xfc\xf3\x66\xa7\x0f\x94\xc1\x0f\xb6\xc9\x83\xf1\x01'
privacy_code += b'\xff\x15' + struct.pack('<i', 0x1160 - (0x1000 + len(privacy_code) + 6))
privacy_code += b'\xeb\xfe'
privacy_pe = bytearray(pe)
privacy_pe[0x200:0x200 + len(privacy_code)] = privacy_code
privacy_pe[0x400:0x400 + len(virtual_path)] = virtual_path
privacy = root / 'owned-private-environment.exe'
privacy.write_bytes(privacy_pe)
run(privacy, 'unknown', True, 0)
p = subprocess.run([str(exe), 'sandbox-next', '--config', str(config)], capture_output=True, text=True, encoding='utf-8', timeout=30)
private_result = json.loads(p.stdout)
if sys.platform == 'win32':
    assert p.returncode == 0 and private_result['complete'] and private_result['exitCode'] == 0, (p.stdout, p.stderr)
else:
    assert p.returncode == 3 and not private_result['complete'], private_result

# A valid DIR64 relocation must still adjust an absolute guest pointer.
relocated_pe = bytearray(pe)
struct.pack_into('<Q', relocated_pe, opt + 24, 0x400000)
struct.pack_into('<II', relocated_pe, opt + 112 + 5 * 8, 0x1300, 12)
struct.pack_into('<IIHH', relocated_pe, 0x500, 0x1000, 12, 0xa200, 0)
struct.pack_into('<Q', relocated_pe, 0x400, 0x401208)
code = bytearray(b'\x48\x83\xec\x28')
code += b'\x48\x8d\x05' + struct.pack('<i', 0x1200 - (0x1000 + len(code) + 7))
code += b'\x48\x8b\x10'
code += b'\x48\x8d\x0d' + struct.pack('<i', 0x1208 - (0x1000 + len(code) + 7))
code += b'\x48\x39\xca\x0f\x95\xc1\x0f\xb6\xc9'
code += b'\xff\x15' + struct.pack('<i', 0x1160 - (0x1000 + len(code) + 6))
code += b'\xeb\xfe'
relocated_pe[0x200:0x200 + len(code)] = code
relocated = root / 'owned-valid-relocation.exe'
relocated.write_bytes(relocated_pe)
run(relocated, 'unknown', True, 0)
p = subprocess.run([str(exe), 'sandbox-next', '--config', str(config)], capture_output=True, text=True, encoding='utf-8', timeout=30)
relocation_result = json.loads(p.stdout)
if sys.platform == 'win32':
    assert p.returncode == 0 and relocation_result['complete'] and relocation_result['exitCode'] == 0, relocation_result
else:
    assert p.returncode == 3 and not relocation_result['complete'], relocation_result

# Static PE metadata remains readable, but every unsafe loader RVA must be
# refused before guest execution, with a structured error rather than a crash.
malformed = []
bad = bytearray(pe)
struct.pack_into('<I', bad, 0x300 + 16, 0xfffffff0)
malformed.append(('iat', bad))
bad = bytearray(pe)
struct.pack_into('<I', bad, 0x300, 0xfffffff0)
malformed.append(('thunk', bad))
bad = bytearray(pe)
struct.pack_into('<Q', bad, opt + 24, 0x400000)
struct.pack_into('<II', bad, opt + 112 + 5 * 8, 0xfffffff0, 12)
malformed.append(('relocation-directory', bad))
for kind in ('target', 'block'):
    bad = bytearray(pe)
    struct.pack_into('<Q', bad, opt + 24, 0x400000)
    struct.pack_into('<II', bad, opt + 112 + 5 * 8, 0x1200, 12)
    struct.pack_into('<IIHH', bad, 0x400, 0xfffffff0 if kind == 'target' else 0x1000,
                     12 if kind == 'target' else 0xfffffff0, 0xa204, 0)
    malformed.append(('relocation-' + kind, bad))
for name, data in malformed:
    sample = root / ('owned-malformed-' + name + '.exe')
    sample.write_bytes(data)
    run(sample, 'unknown', True, 0)
    p = subprocess.run([str(exe), 'sandbox-next', '--config', str(config)], capture_output=True, text=True, encoding='utf-8', timeout=30)
    rejected = json.loads(p.stdout)
    assert p.returncode == 3 and not rejected['complete'], rejected
    if sys.platform == 'win32':
        assert rejected['backend'] == 'MiniNT-WHP-legacy' and not rejected['loaded'] and not rejected['ran'] and 'range' in rejected['error'], rejected
# The same owned image with a deliberate infinite guest loop must be bounded.
settings['limits'] = {'milliseconds': 100}
config.write_text(json.dumps(settings), encoding='utf-8')
loop = root / 'owned-loop.exe'
pe[0x200:0x202] = b'\xeb\xfe'
loop.write_bytes(pe)
run(loop, 'unknown', True, 0)
p = subprocess.run([str(exe), 'sandbox-next', '--config', str(config)], capture_output=True, text=True, encoding='utf-8', timeout=10)
assert p.returncode == 3 and not json.loads(p.stdout)['complete'], (p.stdout, p.stderr)
assert canary.read_bytes() == b'UNCHANGED'
print('scanner pipeline PASS')
