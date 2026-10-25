"""Read bounded v2/v6 namespaces and build owned schema fixtures as PE metadata."""
from pathlib import Path
import argparse
import json
import struct
import nt_compatibility as nt

def section(raw):
    pe=nt.InventoryPe(Path('schema.dll'),raw)
    # Inventory's public RVA reader remains the authoritative bounds check.
    offset=struct.unpack_from('<I',raw,60)[0];count=struct.unpack_from('<H',raw,offset+6)[0];optional=struct.unpack_from('<H',raw,offset+20)[0]
    parts=[]
    for i in range(count):
        at=offset+24+optional+i*40
        if raw[at:at+8].rstrip(b'\0')==b'.apiset':
            size,start=struct.unpack_from('<II',raw,at+16);nt.require(size<=262144 and start+size<=len(raw),'Schema section range');parts.append(raw[start:start+size])
    nt.require(len(parts)==1,'Unique .apiset section required');return parts[0]

def parse(data):
    nt.require(8<=len(data)<=262144,'Schema extent');version=struct.unpack_from('<I',data)[0];nt.require(version in (2,6),'Schema version')
    def u(at):nt.require(0<=at<=len(data)-4,'Schema integer range');return struct.unpack_from('<I',data,at)[0]
    if version==6:
        nt.require(28<=u(4)<=len(data),'Schema size');data=data[:u(4)];count,entries,hashes=u(12),u(16),u(20)
        nt.require(hashes+count*8<=len(data),'Schema hash range')
        nt.require(all(u(hashes+i*8+4)<count for i in range(count)),'Schema hash index')
    else:count,entries=u(4),8
    nt.require(0<count<=4096 and entries+count*(24 if version==6 else 12)<=len(data),'Schema entry budget')
    def text(at,n):
        nt.require(n and not n%2 and n<=254 and at+n<=len(data),'Schema string range');s=data[at:at+n].decode('utf-16le')
        nt.require(all(c.isascii() and (c.isalnum() or c in '-_.') for c in s) and '..' not in s,'Schema name');return s.lower()
    result={}
    for i in range(count):
        at=entries+i*(24 if version==6 else 12);name=text(u(at+(4 if version==6 else 0)),u(at+(8 if version==6 else 4)))
        nt.require(name not in result,'Duplicate contract');offset=u(at+(16 if version==6 else 8));n=u(at+20) if version==6 else u(offset);values=offset if version==6 else offset+4
        nt.require(0<n<=64 and values+n*(20 if version==6 else 16)<=len(data),'Schema values')
        if version==6:nt.require(not u(at+12)%2 and u(at+12)<=u(at+8),'Hashed name length')
        records={}
        for j in range(n):
            value=values+j*(20 if version==6 else 16)+(4 if version==6 else 0);alias=text(u(value),u(value+4)) if u(value+4) else ''
            nt.require(alias not in records,'Duplicate importer override');records[alias]=text(u(value+8),u(value+12)) if u(value+12) else ''
        result[name]=records
    return version,result

def encode(version,mapping):
    nt.require(version in (2,6) and 0<len(mapping)<=4096,'Owned schema budget');count=len(mapping);header=28 if version==6 else 8;entry_size=24 if version==6 else 12
    data=bytearray(header+count*entry_size);struct.pack_into('<I',data,0,version)
    if version==6:struct.pack_into('<5I',data,8,0,count,header,0,31)
    else:struct.pack_into('<I',data,4,count)
    def add(blob):
        while len(data)%4:data.append(0)
        at=len(data);data.extend(blob);return at
    hashes=[]
    for i,(name,records) in enumerate(sorted(mapping.items())):
        raw=name.encode('utf-16le');name_at=add(raw);entries=[]
        for alias,host in records.items():
            a=alias.encode('utf-16le');h=host.encode('utf-16le');a_at=add(a) if a else 0;h_at=add(h) if h else 0
            entries.append(struct.pack('<5I',0,a_at,len(a),h_at,len(h)) if version==6 else struct.pack('<4I',a_at,len(a),h_at,len(h)))
        value_at=add(b''.join(entries) if version==6 else struct.pack('<I',len(entries))+b''.join(entries));at=header+i*entry_size
        if version==6:
            hashed=name.rsplit('-',1)[0];struct.pack_into('<6I',data,at,0,name_at,len(raw),len(hashed)*2,value_at,len(entries));hash_value=0
            for c in hashed:hash_value=(hash_value*31+ord(c))&0xffffffff
            hashes.append((hash_value,i))
        else:struct.pack_into('<3I',data,at,name_at,len(raw),value_at)
    if version==6:
        hash_at=add(b''.join(struct.pack('<2I',*h) for h in sorted(hashes)));struct.pack_into('<I',data,20,hash_at);struct.pack_into('<I',data,4,len(data))
    parse(bytes(data));return bytes(data)

def pe(data):
    size=(len(data)+511)&~511;raw=bytearray(512+size);raw[:2]=b'MZ';struct.pack_into('<I',raw,60,128);raw[128:132]=b'PE\0\0';struct.pack_into('<HHIIIHH',raw,132,0x8664,1,0,0,0,240,0x2022)
    at=152;struct.pack_into('<H',raw,at,0x20b);struct.pack_into('<Q',raw,at+24,0x180000000);struct.pack_into('<II',raw,at+32,4096,512)
    struct.pack_into('<II',raw,at+56,(4096+len(data)+4095)&~4095,512);struct.pack_into('<H',raw,at+68,3);struct.pack_into('<I',raw,at+108,16)
    raw[392:400]=b'.apiset\0';struct.pack_into('<IIIIIIHHI',raw,400,len(data),4096,size,512,0,0,0,0,0x40000040);raw[512:512+len(data)]=data;return bytes(raw)

OWNED={name:{'':'kernelbase.dll'} for name in ('api-ms-win-core-memory-l1-1-0','api-ms-win-core-synch-l1-1-0','api-ms-win-core-processthreads-l1-1-0','api-ms-win-core-file-l1-1-0','api-ms-win-core-libraryloader-l1-1-0','api-ms-win-core-heap-l1-1-0','api-ms-win-core-localization-l1-1-0','api-ms-win-core-sysinfo-l1-1-0','api-ms-win-core-threadpool-l1-1-0')}
def build(profile,original=None,hosts=('kernelbase.dll','kernel32.dll','ntdll.dll')):
    version=2 if profile=='win7' else 6;mapping=OWNED.copy();source='owned bounded profile fixture';digest=None
    if original:
        raw=nt.regular(Path(original),8<<20);actual,reference=parse(section(raw));nt.require(actual==version,'Reference schema version mismatch')
        mapping={k:v for k,v in reference.items() if any(h in hosts for h in v.values())};source='selected native schema, re-encoded data only';digest=nt.sha(raw)
    output=pe(encode(version,mapping));return output,{'schemaVersion':version,'contracts':len(mapping),'source':source,'referenceSha256':digest,'exactTargetReference':False}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('dll',type=Path);a=p.parse_args();v,m=parse(section(nt.regular(a.dll,8<<20)));print(json.dumps({'version':v,'contracts':len(m),'coreMemory':m.get('api-ms-win-core-memory-l1-1-0')}))
