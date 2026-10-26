# Fase 14.9 — rede, HTTP, TLS e criptografia no MiniNT

A 14.9 acrescenta um lote delimitado para **Windows 10 x64**, com 111 exports
em dez DLLs próprias: WS2_32, MSWSOCK, DNSAPI, IPHLPAPI, WINHTTP, WININET, BCRYPT,
NCRYPT, CRYPT32 e SECUR32. Elas encaminham os contratos implementados para
`guardianinet.dll` dentro do convidado. Os clientes de aceitação usam imports
normais do SDK Windows, incluindo os ordinais Winsock selecionados pelo SDK.
Não há implementação completa dessas bibliotecas nem aceitação de aplicativos
Windows arbitrários. Outros perfis NT ainda precisam de suas matrizes próprias.

## Transporte e política

`guardian-kernel-internet.gk` conserva os serviços da 14.8 e acrescenta um
dispositivo de transporte de camada 4. O host mantém sockets TCP/UDP limitados;
o convidado implementa os contratos Winsock, HTTP, criptografia e TLS. Esta
entrega não contém uma pilha TCP/IP completa dentro do kernel convidado.

A entrada A9 somente aceita código executável de `guardianinet.dll`. O kernel
copia mensagens, verifica limites e atribui PID e sequência. O protocolo tem
2.184 bytes, payload de 2.048 bytes, 16 sockets e oito destinos por sessão.
Não transporta ponteiros, callbacks nem handles do host. Identificadores são
monotônicos e pertencem ao processo; repetição, socket de outro PID e mensagens
inválidas são recusados. Saída de processo e fechamento recolhem os sockets,
inclusive depois de esgotada uma cota.

| Modo | Comportamento |
| --- | --- |
| Offline, padrão | DNS e conexões são negados. Entropia continua disponível. |
| Simulado | Endereços sintéticos em 198.18.0.0/24, eco TCP/UDP, HTTP fixo ou silêncio. Nenhum socket nativo é aberto. |
| Laboratório | Somente destinos e portas explicitamente declarados em 127.0.0.1. Usado pelos testes com pares locais. |
| Autorizado | Destinos IPv4 públicos fixos e portas declaradas; verificação adicional exclui endereços e redes locais do host. Não há descoberta nem encaminhamento aberto. |

DNS usa exclusivamente os nomes/endereço da política; não chama o resolvedor
do host. Nomes são ASCII minúsculos, até 63 caracteres. Não há DNS externo,
proxy automático, IPv6, sockets de escuta, RAW ou acesso geral à rede local.
O modo autorizado é validado quanto às restrições, mas a matriz de aceitação
não depende de servidores públicos nem realiza testes na Internet.

O limite padrão é de 32.768 operações e 2 MiB somados de envio, recepção e
entropia. A política admite 16–65.536 operações e 4 KiB–16 MiB. Excesso falha;
não amplia automaticamente o orçamento. `internet.json` registra operações,
resultados, bytes, sockets nativos abertos e recursos ainda ativos. Os eventos
também entram no domínio de rede da telemetria. A política é um arquivo binário
canônico de 800 bytes; `tools/internet_runtime.py policy` valida e converte JSON.

## Contratos do convidado

| Família | Lote implementado |
| --- | --- |
| WS2_32 / MSWSOCK | Clientes IPv4 TCP e UDP conectado, nomes numéricos e mapeamentos fixos, envio/recepção, não bloqueante, WSAPoll, timeout, SO_ERROR, shutdown completo, I/O overlapped com evento e worker convidado, cancelamento por fechamento e TransmitFile de arquivo convidado. |
| DNSAPI / IPHLPAPI | Consultas A/W de registros A, liberação compatível com o import DnsFree do SDK e um adaptador virtual, sem inventário de interfaces pessoais. |
| WINHTTP | GET/POST/HEAD HTTP/1.1, status/cabeçalhos, corpo por Content-Length ou chunked simples, leitura/escrita em partes, callbacks assíncronos em threads convidadas, timeout, cancelamento e Basic explícito sobre TLS. |
| WININET | Wrappers síncronos de sessão/conexão/requisição, consulta, leitura e abertura de URL para o mesmo transporte limitado. |
| BCRYPT | SHA-1/256/384/512, HMAC, AES CBC de blocos inteiros e GCM, ECDSA P-256, importação/exportação limitada e verificação de assinaturas. GCM valida a autenticação antes de publicar plaintext. |
| NCRYPT | Provedor software próprio, chaves P-256 nomeadas no HKCU virtual, abrir/assinar/verificar/excluir e política de exportação. Exportação privada é negada por padrão. |
| CRYPT32 | Contextos DER com metadados, cópia/refcount, lojas em memória, ROOT virtual, cadeia direta folha/raiz e políticas base/SSL com nome, assinatura, tempo e uso de servidor. |
| SECUR32 | Credenciais Schannel de saída próprias, handshake por tokens, atributos de contexto, certificado remoto e EncryptMessage/DecryptMessage de registros TLS. |

