# Fase 1 — Primeiro boot do kernel próprio

Data: 16/10/2026. Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
**Estado: fase 1 concluída. Fase 2 não iniciada.**

## Entrega

O artefato `build-codex/guest/<Config>/guardian-kernel.gk` é uma imagem PE32+
x64 própria, carregada na RAM de uma partição WHP. O VMM existente prepara
long mode e entrega a execução ao código dessa imagem em CPL0. A imagem não
é aberta pelo carregador de processos do Windows. O MiniNt existente continua
como referência de regressão no host.

O ponto de entrada recebe `BootInfo` em RCX, valida cabeçalho, mapa e dispositivos,
verifica dados inicializados, área zerada, pilha, chamada de função e leitura/
escrita de globais. Envia Hello, diagnóstico e StopResult pelo contrato 1.0.
O host só informa sucesso com CPL0, sequência válida, autoteste PASS e status
zero. HLT isolado não é sucesso. O console identifica o kernel como 0.1.0.

Fontes principais:

- `guest/Kernel.cpp` e `guest/Memory.cpp`: entrada, autoteste, transporte e
  primitivas locais de memória, sem CRT, imports, RTTI ou exceções C++.
- `src/microvm/boot/KernelImage.*`: leitura e validação da imagem PE.
- `src/microvm/boot/KernelBoot.*`: layout, permissões, boot, watchdog e relatório.
- `src/microvm/boot/BootChannel.*`: estado e limites do protocolo.
- `src/microvm/boot/BootMain.cpp`: ferramenta `guardian_boot`.

## Formato e layout inicial

O carregador aceita somente PE32+ x64 Native de base fixa `0x200000`, com até
1 MiB de imagem, 4 MiB de arquivo e 16 seções. Valida offsets, tamanhos,
alinhamentos, sobreposições e entrada dentro de código executável. Recusa DLL,
imports, TLS, relocations, diretórios não suportados e dados extras no fim.
Só metadados opcionais de unwind e debug são permitidos; não são executados
nem usados para tratar exceções nesta fase. Seções são copiadas e o restante
da imagem, incluindo BSS, é zerado.

| GPA | Uso |
|---|---|
| `0 .. 0xFFFFF` | Bootstrap reservado, incluindo paginação do VMM |
| `0x100000` | BootInfo, somente leitura |
| `0x101000` | Mapa de memória, somente leitura |
| `0x102000` | Dispositivos, somente leitura |
| `0x103000` | Buffer de mensagens, 4096 bytes, leitura/escrita sem execução |
| `0x104000` | Armadilha #UD para retorno inesperado da entrada, RX |
| `0x200000` | Imagem; cabeçalhos R, código RX, dados RW sem execução |
| `0x400000 .. 0x40FFFF` | Pilha de 64 KiB, primeira página sem mapeamento |

A entrada usa RSP no topo menos `0x28`: alinhamento x64, endereço de retorno
e shadow space. Interrupções ficam desabilitadas. O VMM fornece o mapeamento
inicial de endereços; o kernel ainda não gerencia páginas por conta própria.
As proteções de execução e escrita são aplicadas pelo WHP ao convidado.

O linker pode emitir LNK4281 para a base abaixo de 4 GiB. Essa base é deliberada:
a imagem tem endereço fixo dentro do mapa inicial de 1 GiB da microVM e não é
um processo Windows com ASLR. Imports ou dependências de runtime não são
resolvidos pelo host. Os novos alvos C++ compilam com `/W4 /WX`.

## Fronteira e falhas

O transporte aceita apenas OUT escalar de um byte: `0xE9` para console e
`0xEA` para sinalizar o buffer fixo. Valor de OUT não fornece ponteiro ao host.
IN, operações largas, string/REP e portas desconhecidas são recusados.
O host copia o pacote enquanto a vCPU está parada e limita cabeçalho e payload
ao buffer. Versão, ID do boot e sequência são obrigatórios. Hello é único;
diagnóstico e parada dependem dele. Erros são permanentes na sessão.

Limites por sessão: 128 mensagens, 16 KiB acumulados de diagnóstico, 1024 bytes
por diagnóstico, 4096 bytes de console e 10000 saídas de serviço. Texto é ASCII
limitado. A parada encerra Run antes de executar a próxima instrução convidada.
O contador novo TotalExitCount inclui também saídas terminais; ExitCount legado
mantém a semântica anterior para os exemplos existentes.

O watchdog cancela a vCPU após três segundos de execução. Exceções interceptáveis
são consultadas via WHvGetCapability; #UD/#GP/#PF são exigidas. Não são solicitados
bits não suportados. O relatório registra falha de protocolo, exceção e RIP,
falha de memória e GPA, cancelamento, status do kernel e console limitado.
Ausência de WHP retorna 77; os testes a marcam como ignorada, não como boot aprovado.

## Reproduzir

```powershell
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
# Inspeção sem iniciar VM:
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel.gk
# Boot de 16 MiB com relatório explícito:
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel.gk --ram-mib 16 --repeat 20 --report .\build-codex\manual-boots.json
```

A ferramenta da fase 1 permite 16 ou 64 MiB, uma vCPU e até 100 boots sequenciais.
O grupo `guardian_guest` audita contrato e imagem final. O grupo
`guardian_reference` inclui a ferramenta, os novos testes e as referências
existentes. O script usa a instalação local do SDK; não altera configurações
de inicialização ou segurança do Windows.

## Validação

A suíte acrescenta três testes aos 13 existentes:

