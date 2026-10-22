"""Capture native Windows observations and compare strictly versioned matrices."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PROGRAM_INPUTS = (
    'shared/guardian/Conformance.h', 'shared/guardian/ConformanceProgram.h',
    'guest/fixtures/PeConformance.cpp', 'guest/fixtures/PeExceptionProbe.asm',
    'guest/fixtures/PeRuntimeApi.h', 'shared/guardian/Memory.h',
    'shared/guardian/Exception.h',
)
TARGETS = {'win7': '6.1.7601', 'win10': '10.0.19045', 'win11': '10.0.22631'}


def require(value, reason):
    if not value:
        raise ValueError(reason)


def program_id():
    digest = hashlib.sha256()
    for name in PROGRAM_INPUTS:
        digest.update(name.encode('utf-8') + b'\0' + (ROOT / name).read_bytes() + b'\0')
    return digest.hexdigest()


def catalog():
    text = (ROOT / 'shared/guardian/Conformance.h').read_text(encoding='utf-8')
    rows = re.findall(r'\{"([a-z0-9.]+)",\s*"([a-z-]+)",\s*(\d+)\}', text)
    require(len(rows) >= 50 and len({row[0] for row in rows}) == len(rows), 'Invalid case catalog')
    return [{'id': i, 'case': name, 'domain': domain, 'expected': int(expected)}
            for i, (name, domain, expected) in enumerate(rows)]


def observations(rows):
    expected = catalog()
    require(isinstance(rows, list) and len(rows) == len(expected), 'Incomplete observations')
    normalized = []
    for row, case in zip(rows, expected):
        require(isinstance(row, dict) and type(row.get('id')) is int and row['id'] == case['id'],
                'Duplicate, unordered or unknown case')
        require(type(row.get('actual')) is int and 0 <= row['actual'] <= 2**64 - 1, 'Invalid observed value')
        for key in ('case', 'domain', 'expected'):
            if key in row:
                require(type(row[key]) is type(case[key]), 'Invalid case field type')
                require(row[key] == case[key], 'Case identity/expectation mismatch')
        normalized.append(dict(case, actual=row['actual']))
    return normalized


def parse_reference(output):
    metadata, rows, done = None, [], None
    for line in output.splitlines():
        if not line:
            continue
        if line.startswith('12C_META '):
            require(metadata is None, 'Duplicate reference metadata')
            metadata = json.loads(line[9:])
        elif re.fullmatch(r'12C \d+ \d+', line):
            _, identifier, actual = line.split()
            rows.append({'id': int(identifier), 'actual': int(actual)})
        elif re.fullmatch(r'12C_DONE \d+', line):
            require(done is None, 'Duplicate completion record')
            done = int(line.split()[1])
        else:
            raise ValueError('Unexpected reference output')
    require(isinstance(metadata, dict) and metadata.get('schema') == 1 and
            metadata.get('backend') == 'native-windows' and metadata.get('architecture') == 'x64' and
            re.fullmatch(r'\d+\.\d+\.\d+', metadata.get('nt', '')) and
            metadata.get('pageBytes') == 4096 and metadata.get('programHash') == program_id(),
            'Reference identity/version/program mismatch')
    rows = observations(rows)
    require(done == 0 and all(row['actual'] == row['expected'] for row in rows),
            'Native Windows contract differs; preserve and review raw observations')
    return metadata, rows


def capture_reference(executable, output):
    require(sys.platform == 'win32' and platform.machine().lower() in ('amd64', 'x86_64'),
            'Reference capture requires native Windows x64; Linux is not a Windows reference')
    import ctypes
    require(not hasattr(ctypes.WinDLL('ntdll'), 'wine_get_version'), 'Wine is not a native Windows reference')
    require(executable.read_bytes()[:2] == b'MZ', 'Reference executable must be a native Windows PE')
    output.mkdir(parents=True, exist_ok=True)
    stamp = str(time.time_ns())
    process = subprocess.run([str(executable.resolve()), str(output / ('files-' + stamp))],
                             capture_output=True, text=True, timeout=45)
    (output / ('raw-' + stamp + '.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
    require(process.returncode == 0, 'Native reference failed; see preserved raw log')
    metadata, rows = parse_reference(process.stdout)
    proof = {'schema': 1, 'kind': 'native-reference', 'programHash': program_id(),
             'native': True, 'wine': False, 'host': platform.platform(),
             'executableSha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
             'metadata': metadata, 'observations': rows, 'status': 'PASS'}
    name = 'reference-' + stamp + '.json'
    (output / name).write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    (output / 'latest.json').write_text(json.dumps({'reference': name}) + '\n', encoding='utf-8')
    print(f'Native Windows {metadata["nt"]}: {len(rows)} reference observations PASS')
    return proof


def compare(matrix, references):
    require(matrix.get('schema') == 1 and matrix.get('status') == 'PASS' and
            matrix.get('architecture') == 'x64' and matrix.get('backend') in ('KVM', 'WHP') and
            matrix.get('programHash') == program_id(), 'Guest matrix identity/status mismatch')
    require(matrix.get('caseCount') == len(catalog()) and matrix.get('completeRuns') == 15 and
            len(matrix.get('runs', [])) == 15, 'Guest boot matrix incomplete')
    runs = []
    coverage = {nt: [] for nt in TARGETS}
    for run in matrix['runs']:
        require(run.get('profile') in TARGETS and run.get('nt') == TARGETS[run['profile']], 'Guest NT profile mismatch')
        require(type(run.get('restored')) is bool and run.get('ramBytes') in (16 << 20, 64 << 20), 'Guest resource profile mismatch')
        rows = observations(run['observations'])
        require(all(row['actual'] == row['expected'] for row in rows), 'Guest contract divergence')
        coverage[run['profile']].append((run['ramBytes'], run['restored']))
        runs.append((run, rows))
    expected = sorted([(16 << 20, False)] * 2 + [(64 << 20, False)] * 2 + [(16 << 20, True)])
    require(all(sorted(rows) == expected for rows in coverage.values()), 'RAM/restore/profile coverage incomplete')
    indexed = {}
    for proof in references:
        require(proof.get('schema') == 1 and proof.get('kind') == 'native-reference' and
                proof.get('status') == 'PASS' and proof.get('native') is True and proof.get('wine') is False and
                proof.get('programHash') == program_id() and isinstance(proof.get('host'), str) and
                proof['host'].startswith('Windows-') and re.fullmatch('[a-f0-9]{64}', proof.get('executableSha256', '')),
                'Reference provenance/status/program mismatch')
        metadata = proof.get('metadata', {})
        require(metadata.get('schema') == 1 and metadata.get('pageBytes') == 4096 and
                metadata.get('architecture') == 'x64' and metadata.get('backend') == 'native-windows' and
                metadata.get('programHash') == program_id(), 'Reference architecture/backend mismatch')
        rows = observations(proof['observations'])
        nt = metadata.get('nt')
        require(nt not in indexed, 'Ambiguous duplicate reference version')
        indexed[nt] = rows
    compared, differences = [], []
    for run, rows in runs:
        reference = indexed.get(run['nt'])
        if reference is None:
            continue
        compared.append(run['profile'])
        for actual, expected_row in zip(rows, reference):
            if actual['actual'] != expected_row['actual']:
                differences.append({'profile': run['profile'], 'case': actual['case'],
                                    'guest': actual['actual'], 'windows': expected_row['actual']})
    pending = [nt for nt in TARGETS.values() if nt not in indexed]
    return {'schema': 1, 'status': 'FAIL' if differences else 'PASS', 'programHash': program_id(),
            'guestRuns': len(runs), 'casesPerRun': len(catalog()), 'guestObservations': len(runs) * len(catalog()),
            'referenceCompared': bool(compared), 'comparedGuestRuns': len(compared),
            'missingNativeVersions': pending, 'differences': differences,
            'phaseComplete': False, 'prerequisite12BComplete': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='mode', required=True)
    commands.add_parser('program-id')
    native = commands.add_parser('reference')
    native.add_argument('executable', type=Path);native.add_argument('output', type=Path)
    differential = commands.add_parser('compare')
    differential.add_argument('matrix', type=Path);differential.add_argument('--reference', type=Path, action='append', default=[])
    differential.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'program-id':
        print(program_id());return 0
    if args.mode == 'reference':
        capture_reference(args.executable, args.output);return 0
    proof = compare(json.loads(args.matrix.read_text(encoding='utf-8')),
                    [json.loads(path.read_text(encoding='utf-8')) for path in args.reference])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(proof));return 0 if proof['status'] == 'PASS' else 1

if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print(str(error), file=sys.stderr);sys.exit(1)
