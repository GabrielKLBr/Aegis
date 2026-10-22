# Fase 2 — CPU, exceções, interrupções e relógio no convidado

Data: 16/10/2026. Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
**Estado: fase 2 concluída. Fase 3 não iniciada.**

## Comportamento implementado

A imagem `guardian-kernel-cpu.gk`, versão 0.2.0, executa no WHP com uma vCPU.
O kernel instala GDT, IDT, TSS, pilhas de interrupção e IST de emergência.
Configura CR3, CR0.WP, EFER.SCE/NXE, STAR, LSTAR e FMASK dentro do convidado.
Confirma as tabelas instaladas com SGDT/SIDT/STR e a raiz de paginação com CR3.

Um programa de teste próprio executa em CPL3. A instrução SYSCALL entra no kernel
convidado, troca para uma pilha de kernel e despacha chamadas de eco, consulta
do relógio e término da execução. Chamadas desconhecidas retornam erro explícito.
Na consulta de relógio, RAX contém nanossegundos e RDX informa quantas
interrupções do timer foram atendidas no usuário. O teste espera pelo prazo e
por pelo menos dois atendimentos, inclusive quando o host atrasa um pulso.
O retorno usa IRETQ após validar RIP/RSP e filtrar flags de usuário. SYSRET e
compatibilidade com os números de syscall do Windows continuam fora desta entrega.

O estado preservado inclui registradores gerais, x87, SSE e MXCSR. O código de
entrada inicializa x87/MXCSR, salva/restaura o contexto com FXSAVE64/FXRSTOR64 e
mantém a direção de operações de memória sob controle. O teste verifica os
registradores não voláteis, XMM6, resultado x87 e DF através da chamada de sistema;
XMM6/x87 também atravessam o período de interrupções no usuário. AVX/XSAVE não
são habilitados nesta plataforma inicial.

## Destino das exceções

As exceções de CPU não são interceptadas para emulação no host neste modo:
o bitmap de saídas de exceção da partição é zero. Os stubs do convidado salvam
contexto, chamam o despachante C++ e retornam pela CPU. #UD e #PF conhecidos
do autoteste de kernel retomam em pontos explícitos. Um fault inesperado do
kernel registra o contexto e encerra com falha observável.

O teste de usuário exercita #UD, CLI sem privilégio, leitura de página de kernel,
guard page da pilha, tentativa de OUT e tentativa de INT em gate exclusivo do
kernel. Registra vetor, CPL, RIP, RSP, RFLAGS, CR2 e código de erro. A execução
de usuário que falhou termina e devolve o controle à rotina do kernel. Uma nova
entrada de usuário funciona em seguida. RSP não canônico em uma syscall é
recusado antes do retorno; o registro usa o evento de validação `0x100`.

Duas imagens exclusivas dos testes induzem um #UD inesperado de kernel e uma
falha dupla causada por pilha inválida durante a entrega de exceção. A falha
dupla usa a IST dedicada e envia relatório, em vez de desaparecer em um reset
silencioso. São imagens geradas pelo projeto, não arquivos externos.

## Temporizador e relógio

O dispositivo paravirtual usa a porta `0xEC`, vetor `0x40` e período solicitado
de 5 ms. O host mede tempo monotônico desde o início desta execução da VM e
publica uma página somente de leitura para o convidado. Cada publicação tem
versão, ID de boot e sequência. O convidado valida formato e monotonicidade.

Uma thread do host solicita saídas de serviço com WHvCancelRunVirtualProcessor.
Com a vCPU parada, o VMM atualiza o relógio e solicita a entrega da interrupção.
Se IF estiver limpo ou existir interrupt shadow, solicita notificação de janela
de interrupção e aguarda a CPU permitir a entrega. Só então configura o evento
pendente. Não sobrescreve outro evento. O handler real da IDT do convidado
incrementa os contadores e confirma o atendimento ao dispositivo.

Esse cancelamento de serviço retoma a execução; o cancelamento do watchdog
continua terminal e tem prioridade. A desativação do timer remove notificações
e seu evento pendente. Os testes verificam relógio avançando com IF desabilitado,
ausência de handlers nessa janela, atendimento depois de STI, interrupções em
CPL0 e CPL3, progresso no usuário e prazo concluído somente após o tempo pedido.
Retirar o gerador de pulsos no teste resulta em timeout explícito do watchdog.

