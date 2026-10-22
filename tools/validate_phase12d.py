"""Validate phase 12D on an unchanged native revision, retaining the 12C checks.

Uses the same arguments as validate_phase12c.py. Native Windows/MSVC and Linux/KVM
remain separate evidence. No Wine, no clock acceleration, no 12E work.
"""
import argparse
import json
from pathlib import Path
import sys
import validate_phase12b as matrix
import validate_phase12c


def main():
    if any(flag in sys.argv for flag in ("-h", "--help")):
        print(__doc__)
        validate_phase12c.main()
        return
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--output', type=Path, required=True)
    args, _ = parser.parse_known_args()
    validate_phase12c.main()
    output = args.output.resolve()
    path = output / 'result.json'
    result = json.loads(path.read_text(encoding='utf-8'))
    result.update(phase='12D', phaseComplete=False, prerequisitesComplete=False, sessions={})
    try:
        for config in result['configurations']:
            host = output / ('host-' + config)
            core = json.loads((host / ('session-core-' + ('Debug' if config == 'Sanitize' else config)) / 'core.json').read_text())
            if core['status'] != 'PASS' or core['checks'] < 25:
                raise ValueError('Session core coverage is incomplete')
            proof = {'core': core}
            if config != 'Sanitize':
                wait = json.loads((host / ('session-wait-' + config) / 'wait.json').read_text())
                lifecycle = json.loads((host / ('session-lifecycle-' + config) / 'lifecycle.json').read_text())
                if (wait['status'] != 'PASS' or wait['resumedWallMs'] < 650 or
                        not wait['coldRebootProgressRecovered'] or lifecycle['status'] != 'PASS' or
                        lifecycle['longSessionNs'] < 31000000000 or not lifecycle['workerHandoff'] or
                        not lifecycle['manualViewerRestore'] or not lifecycle['coldRestart'] or
                        not lifecycle['guestOnlyInputs'] or lifecycle['wine']):
                    raise ValueError('Real session wait/restart/interaction coverage is incomplete')
                proof.update(wait=wait, lifecycle=lifecycle)
            result['sessions'][config] = proof
        if matrix.source_inputs() != json.loads((output / 'source-inputs.json').read_text()):
            raise ValueError('Sources changed during session validation')
        result.update(status='PASS', sourceStable=True, timeMode='real', offlineTime='frozen',
                      experimentalVirtualTime=False, nativeWindowsAcceptance=sys.platform == 'win32')
        print('12D native session acceptance PASS; prior phase and Windows gates remain explicit.', flush=True)
    except Exception as error:
        result.update(status='FAIL', error=str(error))
        raise
    finally:
        path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
