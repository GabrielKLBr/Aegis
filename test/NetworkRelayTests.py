"""Independent golden wire vectors, relay framing and denied address families."""
import importlib.util
import ipaddress
import json
import pathlib
import struct

ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('relay',ROOT/'tools/guardian_udp_relay.py');relay=importlib.util.module_from_spec(spec);spec.loader.exec_module(relay)
checks=0

def check(ok):
    global checks
    checks+=1
    if not ok:raise RuntimeError('Relay vector check '+str(checks))

def reject(function):
    try:function()
    except (ValueError,struct.error):check(True);return
    check(False)

vectors=json.loads((ROOT/'test/fixtures/network/golden.json').read_text())
for vector in vectors:
    raw=bytes.fromhex(vector['hex']);message=relay.MAGIC+bytes(range(16))+struct.pack('!H',len(raw))+bytes(6)+raw
    decoded=relay.decode(message);check(decoded[0]==vector['family']);check(decoded[5]==bytes([1,2,3,4,5]));check(decoded[3:5]==(40000,7))
    answer=relay.response(message,decoded,b'response');reply=relay.decode(answer);check(reply[:5]==(decoded[0],decoded[2],decoded[1],7,40000));check(reply[5]==b'response')
    for offset in (0,26,len(message)-1):
        bad=bytearray(message);bad[offset]^=1;reject(lambda:relay.decode(bytes(bad)))
    reject(lambda:relay.decode(message[:-1]));reject(lambda:relay.decode(message+b'\0'))
    reject(lambda:relay.response(message,decoded,bytes(1281)))
for address in ('127.0.0.1','192.168.1.1','169.254.169.254','100.64.0.1','198.18.0.1','224.0.0.1','::1','::ffff:127.0.0.1','fe80::1','fd00::1','ff02::1','2001::1','2002:7f00:1::','64:ff9b::7f00:1','2001:db8::1'):
    reject(lambda:relay.public(address))
check(relay.public('1.1.1.1')==ipaddress.ip_address('1.1.1.1'));check(relay.public('2606:4700:4700::1111').version==6)
print(str(checks)+' independent wire/relay checks PASS')