O período de 5 ms é nominal. O escalonamento do host pode atrasar e combinar
vencimentos; não há garantia de tempo real. O relógio tem resolução de publicação
e pode repetir o mesmo valor entre atualizações. Nesta fase a espera de teste
usa PAUSE; não há scheduler, espera de thread nem suporte a idle/HLT com wakeup
por timer. APIC/PIC/PIT não são emulados por este dispositivo.

## Layout e fronteira

O contrato básico de boot 1.0 permanece intacto. Neste modo, RDX aponta para a
extensão CPU versionada em `0x105000`; RCX continua apontando para BootInfo em
`0x100000`. A extensão descreve clock, relatório, porta, vetor e período.

| GPA | Uso e proteção |
|---|---|
| `0x105000` | Extensão de boot da CPU, R |
| `0x106000` | Relógio paravirtual, R para o convidado |
| `0x107000` | Relatório numérico versionado, RW de supervisor |
| `0x600000` | Código próprio de usuário, RX após cópia e selagem |
| `0x610000` | Primeira página da pilha de usuário, não presente |
| `0x611000 .. 0x61FFFF` | Restante da pilha de usuário, RW/NX |
| `0x620000` | Dados de usuário, RW/NX |

O intervalo `0x600000 .. 0x7FFFFF` fica reservado no mapa de boot. O convidado
instala um mapa estático inicial com páginas de usuário apenas para código,
pilha e dados definidos. Páginas de kernel são supervisor. Código de usuário
passa de RW/NX para RX no WHP antes da execução; não recebe permissão simultânea
de escrita e execução. A extensão e o relógio têm proteção de escrita no WHP.

Ainda não há alocação de páginas, espaços de endereços por processo ou migração
do gerenciador MiniNt para o convidado. Esse trabalho continua na fase 3.
As estruturas de paginação desta fase são o mínimo para a fronteira CPL0/CPL3.

O relatório de CPU tem formato próprio, tamanho fixo e no máximo 12 registros
de fault. A confirmação de relatório usa a porta de CPU; Hello, diagnóstico e
StopResult continuam no canal básico. O host valida formato, versão, sessão,
campos reservados, limites e resultados antes de informar sucesso.

O loader mantém a recusa de arquivos com dados após as seções. Como o linker
MSVC produz slack zerado com estes objetos alinhados, o build canonicaliza
apenas preenchimento de zero em múltiplos de 512 bytes, limitado a 4096 bytes.
Esse passo ocorre sobre a saída compilada, antes da auditoria. Não é aplicado
a imagens recebidas pela ferramenta de execução. Há testes de recusa de trailer
com conteúdo, tamanho excessivo ou alinhamento inválido.

## Reproduzir

```powershell
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
# Inspeção, sem executar:
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-cpu.gk
# Executar somente o kernel próprio da fase 2:
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-cpu.gk --cpu --ram-mib 16 --report .\build-codex\manual-cpu-boot.json
```

`guardian_kernel` conserva a imagem básica da fase 1. `guardian_kernel_cpu`
contém a nova plataforma. `guardian_guest` compila e audita ambas. As variantes
`guardian_kernel_cpu_fatal` e `guardian_kernel_cpu_double_fault` pertencem à
validação e só são criadas com BUILD_TESTING habilitado.

## Validação

Os três novos testes são `guest_cpu_contract`, `guest_cpu` e
`guest_kernel_canonical_image`. A suíte passa a conter 19 testes. A matriz da
fase 2 executa 20 boots em 16 MiB e 20 em 64 MiB, por configuração, com watchdog
de 3 s. Cada boot verifica dez entradas de usuário, três conclusões normais,
seis faults de usuário contidos, dois faults de kernel recuperados, uma recusa
de retorno e o atendimento real do timer. Há também 16 cenários negativos por
configuração, incluindo falhas fatais e ausência do timer.

O kernel da fase 1 continua executando sua matriz de regressão. Seus novos
relatórios usam o prefixo `regression-phase1-`; os JSONs e logs históricos da
fase 1 são preservados. Os sete exemplos MiniNt e os outros testes continuam
como regressão do código existente no host.

### Resultados finais

| Verificação | Debug | Release |
|---|---:|---:|
| Compilação completa em C++20 | 48 executáveis + 4 imagens | 48 executáveis + 4 imagens |
| CTest | 19/19 | 19/19 |
| Testes ignorados | 0 | 0 |
| Referências MiniNt | 7/7 | 7/7 |
| Boots da fase 2: 20 em 16 MiB + 20 em 64 MiB | 40/40 | 40/40 |
| Boots de regressão da fase 1 | 40/40 | 40/40 |
| Cenários negativos externos da fase 2 | 16/16 | 16/16 |
| Falha fatal #UD e falha dupla: relatório e parada | Passou | Passou |
| Inicialização pela CLI com --cpu | Passou | Passou |
| Ajuda de av e guardian_boot | Passou | Passou |

