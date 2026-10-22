"""Validate the unchanged 12C revision, then compare available native references.

All build options are shared with validate_phase12b.py. Add --reference FILE
once per exact native NT version when using authorized external observations.
The script never substitutes Wine or Linux observations for native Windows.
"""
import argparse
import json
from pathlib import Path
import sys

import conformance_matrix
import validate_phase12b as native_matrix


def main():
    extra = argparse.ArgumentParser(add_help=False)
    extra.add_argument('--reference', type=Path, action='append', default=[])
    selected, remaining = extra.parse_known_args()
    if '-h' in remaining or '--help' in remaining:
        print(__doc__, flush=True)
        print('Additional option: --reference FILE (repeat once per exact NT version).', flush=True)
    sys.argv = [sys.argv[0], *remaining]
    native_matrix.main()
    output_parser = argparse.ArgumentParser(add_help=False)
    output_parser.add_argument('--output', type=Path, required=True)
    args, _ = output_parser.parse_known_args(remaining)
    output = args.output.resolve()
    result_path = output / 'result.json'
    result = json.loads(result_path.read_text(encoding='utf-8'))
    expected = json.loads((output / 'source-inputs.json').read_text(encoding='utf-8'))
    result.update(phase='12C', phaseComplete=False, prerequisite12BComplete=False,
                  conformance={}, programHash=conformance_matrix.program_id())
    references = [json.loads(path.read_text(encoding='utf-8')) for path in selected.reference]
    try:
        for config in result['configurations']:
            if config == 'Sanitize':
                continue
            host = output / ('host-' + config)
            matrix_path = host / ('conformance-' + config) / 'matrix.json'
            available = list(references)
            automatic = host / ('conformance-reference-' + config) / 'latest.json'
            if automatic.is_file():
                name = json.loads(automatic.read_text(encoding='utf-8'))['reference']
                if Path(name).name != name:
                    raise ValueError('Invalid reference index')
                proof = json.loads((automatic.parent / name).read_text(encoding='utf-8'))
                if not any(p.get('metadata', {}).get('nt') == proof['metadata']['nt'] for p in available):
                    available.append(proof)
            comparison = conformance_matrix.compare(json.loads(matrix_path.read_text(encoding='utf-8')), available)
            (output / ('conformance-' + config.lower() + '.json')).write_text(
                json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
            result['conformance'][config] = comparison
            if comparison['status'] != 'PASS':
                raise ValueError('Native conformance differences remain; see preserved comparison')
        if native_matrix.source_inputs() != expected:
            raise ValueError('Sources changed during differential validation')
        result['sourceStable'] = True
        result['status'] = 'PASS'
        print('12C platform checks PASS; phase completion still requires prerequisite and native reference review.', flush=True)
    except Exception as error:
        result.update(status='FAIL', error=str(error))
        raise
    finally:
        result_path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
