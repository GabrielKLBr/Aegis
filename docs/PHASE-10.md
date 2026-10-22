# Fase 10 — Rede virtual e contenção

Iniciada em 18/10/2026 por solicitação explícita do usuário. Implementação e aceitação Linux aprovadas; gate nativo Windows pendente.
Windows/MSVC/WHP continua sendo o produto; Linux/KVM é compilado e executado
nativamente, sem Wine ou QEMU. Aceitação nativa Windows permanece separada.

## Aceitação final no Linux

**Implementação concluída para o perfil descrito; matriz Linux aprovada.
Aceitação nativa Windows/MSVC/WHP pendente. A fase 11 não foi iniciada.**

Evidência final: `build-phase10/validation-20261018-180022-c09af2d0/validation.json`.
Estado: `passed` / `awaiting_native_windows_acceptance`. Todas as fontes foram
reconstruídas e seus hashes permaneceram iguais do início ao fim. Sem Wine,
QEMU, testes ignorados ou reaproveitamento de binários de rodadas anteriores.

| Configuração | CTests | Convidados de rede reais | Capturas conferidas |
| --- | --- | --- | --- |
| Debug | 27 aprovados | 16 boots/retomadas + 8 pelo gerenciador | 18 capturas |
| Release | 27 aprovados | 16 boots/retomadas + 8 pelo gerenciador | 18 capturas |
| Sanitize | 27 aprovados | 16 boots/retomadas + 8 pelo gerenciador | 18 capturas |

Totais desta aceitação:

- 81 execuções CTest, 132 boots de regressão das 11 imagens das fases anteriores
  e quatro controles fatal/IST corretamente recusados como sucesso.
- 72 boots/retomadas de rede, com IPv4/IPv6, CPL3, DNS, laboratório de quatro
  convidados, isolamento, clonagem, política de snapshots e histórico durável.
  Há também nove pausas verificadas nos checkpoints de rede.
- 345 verificações do dispositivo/política e 20.000 entradas malformadas por
  configuração; 39 verificações independentes de serialização/relay por configuração.
- Preservada a matriz da fase 9: 81 boots/retomadas do console completo,
  27 pausas, 24 boots do gerenciador e 221 verificações de snapshot por configuração.
  Inclui 589 verificações da inicialização do volume/52 prefixos de falha e
  64 transações interrompidas de criação/clonagem por configuração.
- Preparação Windows: 12 módulos compilados para objetos COFF por LLVM ELF;
  não houve execução Windows. O gate nativo exige 47 CTests em Debug/Release,
  inclusive 153 comparações de handlers e 59 de unwind com APIs Microsoft.

Os limites funcionais continuam explícitos: UDP no dispositivo próprio e
APIs próprias em CPL3, sem ABI Winsock/TCP; relay externo real não exercitado;
snapshots não revertem o mundo externo; proteção física de execução KVM
continua distinta da fornecida por WHP.

Pacote Linux entregue: `dist/guardianav-phase10-linux-x86_64-20261018.tar.gz`.
A extração e a execução fora da pasta do projeto foram aprovadas; resultado
registrado na seção do pacote abaixo.

## Perfil implementado

Dispositivo ponto a ponto de camada IP, MTU 1280, UDP IPv4/IPv6 com checksums,
DNS e eco simulados, laboratório isolado entre VMs do mesmo catálogo e saída
UDP por relay explícito. Sem rede é o padrão. TCP, Ethernet, roteamento,
fragmentação, extensões IPv6, multicast, broadcast e descoberta automática não
serão anunciados como suportados. Pedidos sem suporte serão recusados.

Política por VM limita endereços, portas, pares, filas, bytes e operações.
Nenhum tráfego de laboratório usa a interface física do host. Proxy exige
endereço público fixo do relay e lista explícita de destinos públicos/portas;
nomes DNS vêm da política, sem consultar o resolvedor do host. Captura PCAP e
registro de decisões incluem recusas. Snapshots vinculam política e estado do
dispositivo; efeitos em pares/relay já realizados não podem ser desfeitos.

