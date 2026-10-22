"""Optional UDP relay for an explicitly configured external laboratory host.

Never started by GuardianAV. No DNS lookup, TCP, raw host sockets or automatic
routes. An operator must provide client IPs and exact public UDP destinations.
The framing is GVUDP01 + random request token + bounded raw IPv4/IPv6 packet.
This protocol does not provide encryption or authenticated transport.
"""
import argparse
import ipaddress
import json
import socket
import struct
import time

MTU=1280
MAGIC=b'GVUDP01\0'


def public(text):
    address=ipaddress.ip_address(text)
    if not address.is_global or address.is_multicast:
        raise ValueError('A public unicast address is required')
    # Match the conservative subset accepted by the host, including refusal
    # of local/translation/tunnel/documentation destinations.
    blocks=('0.0.0.0/8','10.0.0.0/8','100.64.0.0/10','127.0.0.0/8','169.0.0.0/8','172.16.0.0/12','192.0.0.0/8','198.18.0.0/15','198.51.100.0/24','203.0.113.0/24','224.0.0.0/3')
    if address.version==4 and any(address in ipaddress.ip_network(n) for n in blocks):raise ValueError('Special IPv4 address rejected')
    if address.version==6 and (address not in ipaddress.ip_network('2000::/3') or any(address in ipaddress.ip_network(n) for n in ('2001::/23','2001:db8::/32','2002::/16','3ffe::/16','3fff::/16'))):raise ValueError('Special IPv6 address rejected')
    return address


def checksum(data):
    if len(data)%2:data+=b'\0'
    total=sum(struct.unpack('!'+str(len(data)//2)+'H',data))
    while total>>16:total=(total&65535)+(total>>16)
    return ~total&65535


def decode(message):
    if len(message)<60 or len(message)>MTU+32 or message[:8]!=MAGIC or any(message[26:32]):raise ValueError('Envelope rejected')
    raw=message[32:]
    if struct.unpack_from('!H',message,24)[0]!=len(raw):raise ValueError('Envelope length rejected')
    family=raw[0]>>4
    if family==4:
        if raw[0]!=0x45 or struct.unpack_from('!H',raw,2)[0]!=len(raw) or checksum(raw[:20]) or not raw[8] or raw[9]!=17 or struct.unpack_from('!H',raw,6)[0]&0xbfff:raise ValueError('IPv4 profile rejected')
        source,target=raw[12:16],raw[16:20];head=20;pseudo=source+target+b'\0\x11'+struct.pack('!H',len(raw)-20)
    elif family==6:
        if len(raw)<48 or struct.unpack_from('!H',raw,4)[0]+40!=len(raw) or raw[6]!=17 or not raw[7]:raise ValueError('IPv6 profile rejected')
        source,target=raw[8:24],raw[24:40];head=40;pseudo=source+target+struct.pack('!I',len(raw)-40)+b'\0\0\0\x11'
    else:raise ValueError('IP family rejected')
    sport,dport,size,check=struct.unpack_from('!HHHH',raw,head)
    if not sport or not dport or size!=len(raw)-head or not check or checksum(pseudo+raw[head:]):raise ValueError('UDP checksum/shape rejected')
    return family,source,target,sport,dport,raw[head+8:]


def response(message,decoded,payload):
    family,source,target,sport,dport,_=decoded
    n=8+len(payload)
    if n+(20 if family==4 else 40)>MTU:raise ValueError('Reply exceeds MTU')
    udp=struct.pack('!HHHH',dport,sport,n,0)+payload
    pseudo=target+source+(b'\0\x11'+struct.pack('!H',n) if family==4 else struct.pack('!I',n)+b'\0\0\0\x11')
    udp=udp[:6]+struct.pack('!H',checksum(pseudo+udp) or 65535)+udp[8:]
    if family==4:
        ip=struct.pack('!BBHHHBBH4s4s',0x45,0,n+20,0,0,64,17,0,target,source)
        ip=ip[:10]+struct.pack('!H',checksum(ip))+ip[12:]
    else:ip=struct.pack('!IHBB16s16s',0x60000000,n,17,64,target,source)
    raw=ip+udp
    return message[:24]+struct.pack('!H',len(raw))+bytes(6)+raw


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--bind',required=True);parser.add_argument('--port',type=int,required=True)
    parser.add_argument('--client',action='append',required=True,help='Permitted numeric client IPv4 address (repeatable)')
    parser.add_argument('--allow',action='append',required=True,help='Exact family,address,UDP-port (repeatable)');parser.add_argument('--requests',type=int,default=128)
    args=parser.parse_args();bind=public(args.bind);clients={str(public(n)) for n in args.client};allowed=set()
    if bind.version!=4 or not 1<=args.port<=65535 or not 1<=args.requests<=4096:parser.error('Invalid listener/resource bounds')
    for item in args.allow:
        family,text,port=item.split(',');address=public(text);port=int(port)
        if address.version!=int(family) or not 1<=port<=65535:parser.error('Invalid destination')
        allowed.add((address.version,address.packed,port))
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
        listener.bind((str(bind),args.port));listener.settimeout(30)
        for _ in range(args.requests):
            try:
                message,client=listener.recvfrom(MTU+33)
                if client[0] not in clients:raise ValueError('Client refused')
                decoded=decode(message);family,source,target,sport,dport,payload=decoded
                if (family,target,dport) not in allowed:raise ValueError('Destination refused')
                address=str(public(str(ipaddress.ip_address(target))))
                with socket.socket(socket.AF_INET if family==4 else socket.AF_INET6,socket.SOCK_DGRAM) as upstream:
                    upstream.settimeout(.075);upstream.connect((address,dport));upstream.send(payload);data=upstream.recv(MTU+1)
                listener.sendto(response(message,decoded,data),client)
                print(json.dumps({'time':time.time(),'status':'relayed','family':family,'target':address,'port':dport,'bytes':len(payload)}),flush=True)
            except (ValueError,OSError) as error:print(json.dumps({'time':time.time(),'status':'refused_or_unavailable','error':str(error)}),flush=True)

if __name__=='__main__':main()