Em cada um dos 80 boots positivos da fase 2, os 11 grupos de verificações
passaram (máscara `0x7FF`). Houve dez entradas de usuário, três conclusões normais,
seis faults de usuário contidos, dois faults conhecidos de kernel recuperados,
uma recusa de retorno inseguro e cinco atendimentos reais do timer. O relatório
guardou os nove registros de fault/validação e confirmou atendimento de IRQ em
CPL3. O kernel concluiu em CPL0 com status zero, sem interceptar exceções no host.

Tempo observado desde o início da preparação, nesta máquina:

| Configuração / RAM | Mínimo | Máximo |
|---|---:|---:|
| Debug / 16 MiB | 35,615 ms | 43,322 ms |
| Debug / 64 MiB | 37,064 ms | 44,651 ms |
| Release / 16 MiB | 35,401 ms | 38,547 ms |
| Release / 64 MiB | 35,458 ms | 41,581 ms |

As saídas de vCPU variaram de 45 a 49 em Debug e de 46 a 48 em Release.
A variação inclui saídas de serviço e janelas de interrupção. A suíte completa
levou 6,51 s em Debug e 3,21 s em Release. São observações, não metas de
desempenho do produto. Os JSONs também registram RAM, memória privada do
processo host antes da destruição da VM, compilador e estado de CPU.

Ambiente validado: Windows 11 Pro 10.0.26300 x64, MSVC 19.44, SDK 10.0.26100.0,
CMake 4.3.0-rc2 e Python 3.12. Outras versões de Windows exigem validação própria.
O build completo mantém avisos C4244 dos alvos legados. Os novos alvos C++
compilam com `/W4 /WX`; o aviso LNK4281 da base fixa de 2 MiB continua deliberado,
como na fase 1. Não há erros nos builds finais.

Um build Debug independente em `build-codex-phase2-clean`, sem BUILD_TESTING,
também compilou e auditou as duas imagens de produção. A inspeção dos projetos
gerados verificou as opções de 16 combinações de imagem/configuração, sem
runtime do host no convidado, preservando os checks do host Debug. A inspeção PE
confirmou ausência de imports Windows, TLS, relocations e seções W+X. A imagem
CPU ocupa 159744 bytes em memória, em ambas as configurações.

Evidências em `build-codex/`:

- `phase2-full-debug-script.log`, `phase2-full-release-script.log` e os logs
  correspondentes `phase2-*-tests-details.log`.
- `phase2-boots-Debug.json`, `phase2-boots-Release.json` e suas versões
  `.negative.json`.
- `regression-phase1-boots-Debug.json` e `regression-phase1-boots-Release.json`.
- `phase2-regressions.json`, `phase2-host-smoke.json` e `phase2-cli-boots.json`.
- `phase2-debug-pe-audit.log`, `phase2-release-pe-audit.log` e
  `phase2-build-boundaries.json`.
- `phase2-clean-configure.log` e `phase2-clean-debug-final-build.log`.
- `phase2-change-manifest.json`: hashes, backup, ambiente e resumo verificado.
  Critérios em `config/phase2-acceptance.json`.

Os arquivos anteriores estão em `snapshots/codex-before-phase2-20261016-162550/`.
Os hashes das evidências históricas da fase 1 permanecem iguais ao manifesto
daquela entrega. Tentativas intermediárias foram preservadas em logs separados.
Nenhuma configuração de boot ou segurança do Windows foi alterada.

## Limites e parada

A execução de usuário demonstrada é um programa em assembly criado pelo
projeto, copiado para uma página controlada. Não é um carregador de aplicações
Windows nem um gerenciador de processos. O kernel continua com uma vCPU, mapa
estático, pilhas de tamanho fixo e sem preempção de threads. Drivers, arquivos,
registro, APIs NT/Win32 e interface WinUI 3 permanecem nas fases correspondentes.
Não há boot físico por UEFI/BIOS nesta entrega.

Ao concluir a fase 2, encerrar o trabalho. A fase 3 requer nova solicitação.

## Referências técnicas

- [Manuais Intel de arquitetura e programação de sistema](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Execução e estado de processadores WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/hypervisor-platform)
- [Saída para injeção de eventos](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvcancelrunvirtualprocessor)
- [Janela de entrega de interrupção](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/interruptwindow)
- [Alteração de registradores do processador virtual](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvsetvirtualprocessorregisters)
