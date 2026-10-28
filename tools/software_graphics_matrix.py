"""Validate declared software COM slots separately from measured compatibility."""
from pathlib import Path
import argparse,json,re
import nt_compatibility as nt
def validate(build,config):
    spec=json.loads((nt.ROOT/'config/software-graphics-api-matrix.json').read_bytes())
    nt.require(spec['phase']=='14.10F' and spec['protocol']==6 and not any(spec[k] for k in ('fullDirectX','hardwareAcceleration','shadersAvailable')),'Software graphics matrix header')
    code=(nt.ROOT/'guest/fixtures/SoftwareGraphicsStubs.inl').read_text(encoding='utf-8')
    declared=set()
    for match in re.finditer(r'class Stub_(\w+):.*?(?=\nclass |\Z)',code,re.S):
        for method in re.findall(r'\bSTDMETHODCALLTYPE\s+(\w+)\(',match[0]):declared.add((match[1],method))
    entries=spec['entries'];nt.require(len(entries)==len({(e['interface'],e['method']) for e in entries}) and declared=={(e['interface'],e['method']) for e in entries},'Missing/duplicate COM matrix slots')
    nt.require(all(e['status'] in ('implemented_with_restrictions','pending') and e['contract'] for e in entries),'Invalid COM method classification')
    expected={'d2d1':['D2D1CreateFactory'],'dwrite':['DWriteCreateFactory'],'windowscodecs':['DllGetClassObject','DllCanUnloadNow'],'d3d11':['D3D11CreateDevice','D3D11CreateDeviceAndSwapChain'],'dxgi':['CreateDXGIFactory','CreateDXGIFactory1','CreateDXGIFactory2'],'d3d9':['Direct3DCreate9','Direct3DCreate9Ex']}
    for dll,names in expected.items():
        actual={name for e in nt.InventoryPe(Path(build)/'guest/software-graphics'/config/(dll+'.dll')).export_records() for name in e['names']}
        nt.require(set(names)==actual,'Software graphics facade exports changed: '+dll)
    return {'phase':'14.10F','status':'PASS','entries':len(entries),'restrictedMethods':sum(e['status']=='implemented_with_restrictions' for e in entries),'pendingMethods':sum(e['status']=='pending' for e in entries),'inventoryIsNotImplementationProof':True}
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);a=p.parse_args();print(json.dumps(validate(a.build,a.config)))