O adaptador exposto representa o dispositivo virtual de transporte. Seus
endereços/portas locais não identificam o socket físico utilizado pelo host.
Overlapped aceita um WSABUF de até 2.048 bytes e evento; completion routines,
IOCP, WSAEventSelect e semântica completa de todos os flags não estão neste lote.
UDP preserva datagramas e informa truncamento; há até oito datagramas pendentes
por socket simulado. O eco TCP simulado tem 64 KiB de fila.

HTTP limita o corpo a 64 KiB, cabeçalhos a 8 KiB e 24 objetos vivos. Não há
HTTP/2, WebSocket, redirect automático, compressão, cache, cookies, PAC/WPAD,
autenticação integrada, trailers ou extensões chunked. Requests não suportados
falham. `RECEIVE_RESPONSE_TIMEOUT` trata cabeçalhos e `RECEIVE_TIMEOUT` trata
recepção; a comparação nativa cobre os dois limites. WinINet assíncrono não é
implementado. Credenciais Basic são fornecidas pelo aplicativo convidado e não
são obtidas do usuário ou do armazenamento do host.

## Criptografia, certificados e TLS

Os algoritmos executam em CPL3 com BearSSL, fontes MIT sem modificações,
fixados em `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f`. A licença e os hashes dos
309 arquivos estão em `guest/third_party/bearssl`. A configuração usa software
portável, sem seleção automática de RNG/tempo do sistema pelo BearSSL. O canal
de entropia usa o gerador do host; não exporta suas chaves ou credenciais. O
relógio vem do serviço de tempo já existente no convidado.

TLS é **1.2**, com ECDHE-ECDSA ou ECDHE-RSA, AES-128-GCM e SHA-256. Handshake,
derivação de chaves, validação e registros executam dentro do MiniNT. O host não
termina TLS. Não há TLS 1.3, renegociação, certificado cliente, NTLM/Kerberos ou
SSPI geral. Flags para ignorar validação ou utilizar credenciais pessoais são
recusados. CBC não implementa padding PKCS#7; RSA não é um algoritmo público
CNG deste lote. SHA-1 existe por compatibilidade; não altera a seleção TLS.

São admitidos até 32 contextos de certificados, oito lojas em memória com oito
entradas cada e 16 cadeias vivas. A ROOT persistente contém **uma** âncora CA DER
de até 512 bytes; uma segunda raiz ou duplicação além dessa cota falha. Lojas
em memória tratam NEW, USE_EXISTING, REPLACE_EXISTING e ALWAYS, com comparação
de emissor e serial. CERT_INFO projeta versão, serial, algoritmos, emissor,
assunto, validade, chave pública e até 16 extensões. O parser é limitado a DER
de 8 KiB e serial de 32 bytes; OIDs têm no máximo 95 caracteres.

A validação cobre a cadeia direta folha/raiz do corpus, nome de servidor,
assinatura, validade e EKU serverAuth. Não há descoberta AIA, intermediárias
arbitrárias, lojas adicionais nem políticas de uso arbitrárias. Parâmetros
fora desse contrato são recusados. Solicitação de revogação retorna estado
desconhecido e falha de política; não é declarada revogação verificada.

Chaves nomeadas aceitam até 31 caracteres minúsculos/dígitos/hífen e são gravadas
como blobs no registro virtual da 14.6. Esse disco de 64 KiB não é um cofre
cifrado. O isolamento é do namespace e da sessão; quem possui o estado exportado
pode inspecioná-lo. Nenhuma chave, loja ROOT ou credencial do host é montada.