Fontes anteriores em `snapshots/codex-before-phase10-20261018`.
Resultados finais no início deste documento; histórico de desenvolvimento preservado abaixo.

## Progresso antes da aceitação completa

Pré-testes Linux: 238 verificações iniciais de política/parser, 20.000 entradas
malformadas, 16 boots/retomadas KVM e oito boots pelo gerenciador aprovados.
Foram acrescentados vetores binários independentes e 39 verificações do relay.
A matriz completa Debug/Release/Sanitize e a regressão das fases anteriores
serão executadas depois da revisão das fontes.

Falhas encontradas e corrigidas durante desenvolvimento: montagem de pacote
com entrada/saída no mesmo buffer; necessidade de stack probe no convidado;
qualificação do namespace no executor de linha de comando; reutilização do
mesmo diretório de captura nos testes. Logs de desenvolvimento permanecem em
`build-phase10/development-logs`. Aprovação parcial não é a aceitação final.

## Uso do dispositivo e da pilha

A nova imagem é `guardian-kernel-network.gk`, contrato `network`. Inclui os
probes de CPU/CPL3 anteriores e um driver de rede do kernel. O programa em
CPL3 usa syscalls 0x90/0x91, com cópia limitada à sua página de dados; endereços
de kernel, overflow e buffers acima da MTU são recusados antes do dispositivo.
São APIs próprias do perfil de teste, sem ABI Winsock. Os consoles PE da fase
8 conservam seu contrato atual e não anunciam sockets não implementados.

A ligação carrega pacotes IP diretamente, com MTU 1280 e fila de oito quadros.
O parser exige tamanhos exatos, checksum IPv4 e checksum UDP não nulo nas duas
famílias. Não aceita opções IPv4, fragmentação, extensões IPv6, ICMP, TCP,
Ethernet, multicast, broadcast, IP-in-IP, GRE, Teredo ou descoberta automática.
IPv4 UDP sem checksum também não pertence ao perfil. Não há TAP/TUN, bridge,
NAT do host nem serviço DNS do sistema. Nenhum programa Windows é executado
pelo host Linux para compilar ou testar o convidado.

