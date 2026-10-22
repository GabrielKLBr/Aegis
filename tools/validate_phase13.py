"""Validate the complete unchanged phase-13 revision on the native platform.

Preserves the phase-12E matrix and all prior gates. Linux/KVM is not Windows/WHP
acceptance. Run with the arguments of validate_phase12e.py in a fresh directory.
"""
import argparse
import json
from pathlib import Path
import sys
import validate_phase12b as matrix
import validate_phase12e


def main():
    if any(flag in sys.argv for flag in ('-h','--help')):
        print(__doc__);validate_phase12e.main();return
    parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--output',type=Path,required=True)
    args,_=parser.parse_known_args();validate_phase12e.main()
    output=args.output.resolve();path=output/'result.json';result=json.loads(path.read_text())
    result.update(phase='13',phaseComplete=False,prerequisitesComplete=False,analysis={})
    try:
        for config in result['configurations']:
            host=output/('host-'+config);selected='Debug' if config=='Sanitize' else config
            core=json.loads((host/('analysis-core-'+selected)/'core.json').read_text())
            if core['status']!='PASS' or core['checks']<1230 or not core['realVolumePolicy'] or not core['untrustedInputs']:
                raise ValueError('Behavioral core, input or actual volume policy checks incomplete')
            proof={'core':core}
            if config!='Sanitize':
                boots=json.loads((host/('analysis-boots-'+config)/'validation.json').read_text())
                lifecycle=json.loads((host/('analysis-lifecycle-'+config)/'validation.json').read_text())
                if boots['status']!='PASS' or boots['scenarios']!=5 or boots.get('combinedDomains')!=9 or boots['wine'] or not all(boots.get(flag) for flag in ('snapshotRestored','changedPolicyRejected','realNetworkEvents','realDriverEvents','personalCanaryAbsent')):
                    raise ValueError('Actual event, policy, device or snapshot validation incomplete')
                if lifecycle['status']!='PASS' or lifecycle['wine'] or not all(lifecycle.get(flag) for flag in ('realManagedVMs','exportVerified','independentRuleOracle','policyEffectsVerified','lossVisible','cloneAndRestore','changedPolicyRejected','hostPrivateFileIntact')):
                    raise ValueError('Managed VM, independent oracle, export or loss checks incomplete')
                for name in ('allow','deny','simulate','benign','overflow','snapshot','network','driver'):
                    report=json.loads((host/('analysis-boots-'+config)/(name+'.json')).read_text())
                    if name=='overflow':
                        if report['complete'] or not report['dropped']:raise ValueError('Event loss was hidden')
                    elif not report['complete'] or report['dropped'] or report['rejected']:raise ValueError('Unexpected incomplete actual collection')
                report=json.loads((host/('analysis-boots-'+config)/'allow.json').read_text())
                domains={e['domain'] for e in report['events']}
                if not {'process','thread','module','file','registry','service','driver','network','memory'}<=domains:
                    raise ValueError('Combined actual timeline lacks a required domain')
                proof.update(boots=boots,lifecycle=lifecycle)
            result['analysis'][config]=proof
        if matrix.source_inputs()!=json.loads((output/'source-inputs.json').read_text()):
            raise ValueError('Sources changed during behavioral validation')
        result.update(status='PASS',sourceStable=True,telemetryVersion=1,policyVersion=1,rulesVersion=1,
                      untrustedGuestInput=True,guestOnlyEffects=True,nextPhaseStarted=False)
        print('13 native behavioral acceptance PASS; prior scope and native Windows gates remain open.',flush=True)
    except Exception as error:
        result.update(status='FAIL',error=str(error));raise
    finally:
        path.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':
    try:main()
    except Exception as error:print(str(error),file=sys.stderr);sys.exit(1)
