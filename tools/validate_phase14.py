#!/usr/bin/env python3
"""Run built scanner/CTests and retain real exit codes, JUnit and source hashes."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def sources(root):
    result = {}
    for folder in ('src', 'guest', 'shared', 'cmake', 'config', 'test', 'tools'):
        for path in (root / folder).rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.cpp', '.c', '.h', '.py', '.cmake', '.json', '.txt'):
                # Generated/compiled fixtures and __pycache__ are not sources.
                if '__pycache__' not in path.parts:
                    result[path.relative_to(root).as_posix()] = digest(path)
    for name in ('CMakeLists.txt', 'signatures.txt'):
        result[name] = digest(root / name)
    return dict(sorted(result.items()))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--config', choices=('Debug', 'Release'), default='Debug')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--all', action='store_true', help='Run the complete registered suite')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    build, output = a.build.resolve(), a.output.resolve()
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    source = next(line.split('=', 1)[1] for line in cache.splitlines()
                  if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL='))
    if Path(source).resolve() != root:
        raise ValueError('Build belongs to a different source directory')
    output.mkdir(parents=True, exist_ok=False)
    before = sources(root)
    started = dt.datetime.now(dt.timezone.utc).isoformat()
    cmd = ['ctest', '--test-dir', str(build), '-C', a.config, '--output-on-failure',
           '--output-junit', str(output / 'ctest.xml'), '-j', '1']
    if not a.all:
        cmd += ['-R', '^scanner_']
    with (output / 'ctest.log').open('w', encoding='utf-8') as log:
        rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=False).returncode
    after = sources(root)
    cases = ET.parse(output / 'ctest.xml').findall('.//testcase')
    failures = [c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None]
    skipped = [c.attrib['name'] for c in cases if c.find('skipped') is not None]
    changed = [name for name in sorted(before.keys() | after.keys()) if before.get(name) != after.get(name)]
    binaries = {}
    binary_root = build / a.config if platform.system() == 'Windows' else build
    for name in ('guardian-scan', 'guardian-scan-worker', 'guardian-whp-memory', 'scanner_core_tests',
                 'scanner_facade_tests', 'av', 'avui', 'guardianav'):
        path = binary_root / (name + ('.exe' if platform.system() == 'Windows' else ''))
        if path.is_file():
            binaries[path.name] = digest(path)
    result = {'schema': 1, 'phase': 14, 'status': 'PASS' if rc == 0 and cases and not failures and not skipped and not changed else 'FAIL',
              'host': platform.platform(), 'startedUtc': started, 'finishedUtc': dt.datetime.now(dt.timezone.utc).isoformat(),
              'sourceRoot': str(root), 'buildRoot': str(build), 'config': a.config,
              'scope': 'all' if a.all else 'scanner', 'exitCode': rc, 'tests': len(cases),
              'failures': failures, 'skipped': skipped, 'changedSources': changed,
              'sources': before, 'binaries': binaries}
    (output / 'validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('status', 'config', 'scope', 'exitCode', 'tests', 'failures', 'skipped', 'changedSources')}, indent=2))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