Referências da serialização: [UDP/RFC 768](https://www.rfc-editor.org/rfc/rfc768),
[IPv6/RFC 8200](https://www.rfc-editor.org/rfc/rfc8200) e
[DNS/RFC 1035](https://www.rfc-editor.org/rfc/rfc1035). O perfil implementado é
mais restrito que os protocolos completos.

## Perfis por VM

| Perfil | Tráfego permitido |
| --- | --- |
| `offline` (padrão) | Nenhum envio ou recebimento; recusa observável |
| `simulated` | DNS local em nó 253/UDP 53 e eco no nó 254/UDP 7; ambas as famílias |
| `lab` | DNS dos pares declarados e UDP somente aos nós/portas permitidos, com autorização recíproca |
| `proxy` | DNS estático da política e datagramas a endereços públicos/portas explícitos, exclusivamente pelo relay público configurado |

Nó N recebe IPv4 `198.18.0.N` e IPv6 `fd42:6761::N`, com 1 ≤ N ≤ 254.
A origem é conferida no host. DNS simulado publica `echo.guardian.test`;
laboratório usa `node-N.guardian.test`. Perguntas A/AAAA limitadas a uma questão,
sem compressão na consulta; nomes fora da política recebem NXDOMAIN. Não há
consulta ao DNS da LAN nem fallback externo.

No laboratório, as VMs trocam quadros por filas limitadas no catálogo, com
locks, arquivos comuns validados e publicação atômica. Catálogos e nomes de
laboratório distintos formam redes distintas. Só membros ativos recebem
envios; nó duplicado é recusado. A nova sessão limpa a fila da encarnação
anterior. Não há conexão de rede física nesse caminho. UDP não promete entrega
ou ordem; fila cheia, membro ausente e falha de armazenamento retornam erro.

Exemplo Linux, depois de compilar host e imagens:

```bash
./build-linux/Release/guardian-vm --root "$PWD/laboratorio" create demo \
  build-guest-linux/Release/guardian-kernel-network.gk network --disposable
./build-linux/Release/guardian-vm --root "$PWD/laboratorio" network demo simulated
./build-linux/Release/guardian-vm --root "$PWD/laboratorio" start demo
```

Para dois pares, criar `alpha` e `beta` com o mesmo contrato e configurar:

```bash
guardian-vm --root laboratorio network alpha lab --lab teste --node 1 --peer 2 --port 40000
guardian-vm --root laboratorio network beta lab --lab teste --node 2 --peer 1 --port 40000
guardian-vm --root laboratorio start alpha --paused
guardian-vm --root laboratorio start beta --paused
guardian-vm --root laboratorio resume alpha
guardian-vm --root laboratorio resume beta
```

O exemplo de convidado troca bytes nas duas famílias, consulta DNS e termina;
não é um servidor permanente. Os comandos `guardian-vm` são iguais no Windows,
usando `guardian-vm.exe`. Configuração de rede exige VM parada. Clones mantêm
a política; dois clones com o mesmo nó no mesmo laboratório não podem iniciar
simultaneamente. Alterar o nó de um clone exige boot novo; um snapshot antigo
não é aceito sob outra política.

## Relay externo opcional

A saída suportada é UDP por um relay próprio, não um proxy HTTP/SOCKS. Nada é
iniciado automaticamente. `--relay` exige IPv4 público numérico, `--relay-port`
exige porta explícita e cada `--allow` declara `nome,família,endereço,porta`.
IPv6 usa oito grupos hexadecimais explícitos na configuração, sem nomes ou
zones. Exemplos de forma da configuração:

```text
guardian-vm --root laboratorio network demo proxy --relay IP_PUBLICO_DO_RELAY --relay-port 9000 --allow exemplo.test,4,IP_PUBLICO_DO_DESTINO,7
```

Os marcadores acima devem ser substituídos por endereços do laboratório.
O operador pode usar `tools/guardian_udp_relay.py` numa máquina externa, com
`--bind`, `--port`, `--client` e `--allow` explícitos. O relay também confere
checksum, família e destino; usa socket UDP conectado ao destino permitido.
Não o execute no host analisador para simular saída externa.

O host só abre um socket UDP para o relay fixo. Confere novamente endereços e
sub-redes das interfaces locais antes de cada envio. Loopback, endereços
privados, link-local, metadados, CGNAT, documentação, multicast, broadcast,
IPv4-mapped IPv6, NAT64 e prefixos de transição ficam fora da política pública.
O filtro é conservador: também recusa todo `169/8`, `192/8`, `2001::/23`,
`2002::/16`, `3ffe::/16` e `3fff::/16`. Não aceita resolvedores ou URLs que
possam trocar o endereço após a validação.

O envelope do relay contém magic/version, nonce aleatório de 128 bits,
tamanho e pacote IP. A resposta deve vir do socket conectado, repetir o nonce
e corresponder exatamente à família, endereços e portas invertidos. Limites:
timeout de recepção de 100 ms no host e 75 ms para a consulta upstream do relay de
referência. O protocolo não oferece TLS, autenticação criptográfica do relay
ou confidencialidade; destina-se ao laboratório explicitamente autorizado.
A validação usa um relay controlado e sockets nativos locais para conferir o
protocolo, sem tráfego de teste enviado à internet. Um relay público real
operado pelo usuário não foi exercitado nesta máquina.

## Snapshots, captura e limites

Estado salvo: política canônica, sequência, contadores, fila de recepção,
relatório do convidado, decisões e captura. A restauração exige a mesma
política; callbacks e sockets do host não são serializados. Efeitos já
realizados em pares ou no relay não podem ser desfeitos por restaurar a VM.
Um novo worker de laboratório usa nova identidade de participação; candidatos restaurados no mesmo worker compartilham o endpoint existente.

Cada sessão/candidato do gerenciador guarda seus arquivos em
`<catálogo>/<VM>/network-history/<geração>/`: `policy.gnp`, `network.pcap` e
`network.json`. Restaurar cria outro registro e preserva os anteriores.
Excluir a VM remove seus arquivos, incluindo esse histórico. O PCAP usa
LINKTYPE_RAW (101), relógio lógico em microssegundos e MTU 1280; não representa
um horário real. O JSON contém hash FNV do PCAP/política para detectar pares
incompletos após falha de publicação, além de contagens e decisões. Publicação
dos dois arquivos é individualmente atômica; não constitui uma transação entre
arquivos. A captura registra a tentativa antes de permitir efeito externo.

Códigos de resultado: 0 aceito, 1 fila vazia, 2 negado, 3 malformado,
4 protocolo sem suporte, 5 fila cheia, 6 quota, 7 indisponível. Eventos indicam
direção (0 saída/1 entrada), tamanho, família e porta; recusas também aparecem.
`acceptedTx` conta tentativas aceitas na política, inclusive se o transporte
falhar depois; o erro posterior é outro evento. Espera sem pacote não gera
um falso evento de tráfego. Falha de captura interrompe a execução.

Padrões: 128 pacotes e 128 KiB por sessão; máximos configuráveis 256 pacotes e
256 KiB, somando envio e recepção. Até oito regras/pares/portas, oito quadros
na fila, 1024 operações/eventos, 512 KiB de PCAP, 16 redes por catálogo e 64
registros de sessão por VM. Quota esgotada retorna erro e não amplia a política.
As quotas de quatro VMs residentes/256 MiB do gerenciador continuam valendo.

## Retomar a aceitação

`python3 tools/validate_phase10.py` no Linux reconstrói todos os artefatos e
executa Debug, Release, ASan/UBSan, convidados reais e regressões. No Windows,
`python tools/validate_phase10.py` usa CMake/MSVC/WHP e exige os oráculos reais
Microsoft. Compilação cruzada é apenas preparação e não substitui esse gate.

A captura de uma nova sessão restaurada/clonada pode conter o prefixo guardado
no snapshot. Esse prefixo é histórico; restaurar não retransmite esses pacotes.
Os diretórios de geração e relatórios do host identificam cada execução.

Marco da aceitação completa: Debug passou nos 27 CTests reconstruídos, sem
falhas/skip, incluindo CPL3, snapshots, recuperação, quatro convidados de
laboratório e os perfis do gerenciador (355,28 s nesta máquina). Evidência em
`build-phase10/validation-20261018-172452-d770ed5f`. A regressão de boots e as
configurações Release/Sanitize ainda estavam em execução ao salvar este marco.

A primeira aceitação completa parou na regressão `Debug-complete-16-0`, com
status de autoteste `0xC7F4` (verificação 244 do exemplo de DLLs dinâmicas),
sem timeout. Os 27 CTests e 60 boots anteriores dessa execução passaram.
O erro reapareceu em um de 12 boots isolados com a mesma imagem. A evidência
original foi preservada como falha; não é contada como aceitação aprovada.
Diagnóstico em andamento para separar TLS/referências e retorno de yield.

Diagnóstico confirmado: ao reduzir o probe para um único worker, o retorno
zero de `SwitchToThread` produziu `0xC7FE`, distinguindo-o dos erros de TLS e
`FreeLibrary`. O teste antigo exigia indevidamente retorno não zero; a
[documentação Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-switchtothread)
permite zero quando não há outro thread pronto. A correção conserva os dois
workers concorrentes, os oito ciclos por worker e todas as verificações de
TLS/referências/descarregamento. Foi acrescentado `LoneYieldProof`, que exige
observar esse retorno legítimo enquanto o pai espera. A aceitação será refeita
com a fonte corrigida; os probes de diagnóstico ficam separados do pacote.

Com a correção, a nova execução
`build-phase10/validation-20261018-174152-6a21f7f6` aprovou os 27 CTests Debug
(363,69 s), os 66 boots de regressão Debug e os dois controles fatal/IST.
Release e Sanitize continuaram em execução. A fase 11 permanece sem início.


A revisão do relay reproduziu uma falha de recepção sob sinal assíncrono
(`build-phase10/eintr-before-fix`, verificação 337). O temporizador KVM usa
sinais para acordar o executor; a recepção UDP agora usa socket não bloqueante
e um prazo absoluto de 100 ms, retomando a espera após interrupções. O teste
positivo recebe a resposta atrasada após um sinal; o teste negativo injeta
sinais por 200 ms e exige erro próximo do prazo de 100 ms, sem reiniciá-lo.
A preparação passou com 345 verificações de rede e 20.000 entradas malformadas.
Windows: os 12 módulos atualizados compilaram em objetos COFF, sem execução.

A rodada `validation-20261018-174152-6a21f7f6` passou os 27 CTests de Debug e
Release e os 66 boots Debug; foi interrompida numa fronteira sem workers
residentes para acrescentar essa correção. É evidência intermediária, não a
aceitação final. A rodada final de aceitação é
`build-phase10/validation-20261018-180022-c09af2d0` e seus resultados aprovados estão no início deste documento.

## Pacote Linux entregue e verificado

Arquivo: `dist/guardianav-phase10-linux-x86_64-20261018.tar.gz` (cerca de 1,5 MiB, 252 arquivos).
SHA-256: `c78332e6eb46c76ac794ddf21a7ce6dc790b6750642549b51dee276273531d6c`.
O arquivo `.tar.gz.sha256` ao lado contém a mesma soma para conferência.

`build-phase10/validation-20261018-180022-c09af2d0/package-validation.json` registrou `passed`.
O pacote foi extraído numa pasta temporária e executado com outro diretório de
trabalho. Todos os hashes e os três executáveis ELF foram conferidos. O scanner
executou nativamente; os testes do gerenciador completaram oito boots de rede e
dois boots adicionais do console completo com persistência. A sequência do
volume sobreviveu ao reboot e a base permaneceu intacta. Sentinelas não
registraram uso de Wine/QEMU. Logs e relatórios do pacote ficam junto à aceitação.

O arquivo distribuído guarda a documentação capturada antes da verificação
final do próprio pacote; o hash final e o resultado dessa verificação estão
neste registro externo. As fontes permanecem idênticas às fontes aceitas.

Revisão recuperável: `changes.patch`, `changes.json`,
`documentation-sha256.json` e `windows-cross-precheck.json` na pasta da aceitação.
Os 329 arquivos anteriores continuam íntegros em
`snapshots/codex-before-phase10-20261018`; há 41 arquivos de texto adicionados ou
modificados com diferenças revisáveis. As rodadas/probes anteriores continuam
preservados e identificados como desenvolvimento, falha ou interrupção.

**Parada nesta fase:** implementação e aceitação Linux concluídas para o perfil
UDP documentado. Aceitação nativa Windows permanece pendente; executar
`python tools/validate_phase10.py` no Windows/MSVC/WHP para esse gate. A fase 11
não foi iniciada.

## Continuação autorizada após esta entrega

Em nova solicitação de 18/10/2026, o usuário autorizou iniciar a fase 11.
O estado e as evidências dessa etapa ficam em [PHASE-11.md](PHASE-11.md);
as menções anteriores a “não iniciada” descrevem o encerramento histórico
da fase 10. O gate Windows da fase 10 permanece pendente.
