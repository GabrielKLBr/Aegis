from pathlib import Path
import argparse,json,subprocess,sys,tempfile,unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import api_set,software_graphics_inventory as inventory,software_graphics_runtime as runtime,pe_volume,nt_compatibility as nt

class ResolverTests(unittest.TestCase):
    def resolve(self,imports,exports,contracts=None):
        class Pe:
            def __init__(self,path):self.name=path.name
            def import_records(self):return [{'module':m,'symbol':s,'kind':'delay'} for m,s in imports.get(self.name,[])]
            def export_records(self):return [{'ordinal':i+1,'names':[name],'forwarder':forwarder} for i,(name,forwarder) in enumerate(exports.get(self.name,{}).items())]
        files=[{'path':'c:\\app\\'+name,'source':name,'kind':'pe'} for name in exports]
        schema=api_set.pe(api_set.encode(6,contracts or {'api-ms-guardian-l1-1-0':{'':'kernelbase.dll'}}))
        section=api_set.section(schema)
        with patch.object(nt,'InventoryPe',Pe),patch.object(api_set,'section',return_value=section):return inventory.require_imports(files,schema)
    def test_recursive_and_ordinal(self):
        self.assertTrue(self.resolve({'app.exe':[('a.dll','Go')]},{'app.exe':{},'a.dll':{'Go':'b.#1'},'b.dll':{'Go':None}})['allStaticAndDelayImportsResolved'])
    def test_revision_and_importer_alias(self):
        self.resolve({'app.exe':[('api-ms-guardian-l1-1-0.dll','Go')]},{'app.exe':{},'kernelbase.dll':{'Go':None}},{'api-ms-guardian-l1-1-2':{'':'unavailable.dll','app.exe':'kernelbase.dll'}})
    def test_missing_and_cycle(self):
        for imports,exports in (({'app.exe':[('d3d12.dll','D3D12CreateDevice')]},{'app.exe':{}}),({'app.exe':[('a.dll','Missing')]},{'app.exe':{},'a.dll':{'Go':None}}),({'app.exe':[('a.dll','Go')]},{'app.exe':{},'a.dll':{'Go':'b.Go'},'b.dll':{'Go':'a.Go'}})):
            with self.subTest(exports=exports):
                with self.assertRaises(ValueError):self.resolve(imports,exports)
def main():
    p=argparse.ArgumentParser()
    for name in ('build','reference','schema-reference','evidence'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args()
    result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(ResolverTests));assert result.wasSuccessful()
    with tempfile.TemporaryDirectory(prefix='aegis-software-tools-') as directory:
        folder=Path(directory);bundle=folder/'bundle'
        manifest=runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference);assert pe_volume.verify(bundle)['phase']=='14.10F'
        base=[str(a.build/a.config/'guardian-desktop.exe'),str(a.build/'guest'/a.config/'guardian-kernel-software-graphics.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt')]
        for index,options in enumerate(([],['--software-graphics','--expanded-gdi'],['--software-graphics','--integrated'],['--software-graphics','--interactive'],['--software-graphics','--native-controls'])):
            output=folder/str(index);run=subprocess.run([*base,str(output),*options],capture_output=True,text=True,timeout=15);assert run.returncode and not output.exists()
        manifest['nativeGuiRuntime']['helperSha256']='0'*64;raw=nt.canonical(manifest);(bundle/'manifest.json').write_bytes(raw);binding=bytearray((bundle/'binding.gnt').read_bytes());binding[48:80]=bytes.fromhex(nt.sha(raw));(bundle/'binding.gnt').write_bytes(binding)
        output=folder/'wrong-helper';run=subprocess.run([*base,str(output),'--software-graphics'],capture_output=True,text=True,timeout=15);assert run.returncode and not output.exists()
    print(json.dumps({'phase':'14.10F','status':'PASS','resolverTests':result.testsRun,'negativeModes':5,'negativeHelperBinding':1}))
if __name__=='__main__':main()
