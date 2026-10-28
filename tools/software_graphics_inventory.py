"""Resolve selected PE imports and forwarders before a software graphics guest boots."""
from pathlib import Path
import api_set,nt_compatibility as nt

def require_imports(files,schema):
    modules={row['path'].rsplit('\\',1)[-1]:nt.InventoryPe(Path(row['source'])) for row in files if row['kind']=='pe'}
    version,contracts=api_set.parse(api_set.section(schema));exports={}
    for name,pe in modules.items():
        entries={}
        for row in pe.export_records():
            for symbol in (*row['names'],'#'+str(row['ordinal'])):entries[symbol]=row['forwarder']
        exports[name]=entries
    def resolve(module,symbol,importer,seen=()):
        if module.startswith(('api-ms-','ext-ms-')):
            original=module
            requested=module.removesuffix('.dll');options=None
            for contract,records in contracts.items():
                prefix,_,revision=contract.rpartition('-');wanted,_,number=requested.rpartition('-')
                if contract==requested or version==6 and prefix==wanted and revision.isdigit() and number.isdigit() and int(number)<=int(revision):
                    options=records;break
            options=options or {}
            aliases={k.removesuffix('.dll'):v for k,v in options.items()}
            module=aliases.get(importer.removesuffix('.dll'),aliases.get('',None))
            nt.require(module,'Unsupported software graphics API set: '+importer+' -> '+original+'!'+symbol)
        nt.require(module in exports,'Unsupported software graphics module: '+str(module))
        identity=(module,symbol);nt.require(identity not in seen and len(seen)<16,'Software graphics forwarder cycle/depth')
        nt.require(symbol in exports[module],'Unsupported software graphics import: '+importer+' -> '+module+'!'+symbol)
        forwarded=exports[module][symbol]
        if forwarded:
            dll,function=forwarded.rsplit('.',1);resolve(dll.lower()+'.dll',function,module,(*seen,identity))
    for name,pe in modules.items():
        for imported in pe.import_records():resolve(imported['module'],imported['symbol'],name)
    return {'modules':len(modules),'allStaticAndDelayImportsResolved':True}
