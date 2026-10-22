"""Audit the actual MSVC x64 COFF object without executing guest code."""
import argparse
import pathlib
import re
import struct
import sys


def audit(data):
    def bounded(offset, size):
        if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
            raise ValueError('truncated COFF range')
    bounded(0, 20)
    machine, count, _, symbols, symbol_count, optional, _ = struct.unpack_from('<HHIIIHH', data)
    if machine != 0x8664 or optional or not count or count > 4096:
        raise ValueError('expected a normal x64 COFF object')
    bounded(20, count * 40)
    for index in range(count):
        section = 20 + index * 40
        name = data[section:section + 8].rstrip(b'\0')
        size, offset = struct.unpack_from('<II', data, section + 16)
        if size:
            bounded(offset, size)
        if name == b'.drectve' and re.search(rb'[/\-](?:defaultlib|include|alternatename):', data[offset:offset + size], re.I):
            raise ValueError('guest object requests a default library or forced external symbol')
    if not symbols or not symbol_count or symbol_count > 100000:
        raise ValueError('missing or unreasonable COFF symbol table')
    bounded(symbols, symbol_count * 18 + 4)
    strings = symbols + symbol_count * 18
    string_bytes, = struct.unpack_from('<I', data, strings)
    if string_bytes < 4:
        raise ValueError('invalid COFF string table')
    bounded(strings, string_bytes)
    undefined = []
    index = 0
    while index < symbol_count:
        symbol = symbols + index * 18
        name = data[symbol:symbol + 8]
        if name[:4] == b'\0' * 4:
            relative, = struct.unpack_from('<I', name, 4)
            if relative < 4 or relative >= string_bytes:
                raise ValueError('invalid symbol string offset')
            end = data.find(b'\0', strings + relative, strings + string_bytes)
            if end < 0:
                raise ValueError('unterminated symbol name')
            name = data[strings + relative:end]
        section, = struct.unpack_from('<h', data, symbol + 12)
        storage, auxiliary = data[symbol + 16:symbol + 18]
        if index + auxiliary >= symbol_count:
            raise ValueError('invalid auxiliary symbol count')
        if storage in (2, 105) and section == 0:
            undefined.append(name.rstrip(b'\0').decode('ascii', errors='replace'))
        index += 1 + auxiliary
    if undefined:
        raise ValueError('unresolved external symbols: ' + ', '.join(undefined))
    return dict(machine='x64', sections=count, symbols=symbol_count, undefined_externals=0, default_libraries=0)


def self_test():
    def fixture(directives=b'', undefined=False):
        table = 60 + len(directives)
        header = struct.pack('<HHIIIHH', 0x8664, 1, 0, table, 1, 0, 0)
        section = b'.drectve' + struct.pack('<IIIIIIHHI', 0, 0, len(directives), 60, 0, 0, 0, 0, 0)
        symbol = b'probe\0\0\0' + struct.pack('<IhHBB', 0, 0 if undefined else 1, 0, 2, 0)
        return header + section + directives + symbol + struct.pack('<I', 4)
    audit(fixture())
    invalid = [b'', fixture()[:30], fixture(undefined=True), fixture(b'/DEFAULTLIB:MSVCRT'),
               fixture(b'/defaultlib:kernel32'), fixture(b'/INCLUDE:__imp_CreateFileW'),
               fixture(b'/ALTERNATENAME:foo=bar')]
    altered = bytearray(fixture())
    struct.pack_into('<I', altered, 8, 0xffffffff)
    invalid.append(bytes(altered))
    for data in invalid:
        try:
            audit(data)
        except ValueError:
            continue
        raise AssertionError('invalid guest object accepted')
    print('PASS: COFF audit rejects truncation, external dependencies, linker directives and overflowing offsets')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--object', nargs='+', type=pathlib.Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif args.object:
        for path in args.object:
            result = audit(path.read_bytes())
            print(f'PASS: {path.name}: {result}')
    else:
        parser.error('provide --object or --self-test')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, AssertionError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
