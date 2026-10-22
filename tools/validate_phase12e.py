"""Validate one unchanged 12E revision natively, including all previous tests.

Uses the arguments of validate_phase12d.py, with optional exact native 12C
references. Linux/KVM and Windows/MSVC/WHP evidence remain distinct.
"""
import argparse
import json
from pathlib import Path
import sys
import validate_phase12b as matrix
import validate_phase12d


def main():
    if any(flag in sys.argv for flag in ('-h','--help')):
        print(__doc__)
        validate_phase12d.main()
        return
    parser=argparse.ArgumentParser(add_help=False)
    parser.add_argument('--output',type=Path,required=True)
    args,_=parser.parse_known_args()
    validate_phase12d.main()
    output=args.output.resolve();path=output/'result.json'
    result=json.loads(path.read_text(encoding='utf-8'))
    result.update(phase='12E',phaseComplete=False,prerequisitesComplete=False,environments={})
    try:
        for config in result['configurations']:
            host=output/('host-'+config);selected='Debug' if config=='Sanitize' else config
            core=json.loads((host/('environment-core-'+selected)/'core.json').read_text())
            if core['status']!='PASS' or core['checks']<89:
                raise ValueError('Environment core checks incomplete')
            proof={'core':core}
            if config!='Sanitize':
                profiles=json.loads((host/('environment-boots-'+config)/'profiles.json').read_text())
                lifecycle=json.loads((host/('environment-lifecycle-'+config)/'lifecycle.json').read_text())
                if (profiles['status']!='PASS' or len(profiles['profiles'])!=12 or profiles['restores']!=12 or
                    profiles['changedPolicyRejected']!=12 or not profiles['personalCanaryAbsent'] or
                    lifecycle['status']!='PASS' or lifecycle['scenarioPairs']!=6 or lifecycle['wine'] or
                    not all(lifecycle.get(flag) for flag in ('seedReproduction','documentsOpened',
                        'executableCatalogVerified','persistentEvolution','disposableReset',
                        'cloneAndRestore','personalCanaryAbsent'))):
                    raise ValueError('Actual profile, document, persistence or privacy validation incomplete')
                proof.update(profileBoots=len(profiles['profiles']),restores=profiles['restores'],
                             changedPolicyRejected=profiles['changedPolicyRejected'],lifecycle=lifecycle)
            result['environments'][config]=proof
        if matrix.source_inputs()!=json.loads((output/'source-inputs.json').read_text()):
            raise ValueError('Sources changed during synthetic profile validation')
        result.update(status='PASS',sourceStable=True,syntheticProfiles=True,hostPersonalImports=False,
                      profileVersion=1,contentVersion=1,nextPhaseStarted=False)
        print('12E native profile acceptance PASS; Windows and previous phase gates remain explicit.',flush=True)
    except Exception as error:
        result.update(status='FAIL',error=str(error));raise
    finally:
        path.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    try:main()
    except Exception as error:
        print(str(error),file=sys.stderr);sys.exit(1)
