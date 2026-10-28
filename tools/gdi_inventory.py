"""Inventory is evidence of declared contracts; implementation is validated by native/guest tests."""
from pathlib import Path
import argparse,json,re
import nt_compatibility as nt
MATRIX=nt.ROOT/'config/gdi-api-matrix.json'
def matrix():return json.loads(MATRIX.read_bytes())
def exported(build,config):
    result={}
    for dll in ('user32','gdi32','comdlg32','comctl32','dwmapi','win32u'):
        pe=nt.InventoryPe(Path(build)/'guest/expanded-gdi'/config/(dll+'.dll'));result[dll+'.dll']={n for record in pe.export_records() for n in record['names']}
    return result
def inspect(application,build,config):
    symbols=exported(build,config);known={(row['dll'],row['function']):row for row in matrix()['entries']};rows=[]
    for imported in nt.InventoryPe(application).import_records():
        dll=imported['module'];name=imported['symbol']
        if dll not in symbols:continue
        item=known.get((dll,name))
        status=item['status'] if item else 'exported_requires_execution_test' if name in symbols[dll] else 'pending'
        rows.append({'dll':dll,'function':name,'status':status,'available':name in symbols[dll]})
    return rows
def require_imports(application,build,config):
    missing=[r for r in inspect(application,build,config) if not r['available']]
    nt.require(not missing,'Unsupported expanded GUI imports: '+', '.join(r['dll']+'!'+r['function']+' ('+r['status']+')' for r in missing))
def validate(build,config):
    spec=matrix();entries=spec['entries'];allowed={'implemented','implemented_with_restrictions','denied_by_policy','pending'}
    nt.require(spec['phase']=='14.10E' and spec['protocol']==5 and spec['completeGdi'] is False,'GUI matrix header')
    nt.require(len({(r['dll'],r['function']) for r in entries})==len(entries) and all(r['status'] in allowed and r['contract'] for r in entries),'GUI matrix schema')
    symbols=exported(build,config)
    nt.require(all((dll,name) in {(r['dll'],r['function']) for r in entries} for dll,names in symbols.items() for name in names),'Uninventoried GUI export')
    for row in entries:
        present=row['function'] in symbols.get(row['dll'],set())
        nt.require(present if row['status'].startswith('implemented') or row.get('availability')=='exported_refusal' else not present,'Matrix/export mismatch: '+row['function'])
    counts={s:sum(row['status']==s for row in entries) for s in sorted(allowed)}
    return {'phase':'14.10E','status':'PASS','entries':len(entries),'states':counts,'exportInventoryIsNotImplementationProof':True}
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--application',type=Path);a=p.parse_args();print(json.dumps(inspect(a.application,a.build,a.config) if a.application else validate(a.build,a.config)))
if __name__=='__main__':main()

