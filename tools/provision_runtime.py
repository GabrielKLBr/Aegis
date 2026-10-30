"""Provision local MiniNT templates; Microsoft reference files are never downloaded.

Only the owned probe inputs are enrolled. Later EXEs are assembled in memory by
guardian-runtime, without Python, a compiler, or host execution of the input.
Do not redistribute this generated directory: it contains local Windows DLLs.
"""
from pathlib import Path
import argparse
import json
import unified_runtime as unified
import nt_compatibility as nt


def provision(build, config, registry, work, reference, schema_reference):
    build, registry, work = map(Path, (build, registry, work))
    nt.require(not registry.exists(), 'A fresh template directory is required')
    specifications = (
        ('frameworks', build/'guest/frameworks'/config/'guardian-framework-winrt.exe',
         'auto', ('gdi32.dll', 'dwmapi.dll', 'winmm.dll', 'msvcrt.dll',
                  'msvcp140.dll', 'vcruntime140_1.dll', 'shell32.dll', 'comctl32.dll', 'comdlg32.dll')),
        ('user32', build/'guest/unified'/config/'guardian-unified-probe.exe', 'user32', ()),
        ('compat32', build/'guest/compat32'/config/'guardian-x86-input.exe', 'auto', ()),
    )
    rows = []
    for name, application, family, modules in specifications:
        rows.append(unified.prepare(build, config, registry, application, work/name,
                                    reference, schema_reference, family, modules))
    unified.automatic_templates(registry, [row['sha256'] for row in rows])
    return {'status': 'PROVISIONED', 'templates': rows, 'inputExecutedOnHost': False,
            'redistributable': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'registry', 'work', 'reference', 'schema-reference'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--config', choices=('Release', 'Debug'), default='Release')
    args = parser.parse_args()
    print(json.dumps(provision(args.build, args.config, args.registry, args.work,
                               args.reference, args.schema_reference)))