## Persistência, catálogo e execução

GPV2 permanece limitado a 32 arquivos, 24 módulos por processo, 8 MiB por
arquivo e 64 MiB por volume. `.cfg` e `.der` passam a ser dados permitidos em
`C:\App` para GPV2; GPV1 conserva sua lista anterior. Dependências são incluídas
por alcance e os imports de rede são verificados antes do boot. Microsoft CRT
e schema continuam sendo entradas locais legais e não acompanham a build.

`--state` conserva o disco virtual entre sessões, inclusive chave e raiz de
teste. Captura e restauração de snapshots de RAM são recusadas para esse
transporte: sockets e estado de pares externos não são reproduzíveis pela RAM.
A execução continua limitada a 1–30 segundos por sessão no CLI.

```powershell
python -B tools/internet_runtime.py bundle --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output runtime-internet
.\PASTA-BUILD\Release\guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-internet.gk runtime-internet/runtime.gpv runtime-internet/manifest.json runtime-internet/binding.gnt sessao
```

O aplicativo padrão verifica criptografia offline. Para declarar destinos, use
um JSON com `mode`, `endpoints` e cotas opcionais; cada endpoint tem `name`,
`address`, `port`, `transport` (`tcp`/`udp`) e `service` simulado opcional:

```json
{"mode":"simulated","endpoints":[{"name":"localhost","address":"198.18.0.254","port":7,"transport":"tcp"}]}
```

Converta com `python -B tools/internet_runtime.py policy politica.json politica.bin`
e acrescente `--network-policy politica.bin` à execução. Omitir esse argumento
seleciona offline.

## Aceitação e reprodução

Há nove testes novos: `internet_core`, `internet_runtime_tools`,
`internet_crypto_reference` e `internet_{crypto,sockets,http,tls,sspi,reference}_integration`.
Os testes de integração iniciam **20 sessões MiniNT por configuração**, além de
quatro clientes nativos contra pares loopback próprios. A referência CNG nativa
usa somente chaves efêmeras. Os pares TLS usam certificados e chaves sintéticos
intencionalmente públicos em `test/fixtures/internet/certificates.json`.
Eles nunca são instalados no Windows ou usados como credenciais de produção.

A matriz cobre vetores conhecidos SHA/HMAC/AES e RFC 6979, tag/assinatura
inválidas, chave reaberta em outra sessão, TCP/UDP, DNS permitido/negado,
simulação sem sockets nativos, HTTP chunked/POST, leitura parcial assíncrona,
cancelamento, timeout e Basic sobre TLS. TLS negocia com pares reais ECDSA/RSA;
rejeita certificado vencido, nome errado, assinatura alterada, EKU somente
cliente e raiz ausente. SSPI testa handshake, registro parcial, plaintext e
registro adulterado. A loja virtual persistente é usada em uma segunda sessão.

`tools/validate_phase14_9.py` exige **133 testes na regressão Release** e
**58 na aceitação Debug**, sem ignorados, preservando hashes dos fontes,
binários e referências antes/depois. As regressões incluem as fases anteriores
e o Flappy original. O pacote exige as evidências estáveis e é executado outra
vez nas duas configurações, após verificar todos os arquivos e o ZIP.
As contagens são critérios exigidos pelo validador; o resultado da execução
fica nos `validation.json` da build privada. Linux não foi revalidado nesta fase.

A entrega termina na 14.9. A **14.10 e a 12F não são iniciadas**; compatibilidade
completa Windows, GUI nativa por janela e os lotes seguintes permanecem abertos.

Referências: [BearSSL](https://www.bearssl.org/),
[CNG](https://learn.microsoft.com/windows/win32/api/bcrypt/),
[Schannel](https://learn.microsoft.com/windows/win32/secauthn/initializesecuritycontext--schannel),
[opções WinHTTP](https://learn.microsoft.com/windows/win32/winhttp/option-flags),
[lojas de certificados](https://learn.microsoft.com/windows/desktop/api/wincrypt/nf-wincrypt-certaddcertificatecontexttostore),
[cadeias](https://learn.microsoft.com/windows/win32/api/wincrypt/nf-wincrypt-certgetcertificatechain)
e [RFC 6979](https://www.rfc-editor.org/rfc/rfc6979.html).
