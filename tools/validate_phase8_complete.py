"""Validate the final phase-8 workload without starting phase 9.

Linux validates real guest execution; native Windows additionally validates
MSVC/WHP and the Microsoft API oracles. Neither result implies the other.
"""
import datetime
import json
import os
import pathlib
import subprocess
import sys
from validate_linux_native import ROOT, require, source_hashes

def main():
    policy=json.loads((ROOT/'config/phase8-completion-acceptance.json').read_text())
    require(policy['phase']==8 and not policy['next_phase_started'] and not policy['wine_allowed'],'Invalid phase contract')
    if sys.platform=='linux':
        from validate_linux_kvm import main as validate
        evidence=validate()
        report=json.loads((evidence/'validation.json').read_text())
        require(report['status']=='passed' and len(report['boots'])==policy['linux_successful_boots'],'Incomplete Linux acceptance')
        for row in report['configurations']:
            log={item['name']:item['output'] for item in row['tests']}
            require('415 exception dispatch/continuation/nonlocal unwind checks PASS' in log['guest_exception_dispatch'],'Missing exception core evidence')
            require('49 dynamic mapping rollback fault points PASS' in log['guest_pe_loader'],'Missing dynamic rollback evidence')
        result={'phase':8,'implementation':'complete_for_owned_console_matrix','validation':'linux_passed',
            'phase_status':'awaiting_native_windows_acceptance','next_phase_started':False,
            'linux_report':str(evidence/'validation.json'),'native_windows':'not_run',
            'windows_command':'python tools/validate_phase8_complete.py','sources':source_hashes(),
            'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
        (evidence/'phase8-completion.json').write_text(json.dumps(result,indent=2)+'\n')
        print('Owned phase-8 functionality passed on Linux. Native Windows acceptance remains pending. '+str(evidence),flush=True)
        return
    if os.name=='nt':
        # The Windows validator builds a fresh directory, rejects skipped
        # tests and requires the actual RtlVirtualUnwind differential results.
        subprocess.run([sys.executable,ROOT/'tools/validate_phase8_handlers.py'],cwd=ROOT,check=True)
        print('Native Windows phase-8 suite and oracles passed. Preserve the generated manifest for the final phase record.',flush=True)
        return
    raise RuntimeError('Linux x86-64 or native Windows x64 required')

if __name__=='__main__':
    main()
