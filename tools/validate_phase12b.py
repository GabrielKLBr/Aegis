"""Build and validate one unchanged revision natively on Windows/MSVC or Linux/KVM.

Run from a Visual Studio x64 developer terminal on Windows. On Linux, provide
native LLVM through GUARDIAN_LLVM_ROOT or --llvm. No Wine/QEMU is invoked.
The result is platform evidence; this script never declares phase completion.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def source_inputs():
    files = [ROOT / 'CMakeLists.txt']
    for name in ('src', 'shared', 'guest', 'test', 'cmake', 'tools'):
        files.extend(p for p in (ROOT / name).rglob('*')
                     if p.is_file() and '__pycache__' not in p.parts)
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(files)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--config', action='append', choices=('Debug', 'Release', 'Sanitize'))
    parser.add_argument('--cmake', default=shutil.which('cmake'))
    parser.add_argument('--llvm', type=Path, default=os.environ.get('GUARDIAN_LLVM_ROOT'))
    parser.add_argument('--openssl-root', type=Path)
    parser.add_argument('--legacy-storage', type=Path)
    parser.add_argument('--native-fixtures', type=Path,
                        help='Owned parser fixtures generated from sources; Linux only')
    parser.add_argument('--generator')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    native_windows = sys.platform == 'win32'
    if not native_windows and sys.platform != 'linux':
        parser.error('Native Windows x64 or Linux x64 required')
    if not args.cmake or not 1 <= args.jobs <= 8:
        parser.error('CMake and 1..8 build jobs are required')
    configs = args.config or (['Debug', 'Release'] if native_windows else ['Debug', 'Release', 'Sanitize'])
    if native_windows and 'Sanitize' in configs:
        parser.error('The ASan/UBSan core profile is a Linux validation')
    if not native_windows and not args.llvm:
        parser.error('Set GUARDIAN_LLVM_ROOT or --llvm to the native Linux LLVM installation')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        parser.error('Use a fresh empty output directory to preserve previous evidence')
    cmake = Path(args.cmake).resolve()
    ctest = cmake.with_name('ctest.exe' if native_windows else 'ctest')
    expected = source_inputs()
    (output / 'source-inputs.json').write_text(json.dumps(expected, indent=2) + '\n', encoding='utf-8')
    proof = {'status': 'running', 'phaseComplete': False, 'host': platform.platform(),
             'backend': 'WHP' if native_windows else 'KVM', 'wine': False, 'qemu': False,
             'sourceFiles': len(expected), 'steps': [], 'configurations': {}}
    environment = dict(os.environ)
    if native_windows:
        environment['MSBUILDDISABLENODEREUSE'] = '1'

    def save():
        (output / 'result.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')

    def stable():
        if source_inputs() != expected:
            raise RuntimeError('Sources changed during validation; results do not accept the final revision')

    def step(name, command):
        stable()
        command = list(map(str, command))
        print('Starting ' + name, flush=True)
        started = time.monotonic()
        with (output / (name + '.log')).open('w', encoding='utf-8') as log:
            process = subprocess.run(command, cwd=ROOT, env=environment, stdout=log,
                                     stderr=subprocess.STDOUT)
        result = {'name': name, 'command': command, 'exit': process.returncode,
                  'seconds': round(time.monotonic() - started, 3)}
        proof['steps'].append(result)
        save()
        print(json.dumps(result), flush=True)
        stable()
        if process.returncode:
            raise RuntimeError(name + ' failed; see its preserved log')

    built_guests = {}
    try:
        for config in configs:
            sanitize = config == 'Sanitize'
            guest_config = 'Debug' if sanitize else config
            host = output / ('host-' + config)
            configure = [cmake, '-S', ROOT, '-B', host, '-DBUILD_TESTING=ON']
            if native_windows:
                configure += ['-A', 'x64', '-DGUARDIAN_BUILD_GUEST_CONTRACT=ON']
                if args.generator:
                    configure += ['-G', args.generator]
            else:
                guest = output / ('guest-' + guest_config)
                if guest_config not in built_guests:
                    step('guest-' + guest_config,
                         [sys.executable, ROOT / 'tools/build_guest_linux.py', '--llvm', args.llvm,
                          '--config', guest_config, '--output', guest])
                    built_guests[guest_config] = guest
                configure += ['-DCMAKE_BUILD_TYPE=' + ('Debug' if sanitize else config),
                              '-DGUARDIAN_TEST_KVM=' + ('OFF' if sanitize else 'ON'),
                              '-DGUARDIAN_ENABLE_SANITIZERS=' + ('ON' if sanitize else 'OFF'),
                              '-DGUARDIAN_KVM_GUEST_DIR=' + str(guest),
                              '-DGUARDIAN_DRIVER_FIXTURE_DIR=' + str(guest / 'drivers')]
                if args.openssl_root:
                    configure += ['-DOPENSSL_ROOT_DIR=' + str(args.openssl_root.resolve())]
            if args.native_fixtures and not native_windows:
                configure += ['-DGUARDIAN_NATIVE_FIXTURE_DIR=' + str(args.native_fixtures.resolve())]
            legacy = args.legacy_storage or ROOT / 'build-phase12b/legacy-storage.gk'
            if legacy.is_file():
                configure += ['-DGUARDIAN_LEGACY_STORAGE_KERNEL=' + str(legacy.resolve())]
            step('configure-' + config, configure)
            if native_windows:
                definitions = list(host.glob('CMakeFiles/*/CMakeCXXCompiler.cmake'))
                if not any('set(CMAKE_CXX_COMPILER_ID "MSVC")' in p.read_text() for p in definitions):
                    raise RuntimeError('Windows acceptance requires the native Microsoft compiler')
            step('build-' + config, [cmake, '--build', host, '--config', guest_config,
                                     '--parallel', args.jobs])
            xml = output / (('windows-' if native_windows else 'linux-') + config.lower() + '.xml')
            step('tests-' + config, [ctest, '--test-dir', host, '-C', guest_config,
                                     '--output-on-failure', '--output-junit', xml])
            tests = list(ET.parse(xml).getroot().iter('testcase'))
            if not tests or any(t.find('failure') is not None or t.find('error') is not None or
                                t.find('skipped') is not None for t in tests):
                raise RuntimeError('JUnit does not contain a complete passing configuration')
            proof['configurations'][config] = {'tests': len(tests), 'status': 'PASS', 'xml': xml.name}
            save()
        stable()
        proof['status'] = 'PASS'
        proof['sourceStable'] = True
        save()
        print('Native platform matrix PASS; phase completion still requires scope review.', flush=True)
    except Exception as error:
        proof['status'] = 'FAIL'
        proof['error'] = str(error)
        save()
        raise


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
