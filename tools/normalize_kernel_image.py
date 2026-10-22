"""Canonicalize zero-only MSVC linker slack in project-built kernel images.

The runtime loader stays strict: this is a build step, never an input repair path.
"""
import argparse
import pathlib
import struct

def normalize(data):
    if len(data) < 64 or len(data) > 4 * 1024 * 1024 or data[:2] != b'MZ':
        raise ValueError('invalid kernel file extent/signature')
    pe = struct.unpack_from('<I', data, 60)[0]
    if pe < 64 or pe + 264 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('invalid PE header')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    opt = pe + 24
    if not 1 <= count <= 16 or opt + 240 + count * 40 > len(data):
        raise ValueError('invalid section table')
    if struct.unpack_from('<H', data, opt)[0] != 0x20b or struct.unpack_from('<II', data, opt + 144) != (0, 0):
        raise ValueError('only unsigned PE32+ kernel builds are supported')
    end = struct.unpack_from('<I', data, opt + 60)[0]
    for index in range(count):
        size, start = struct.unpack_from('<II', data, opt + 240 + index * 40 + 16)
        if size:
            if start > len(data) or size > len(data) - start:
                raise ValueError('truncated section')
            end = max(end, start + size)
    if end > len(data) or end % 512 or (len(data) - end) % 512 or len(data) - end > 4096 or any(data[end:]):
        raise ValueError('unexpected or nonzero linker trailer')
    if end == len(data):
        return data
    out = bytearray(data[:end])
    struct.pack_into('<I', out, opt + 64, 0)  # PE checksum is optional for this guest format.
    return bytes(out)

def self_test(image):
    data = normalize(image)
    assert normalize(data) == data
    expected = bytearray(data)
    pe = struct.unpack_from('<I', data, 60)[0]
    struct.pack_into('<I', expected, pe + 24 + 64, 0)
    assert normalize(data + bytes(512)) == bytes(expected)
    for bad in [data + bytes(512) + b'x', data + bytes(4608), data[:63], data + bytes(1)]:
        try:
            normalize(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('unsafe normalization accepted')
    print('Canonical image: idempotence, zero padding and trailer refusals passed')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('image', type=pathlib.Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    raw = args.image.read_bytes()
    if args.self_test:
        self_test(raw)
    else:
        canonical = normalize(raw)
        if canonical != raw:
            args.image.write_bytes(canonical)
        print(f'Kernel build canonicalized: {len(raw)} -> {len(canonical)} bytes')
