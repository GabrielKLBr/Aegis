"""Enroll hash-bound MiniNT packages for the scanner. No application runs on the host."""
from pathlib import Path
import argparse,json,shutil,struct
import nt_compatibility as nt,pe_volume,compat32_runtime

def enroll(registry,bundle,kernel,helper):
    registry,bundle,kernel,helper=map(Path,(registry,bundle,kernel,helper))
    manifest=json.loads(nt.regular(bundle/'manifest.json',4<<20));mode={'14.10B':'user32','14.11':'frameworks','14.12':'compat32'}.get(manifest.get('phase'))
    nt.require(mode and manifest.get('profile')=='win10','Unsupported unified runtime family/profile')
    if mode=='compat32':compat32_runtime.verify(bundle,kernel,helper)
    else:pe_volume.verify(bundle)
    nt.require(nt.sha(nt.regular(kernel,8<<20))==manifest['kernelSha256'],'Kernel SHA mismatch')
    nt.require(nt.sha(nt.regular(helper,16<<20))==manifest['nativeGuiRuntime']['helperSha256'],'Helper SHA mismatch')
    digest=manifest['applicationSha256'];nt.require(len(digest)==64 and all(c in '0123456789abcdef' for c in digest),'Application SHA rejected')
    registry.mkdir(parents=True,exist_ok=True);index=registry/'index.json'
    data=json.loads(index.read_bytes()) if index.exists() else {'schema':1,'phase':'14.13','applications':{}}
    nt.require(data.get('schema')==1 and data.get('phase')=='14.13' and digest not in data['applications'] and len(data['applications'])<128,'Duplicate or invalid runtime registry')
    target=registry/digest;nt.require(not target.exists(),'Fresh enrollment required');target.mkdir()
    files={}
    for source in (kernel,helper,bundle/'runtime.gpv',bundle/'manifest.json',bundle/'binding.gnt'):
        name=digest+'/'+source.name;shutil.copy2(source,registry/name);files[name]=nt.sha(nt.regular(registry/name,64<<20))
    record={'architecture':manifest['architecture'],'profile':'win10','mode':mode,'kernel':digest+'/'+kernel.name,'helper':digest+'/'+helper.name,'volume':digest+'/runtime.gpv','manifest':digest+'/manifest.json','binding':digest+'/binding.gnt','files':files}
    data['applications'][digest]=record;temporary=registry/'index.new';temporary.write_bytes(nt.canonical(data));temporary.replace(index)
    return {'status':'ENROLLED','sha256':digest,'architecture':manifest['architecture'],'mode':mode,'applications':len(data['applications'])}

def prepare(build,config,registry,application,work,reference=None,schema_reference=None,family='auto', included_modules=()):
    build,application,work=map(Path,(build,application,work));raw=nt.regular(application,8<<20);nt.require(raw[:2]==b'MZ','PE input required');machine=struct.unpack_from('<H',raw,struct.unpack_from('<I',raw,60)[0]+4)[0]
    bundle=work/'bundle';work.mkdir(parents=True,exist_ok=True)
    if machine==0x14C:
        compat32_runtime.bundle(build,config,bundle,application);kernel=build/'guest'/config/'guardian-kernel-compat32.gk';helper=build/config/'guardian-native-user-host.exe'
    else:
        nt.require(machine==0x8664 and reference and schema_reference,'x64 preparation requires local legal CRT/schema references')
        if family=='user32':
            import native_user32_runtime as runtime
            runtime.bundle(build,config,bundle,reference,schema_reference,application);kernel=build/'guest'/config/'guardian-kernel-native-user32.gk';helper=build/config/'guardian-native-user-host.exe'
        else:
            import framework_runtime as runtime
            runtime.bundle(build,config,bundle,reference,schema_reference,application,included_modules=included_modules);kernel=build/'guest'/config/'guardian-kernel-frameworks.gk';helper=build/config/'guardian-audio-host.exe'
    return enroll(registry,bundle,kernel,helper)

def automatic_templates(registry, digests):
    """Opt in to input rebinding; enrolled originals remain untouched."""
    registry=Path(registry);index=registry/'index.json';data=json.loads(nt.regular(index,2<<20))
    nt.require(0<len(digests)<=16 and len(set(digests))==len(digests),'Automatic template quota')
    for digest in digests:nt.require(digest in data['applications'],'Unknown template')
    data['automaticTemplates']=list(digests);temporary=registry/'index.new';temporary.write_bytes(nt.canonical(data));temporary.replace(index)
    return {'templates':len(digests)}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','registry','application','work'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',choices=('Release','Debug'),default='Release');p.add_argument('--reference',type=Path);p.add_argument('--schema-reference',type=Path);p.add_argument('--family',choices=('auto','user32'),default='auto');a=p.parse_args()
    print(json.dumps(prepare(a.build,a.config,a.registry,a.application,a.work,a.reference,a.schema_reference,a.family)))