- `guest_kernel_image`: imagem real, conteúdo mapeado/BSS, limites de escrita
  e casos de imagem truncada, corrompida ou com dependências não suportadas.
- `guest_boot_protocol`: ordem, versão, sessão, replay, capacidades, texto,
  tamanhos, campos reservados, limites acumulados e estado terminal.
- `guest_kernel_boot`: 20 boots em 16 MiB e 20 em 64 MiB por configuração,
  com CPL0, status zero, diagnóstico e tempo total observado abaixo de 3 s.
  Exercita também 22 falhas/injeções controladas e seis recusas de admissão
  antes de alocar RAM. Só executa o kernel e programas minúsculos deste teste.

Os casos incluem parâmetros e tabelas inválidos, retorno inesperado, #UD,
HLT sem resultado, loop infinito, I/O inválido, pacote excessivo, escrita em
código/cabeçalho, execução em dados, guard page, autoteste com falha e prova
de que RequestStop impede a instrução seguinte.

Os JSONs registram configuração, compilador, workload, RAM, tempo desde o início
da preparação, total de saídas de vCPU, memória privada do processo host medida
antes da destruição da VM, CPL, máscara de exceções e resultado. Memória privada
inclui o processo inteiro, não apenas a VM. Os números são observações desta
máquina, não promessas de desempenho ou compatibilidade.

### Resultados finais

| Verificação | Debug | Release |
|---|---:|---:|
| Compilação completa em C++20 | 46 executáveis + kernel | 46 executáveis + kernel |
| CTest | 16/16 | 16/16 |
| Testes ignorados | 0 | 0 |
| Exemplos de referência | 7/7 | 7/7 |
| Boots reais: 20 em 16 MiB + 20 em 64 MiB | 40/40 | 40/40 |
| Cenários negativos no convidado | 22/22 | 22/22 |
| Admissões inválidas recusadas antes de alocar VM | 6/6 | 6/6 |
| Imagens inválidas recusadas | 56/56 | 56/56 |
| Imports Windows / TLS / relocations | 0 / 0 / 0 | 0 / 0 / 0 |
| Tamanho da imagem em memória | 24576 bytes | 20480 bytes |
| CLI av e guardian_boot: ajuda | Passou | Passou |

São **80 boots positivos** na matriz final. Todos observaram CPL0, status zero,
autoteste PASS e 25 saídas de vCPU. Tempo observado, incluindo preparação:

| Configuração / RAM | Mínimo | Máximo |
|---|---:|---:|
| Debug / 16 MiB | 1,548 ms | 4,269 ms |
| Debug / 64 MiB | 3,005 ms | 7,586 ms |
| Release / 16 MiB | 1,449 ms | 2,575 ms |
| Release / 64 MiB | 2,743 ms | 4,170 ms |

A suíte completa levou 4,07 s em Debug e 3,26 s em Release nesta execução.
Ambiente: Windows 11 Pro 10.0.26300 x64, WHP disponível, MSVC 19.44, SDK
10.0.26100.0. Outras versões de Windows ainda precisam de validação própria.
Avisos C4244 dos alvos legados continuam registrados nos logs. Os novos
alvos C++ passaram com avisos tratados como erros; o aviso de linker LNK4281
da base fixa está explicado acima.

Uma configuração independente em `build-codex-phase1-clean`, com testes
desabilitados, também compilou e auditou contrato e imagem Debug. A inspeção
dos projetos gerados confirmou a separação de opções do host e do convidado.
O dumpbin do MSVC inspecionou ambas as imagens sem dependências importadas.

Evidências preservadas em `build-codex/`:

- `phase1-full-debug-script.log` e `phase1-full-release-script.log`.
- `phase1-debug-tests-details.log` e `phase1-release-tests-details.log`.
- `phase1-boots-Debug.json` e `phase1-boots-Release.json`; os arquivos
  correspondentes `.negative.json` registram os cenários negativos.
- `phase1-regressions.json` e `phase1-host-smoke.json`.
- `phase1-debug-pe-audit.log`, `phase1-release-pe-audit.log` e
  `phase1-build-boundaries.json`.
- `phase1-clean-configure.log` e `phase1-clean-debug-build.log`.
- `phase1-change-manifest.json`: hashes das fontes, backup, ambiente e resumo
  verificado dos resultados. Critérios em `config/phase1-acceptance.json`.

Os sete arquivos anteriores alterados foram preservados em
`snapshots/codex-before-phase1-20261016-155528/`. Logs das tentativas iniciais
também foram mantidos. Nenhuma configuração de segurança do Windows foi alterada.

## Limites da entrega

Este é o primeiro kernel próprio inicializável dentro do sandbox. Ainda não há
IDT/ISR do convidado, temporizador com interrupção, syscalls, processos, memória
gerenciada, filesystem, drivers Windows ou carregamento de aplicações Windows
nesse novo kernel. Essas tarefas continuam nas fases seguintes do roadmap.
Também não há inicialização física por BIOS/UEFI nesta entrega.

A execução termina ao concluir a fase 1. A fase 2 só será iniciada após nova
solicitação, conforme a regra de parada acordada.

## Referências

- [Formato PE — Microsoft](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [Entrada explícita /ENTRY](https://learn.microsoft.com/en-us/cpp/build/reference/entry-entry-point-symbol)
- [Sem bibliotecas padrão /NODEFAULTLIB](https://learn.microsoft.com/en-us/cpp/build/reference/nodefaultlib-ignore-libraries)
- [Capacidades do WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetcapability)
