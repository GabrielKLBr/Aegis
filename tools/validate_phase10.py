"""Validate phase 10 on real KVM or native Windows/MSVC/WHP, without Wine/QEMU."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
import uuid

from validate_linux_native import ROOT, elf, require, sha, source_hashes
import struct
import xml.etree.ElementTree as ET

def tests(path,count):
    cases=ET.parse(path).getroot().findall(".//testcase")
    require(len(cases)==count and len({c.get("name") for c in cases})==count,"Incomplete test inventory")
    require(all(not c.findall("failure") and not c.findall("error") and not c.findall("skipped") and c.get("status","run")=="run" for c in cases),"Failed/skipped test")
    return [{"name":c.get("name"),"output":c.findtext("system-out","")} for c in cases]

from validate_linux_kvm import WORKLOADS, MARKERS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    policy = json.loads((ROOT / 'config/phase10-acceptance.json').read_text())
    require(policy['phase'] == 10 and not policy['next_phase_started'], 'Invalid phase-10 policy')
    windows = os.name == 'nt'
    require((windows or sys.platform == 'linux') and platform.machine().lower() in ('amd64', 'x86_64'),
            'Native Windows x64 or Linux x86-64 required')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S')
    evidence = ROOT / 'build-phase10' / ('validation-' + stamp + '-' + uuid.uuid4().hex[:8])
    evidence.mkdir(parents=True, exist_ok=False)
    environment = dict(os.environ, GUARDIAN_BUILD_JOBS='4')
    sentinel = evidence / 'forbidden-executor-invoked'
    if not windows:
        traps = evidence / 'sentinels'
        traps.mkdir()
        for name in ('wine', 'wine64', 'cmd.exe', 'powershell.exe', 'qemu-system-x86_64'):
            file = traps / name
            file.write_text('#!/bin/sh\nprintf invoked >> "' + str(sentinel) + '"\nexit 97\n')
            file.chmod(0o700)
        environment['PATH'] = str(traps) + os.pathsep + environment.get('PATH', '')
    before = source_hashes()
    result = {'phase': 10, 'status': 'running', 'phase_status': 'in_progress', 'next_phase_started': False,
              'platform': platform.platform(), 'backend': 'WHP' if windows else 'KVM',
              'wine_used': False, 'qemu_used': False, 'sources': before,
              'configurations': [], 'regression_boots': [], 'negative_guests': []}
    print('Evidence: ' + str(evidence), flush=True)

    def run(command, name, env=None, expected=0):
        command = list(map(str, command))
        log = evidence / (name + '.log')
        with log.open('w', encoding='utf-8') as stream:
            stream.write(json.dumps(command) + '\n')
            stream.flush()
            completed = subprocess.run(command, cwd=ROOT, env=env or environment,
                                       stdout=stream, stderr=subprocess.STDOUT, timeout=900)
        require(completed.returncode == expected, 'Exit ' + str(completed.returncode) + '; see ' + str(log))
        return log

    def application_proofs(host, configuration):
        data = tests(host / ('tests-' + configuration + '.xml' if windows else 'tests.xml'),
                     policy['windows_ctest_count'] if windows else policy['linux_ctest_count'])
        by_name = {entry['name']: entry['output'] for entry in data}
        for name, expression, minimum in (
            ('guest_session_snapshot', r'(\d+) complete session snapshot', policy['snapshot_minimum_checks']),
            ('vm_catalog', r'(\d+) VM catalog ownership', policy['catalog_minimum_checks']),
            ('vm_manager', r'(\d+) managed VM lifecycle', policy['manager_minimum_checks']),
            ('guest_storage_core', r'(\d+) initial volume commit recovery checks', policy['volume_initialization_checks'])):
            match = re.search(expression, by_name.get(name, ''))
            require(match and int(match[1]) >= minimum, 'Missing behavior proof: ' + name)
        require('x64 GPR XMM x87 FS GS CPUID pending IRQ and physical mappings restored PASS' in
                by_name['guest_session_snapshot'], 'Architectural state did not resume correctly')
        suffix = pathlib.Path(configuration) if windows else pathlib.Path()
        snapshot_report = host / 'snapshot-tests' / suffix / 'snapshot-boots.json'
        rows = json.loads(snapshot_report.read_text())['boots']
        require(sum(row['success'] for row in rows) == policy['snapshot_successful_boots_per_configuration'] and
                sum(row['paused'] for row in rows) == policy['snapshot_paused_boots_per_configuration'],
                'Incomplete RAM/checkpoint/branch snapshot matrix')
        for row in rows:
            require(row['success'] or (row['paused'] and not row['timedOut'] and not row['error']),
                    'Snapshot guest did not stop coherently')
            if row['success']:
                require('PE dynamic DLL TLS references rollback unload PASS' in row['console'] and
                        row['processes']['liveObjects'] == row['processes']['liveHandles'] ==
                        row['processes']['liveReferences'] == 0, 'Snapshot application cleanup incomplete')
        manager_report = host / 'manager-tests' / suffix / 'manager-validation.json'
        manager = json.loads(manager_report.read_text())
        require(manager['status'] == 'passed' and manager['worker_crash_recovery'] and
                manager['persistent_and_disposable'] and manager['neighbor_files_preserved'] and
                manager['concurrent_vms'] == 2 and len(manager['completed_boots']) == 8,
                'Managed VM lifecycle evidence incomplete')
        for boot in manager['completed_boots']:
            require(sha(pathlib.Path(boot['report'])) == boot['sha256'], 'Managed boot evidence changed')
        network_rows=json.loads((host/'network-boots'/suffix/'network-boots.json').read_text())['boots']
        require(len(network_rows)==policy['network_successful_boots_per_configuration'] and all(r['success'] and r['networkReportValid'] and r['networkChecks']==63 and r['networkUserCalls']>=9 for r in network_rows),'Real network boot/resume matrix incomplete')
        require(sum('Guest network IPv4 IPv6 UDP DNS containment PASS' in r['console'] for r in network_rows)==16,'Guest stack did not run')
        network_manager=json.loads((host/'network-manager'/suffix/'network-manager-validation.json').read_text())
        require(network_manager['status']=='passed' and network_manager['boots']==8 and all(network_manager[key] for key in ('offline_default','policy_snapshot_binding','durable_audit','lab_two_workers','duplicate_identity_refused','private_proxy_refused')),'Managed network profile proof incomplete')
        match=re.search(r'(\d+) network checks',by_name.get('network',''))
        require(match and int(match[1])>=policy['network_minimum_checks'] and '20000 malformed inputs PASS' in by_name['network'],'Parser/policy/failure coverage missing')
        require('39 independent wire/relay checks PASS' in by_name.get('network_relay',''),'Independent packet/relay oracle missing')
        capture_count=0
        for capture in host.rglob('network.pcap'):
            raw=capture.read_bytes();require(len(raw)>=24 and struct.unpack_from('<I',raw)[0]==0xa1b2c3d4 and struct.unpack_from('<I',raw,20)[0]==101,'Invalid bounded raw-IP capture')
            at=24
            while at<len(raw):
                require(at+16<=len(raw),'Truncated capture record');_,_,n,original=struct.unpack_from('<IIII',raw,at);at+=16
                require(0<n<=1280 and n==original and at+n<=len(raw),'Capture record escaped device MTU');at+=n
            require(at==len(raw) and len(raw)<=524288,'Capture quota violated');capture_count+=1
        require(capture_count>=8,'Missing durable packet captures')
        if windows:
            for name, expression, count in (
                ('guest_unwind_handlers', r'(\d+) Windows handler differential frames PASS', 153),
                ('guest_unwind_core', r'(\d+) Windows differential frames PASS', 59)):
                match = re.search(expression, by_name[name])
                require(match and int(match[1]) == count and 'NOT RUN' not in by_name[name],
                        'Native Microsoft API comparison did not execute')
        return {'configuration': configuration, 'tests': data,
                'snapshot_report': str(snapshot_report.relative_to(ROOT)), 'snapshot_sha256': sha(snapshot_report),
                'manager_report': str(manager_report.relative_to(ROOT)), 'manager_sha256': sha(manager_report),
                'network_boots':len(network_rows),'network_manager_boots':network_manager['boots'],'captures_checked':capture_count}

    try:
        fixture = ROOT / 'test/fixtures/phase9/SnapshotProbe.json'
        manifest = json.loads(fixture.read_text())
        require(sha(fixture.with_suffix('.asm')) == manifest['assembly_sha256'] and
                sha(fixture.with_suffix('.h')) == manifest['header_sha256'], 'Owned architectural probe changed')
        if windows:
            host = evidence / 'host'
            run(['cmake', '-S', ROOT, '-B', host, '-G', 'Visual Studio 17 2022', '-A', 'x64',
                 '-DBUILD_TESTING=ON', '-DGUARDIAN_BUILD_GUEST_CONTRACT=ON'], 'configure')
            for configuration in ('Debug', 'Release'):
                print('Building and validating MSVC/WHP ' + configuration, flush=True)
                run(['cmake', '--build', host, '--config', configuration, '--parallel', '4'], configuration + '-build')
                run(['cmake', '--build', host, '--config', configuration, '--target', 'guardian_guest'], configuration + '-audit')
                run(['ctest', '--test-dir', host, '-C', configuration, '--output-on-failure',
                     '--output-junit', host / ('tests-' + configuration + '.xml')], configuration + '-ctest')
                result['configurations'].append(application_proofs(host, configuration))
        else:
            identifier = 1000
            for configuration in ('Debug', 'Release'):
                print('Building and validating ELF/KVM ' + configuration, flush=True)
                guest, host = evidence / ('guest-' + configuration), evidence / ('host-' + configuration)
                run([sys.executable, ROOT / 'tools/build_guest_linux.py', '--config', configuration,
                     '--output', guest], configuration + '-guest')
                env = dict(environment, GUARDIAN_LINUX_BUILD_DIR=str(host), GUARDIAN_TEST_KVM='ON',
                           GUARDIAN_KVM_GUEST_DIR=str(guest))
                run(['bash', ROOT / 'Build-Linux.sh', configuration], configuration + '-host', env)
                row = application_proofs(host, configuration)
                row['artifacts'] = [elf(p) for p in sorted(host.iterdir()) if p.is_file() and
                                    (p.name in ('guardianav', 'guardian-kvm', 'guardian-vm') or p.name.endswith('_tests'))]
                result['configurations'].append(row)
                print(configuration + ': 27 CTests and complete snapshot/manager matrix PASS', flush=True)
                for name, contract in WORKLOADS:
                    for ram in (16, 32, 64):
                        previous = None
                        disk = evidence / (configuration + '-' + name + '-' + str(ram))
                        for iteration in range(2):
                            identifier += 1
                            tag = configuration + '-' + name + '-' + str(ram) + '-' + str(iteration)
                            target = guest / ('guardian-kernel' + ('' if name == 'basic' else '-' + name) + '.gk')
                            report = evidence / (tag + '.json')
                            command = [host / 'guardianav', 'boot-kernel', target, contract, report,
                                       '--ram-mib', ram, '--boot-id', identifier, '--timeout-ms', policy['regression_timeout_ms']]
                            if contract == 'storage':
                                command += ['--disk', disk.with_suffix('.gvd'), disk.with_suffix('.gvo'), '--create-disk']
                            run(command, tag)
                            boot = json.loads(report.read_text())['boots'][0]
                            require(boot['success'] and not boot['timedOut'] and not boot['paused'] and not boot['error'],
                                    'Regression boot failed')
                            for marker in MARKERS.get(name, []):
                                require(marker in boot['console'] + boot.get('io', {}).get('console', ''),
                                        'Application proof missing: ' + marker)
                            if contract == 'storage':
                                require(sha(disk.with_suffix('.gvd')) == hashlib.sha256(bytes(65536)).hexdigest(),
                                        'Regression modified its base')
                                if previous:
                                    require(boot['storage']['recovered'] == 1 and
                                            boot['storage']['sequenceBefore'] == previous['storage']['sequenceAfter'],
                                            'Persistent sequence did not survive reboot')
                            previous = boot
                            result['regression_boots'].append({'configuration': configuration, 'workload': name,
                                'ram_mib': ram, 'iteration': iteration, 'report': str(report.relative_to(ROOT)),
                                'sha256': sha(report), 'success': True})
                    print(configuration + ' ' + name + ': 6 regression boots PASS', flush=True)
                for name in ('cpu-fatal', 'cpu-double-fault'):
                    report = evidence / (configuration + '-' + name + '.json')
                    run([host / 'guardian-kvm', guest / ('guardian-kernel-' + name + '.gk'), 'cpu', report],
                        configuration + '-' + name, expected=3)
                    boot = json.loads(report.read_text())['boots'][0]
                    require(not boot['success'] and boot['cpuReportValid'] and boot['cpu']['fatal'] == 1,
                            'Fatal/IST control accepted as success')
                    result['negative_guests'].append({'report': str(report.relative_to(ROOT)), 'sha256': sha(report)})
            print('Building and validating AddressSanitizer/UndefinedBehaviorSanitizer', flush=True)
            host = evidence / 'host-Sanitize'
            env = dict(environment, GUARDIAN_LINUX_BUILD_DIR=str(host), GUARDIAN_TEST_KVM='ON',
                       GUARDIAN_KVM_GUEST_DIR=str(evidence / 'guest-Release'))
            run(['bash', ROOT / 'Build-Linux.sh', 'Sanitize'], 'Sanitize-host', env)
            sanitized=application_proofs(host,'Sanitize')
            sanitized['artifacts']=[elf(p) for p in sorted(host.iterdir()) if p.is_file() and (p.name in ('guardianav','guardian-kvm','guardian-vm') or p.name.endswith('_tests'))]
            result['configurations'].append(sanitized)
            require(len(result['regression_boots']) == policy['successful_regression_boots'] and
                    len(result['negative_guests']) == 4 and not sentinel.exists(), 'Incomplete Linux acceptance')
        require(source_hashes() == before, 'Sources changed during acceptance')
        result['status'] = 'passed'
        result['phase_status'] = 'awaiting_linux_acceptance' if windows else 'awaiting_native_windows_acceptance'
        counterpart = 'KVM' if windows else 'WHP'
        for path in (ROOT / 'build-phase10').glob('validation-*/validation.json'):
            other = json.loads(path.read_text())
            if other.get('status') == 'passed' and other.get('backend') == counterpart and other.get('sources') == before:
                result['phase_status'] = 'complete'
                result['counterpart_evidence'] = str(path.relative_to(ROOT))
                break
    except BaseException as error:
        result['status'] = 'failed'
        result['error'] = str(error)
        raise
    finally:
        result['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (evidence / 'validation.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Phase 10 ' + result['backend'] + ' validation PASS; ' + result['phase_status'] + '. ' + str(evidence), flush=True)


if __name__ == '__main__':
    main()
