"""Loopback-only test peers using public synthetic keys, never host trust stores."""
from pathlib import Path
import base64,contextlib,hashlib,json,socket,socketserver,ssl,threading,time

FIXTURES=Path(__file__).resolve().parent/'fixtures/internet/certificates.json'
BODY=b'MiniNT HTTP works\n'

def pem(label,data):
    fence='-'*5
    return (fence+'BEGIN '+label+fence+'\n').encode()+base64.encodebytes(data)+(fence+'END '+label+fence+'\n').encode()

class Tcp(socketserver.ThreadingTCPServer):
    allow_reuse_address=False
    daemon_threads=True
    block_on_close=False
    def handle_error(self,request,address):
        self.events.append({'peerError':True})

class Echo(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(3)
        try:
            while True:
                data=self.request.recv(2048)
                if not data:return
                self.server.events.append({'echoBytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
                self.request.sendall(data)
        except OSError:return

class UdpEcho(socketserver.BaseRequestHandler):
    def handle(self):
        data,sock=self.request
        self.server.events.append({'udpBytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
        sock.sendto(data,self.client_address)

class Http(socketserver.BaseRequestHandler):
    def handle(self):
        stream=self.request
        if getattr(self.server,'tls',None):
            try:
                stream=self.server.tls.wrap_socket(stream,server_side=True)
                self.server.events.append({'tlsVersion':stream.version(),'cipher':stream.cipher()[0]})
            except (OSError,ssl.SSLError):
                self.server.events.append({'tlsRejected':True});return
        stream.settimeout(3)
        try:
            raw=b''
            while b'\r\n\r\n' not in raw:
                part=stream.recv(2048)
                if not part or len(raw)+len(part)>8192:return
                raw+=part
            headers,body=raw.split(b'\r\n\r\n',1)
            lines=headers.split(b'\r\n');request=lines[0].split(b' ')
            if len(request)!=3:return
            fields={line.split(b':',1)[0].strip().lower():line.split(b':',1)[1].strip() for line in lines[1:] if b':' in line}
            length=int(fields.get(b'content-length',b'0'))
            if length<0 or length>65536:return
            while len(body)<length:
                part=stream.recv(min(2048,length-len(body)))
                if not part:return
                body+=part
            path=request[1]
            self.server.events.append({'method':request[0].decode('ascii'),'path':path.decode('ascii'),'bodyBytes':len(body),'bodySha256':hashlib.sha256(body).hexdigest()})
            if path==b'/stall':
                stream.settimeout(.1);deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    try:
                        if not stream.recv(1):break
                    except socket.timeout:continue
                return
            if path==b'/chunked':
                stream.sendall(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\n'+BODY[:5]+b'\r\nD\r\n'+BODY[5:]+b'\r\n0\r\n\r\n');return
            response=body if path==b'/echo' else BODY
            status=b'200 OK'
            if path==b'/auth' and fields.get(b'authorization')!=b'Basic '+base64.b64encode(b'guest-user:guest-only'):
                status=b'401 Unauthorized';response=b'credentials required\n'
            stream.sendall(b'HTTP/1.1 '+status+b'\r\nContent-Length: '+str(len(response)).encode()+b'\r\nConnection: close\r\n\r\n'+response)
        except (OSError,ValueError,ssl.SSLError):return
        finally:
            if stream is not self.request:stream.close()

class Lab(contextlib.AbstractContextManager):
    def __init__(self,folder,certificate='server'):
        self.folder=Path(folder);self.folder.mkdir(parents=True,exist_ok=True)
        self.events=[];self.servers=[];self.threads=[]
        fixtures=json.loads(FIXTURES.read_bytes());self.certificates={name:bytes.fromhex(data) for name,data in fixtures['certificates'].items()}
        cert=self.folder/'test-server.pem';key=self.folder/'public-test-key.pem'
        cert.write_bytes(pem('CERTIFICATE',self.certificates[certificate]))
        if certificate=='rsa':private=bytes.fromhex(fixtures['publicRsaServerKeyPkcs8'])
        else:private=bytes.fromhex('3041020100301306072A8648CE3D020106082A8648CE3D030107042730250201010420')+int(fixtures['serverPrivateScalar']).to_bytes(32,'big')
        key.write_bytes(pem('PRIVATE KEY',private))
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=context.maximum_version=ssl.TLSVersion.TLSv1_2;context.set_ciphers('ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256');context.load_cert_chain(cert,key)
        echo=self.tcp(Echo);self.echo=echo.server_address[1]
        udp=socketserver.ThreadingUDPServer(('127.0.0.1',self.echo),UdpEcho);udp.daemon_threads=True;self.add(udp)
        self.http=self.tcp(Http).server_address[1]
        tls=self.tcp(Http);tls.tls=context;self.tls=tls.server_address[1]
        self.stall=self.tcp(Http).server_address[1]
    def add(self,server):
        server.events=self.events;self.servers.append(server)
        thread=threading.Thread(target=server.serve_forever,kwargs={'poll_interval':.02},daemon=True);thread.start();self.threads.append(thread);return server
    def tcp(self,handler):return self.add(Tcp(('127.0.0.1',0),handler))
    def config(self,folder,expectation=0):
        folder=Path(folder);folder.mkdir(parents=True,exist_ok=True);path=folder/'network.cfg';path.write_text(f'{self.echo} {self.http} {self.tls} {self.stall} {expectation}\n',encoding='ascii');return path
    def policy(self):
        return {'mode':'lab','endpoints':[{'name':'localhost','address':'127.0.0.1','port':port,'transport':transport} for port,transport in ((self.echo,'tcp'),(self.echo,'udp'),(self.http,'tcp'),(self.tls,'tcp'),(self.stall,'tcp'))]}
    def __exit__(self,*args):
        for server in self.servers:server.shutdown();server.server_close()
        for thread in self.threads:thread.join(1)
        (self.folder/'peers.json').write_text(json.dumps(self.events,indent=2)+'\n',encoding='utf-8')
        return False
