# Threads e esperas do MiniNt — 16/10/2026

Esta etapa continua a implementação existente em
`D:\Desktop Folders (DO NOT ERASE)\AntiVirus`, com WHP direto, sem QEMU.
O núcleo ainda é implementado no host: não inicializa sozinho em hardware.

A etapa seguinte, [sincronização](KERNEL-SYNCHRONIZATION.md), acrescenta mutexes
e semáforos às esperas simples. Os resultados abaixo preservam a validação
original desta etapa; a suíte atual contém dez testes.

## Escalonamento implementado

O monitor existente mantém uma única vCPU. `GuestScheduler.h` implementa uma
fila cooperativa: uma thread executa até esperar, ceder a execução, suspender-se
ou terminar. Outra thread pronta assume a vCPU e, quando a anterior volta a
executar, retoma a instrução seguinte ao syscall que a suspendeu.

Os estados são pronto, executando, esperando e terminado. A contagem de
suspensão é independente do estado de espera. A fila usa a ordem de entrada,
não a ordem numérica dos IDs. A política recebe o tempo como argumento, o que
permite testar corridas entre sinalização e timeout sem usar pausas nos testes
de unidade. O limite é de 64 registros de threads por instância, incluindo a
thread inicial; registros terminados sem handles ou referências são recolhidos
antes de uma nova criação.

`MicroVmMonitor` salva e restaura registradores gerais, segmentos, registradores
de controle e debug, XMM, dados x87/MMX e controles x87/MXCSR. O caminho de syscall
reconhece quando o escalonador restaurou outro RIP e evita avançá-lo novamente.
A entrada de uma nova thread começa com registradores gerais e ponto flutuante
limpos, argumento em RCX e pilha conforme o ABI x64.

Esta versão exige o modo de usuário existente, com interrupções desabilitadas
e OSXSAVE desabilitado. Estado AVX/XSAVE não é silenciosamente descartado: uma
configuração que o exigiria é recusada na ativação. Há uma única vCPU; não existe
execução simultânea de duas threads nem preempção por temporizador.

## Syscalls e esperas

| Operação | Comportamento |
|---|---|
| `NtCreateThreadEx` | Cria uma thread do processo corrente, pronta ou inicialmente suspensa, com handle tipado e direitos específicos. |
| `NtSuspendThread` / `NtResumeThread` | Mantêm uma contagem de suspensão, validam direitos e escrevem a contagem anterior em exatamente quatro bytes. |
| `NtYieldExecution` | Cede a vCPU a outra thread pronta; informa `STATUS_NO_YIELD_PERFORMED` se não houver outra. |
| `NtWaitForSingleObject` sobre evento | Bloqueia a thread ativa até sinalização ou timeout, sem impedir outra thread de executar. |
| `NtWaitForSingleObject` sobre thread | Retoma quando a thread alvo termina; uma thread já terminada satisfaz a espera imediatamente. |
| `NtDelayExecution` | Suspende até o prazo, sem fabricar sucesso antecipado; intervalo zero pode ceder a execução. |
| `NtTerminateThread` | Termina a thread corrente ou outra thread autorizada, libera seus recursos próprios e acorda quem espera por ela. |
| `NtQueryInformationThread` | Atende `ThreadBasicInformation` x64 de 48 bytes: status de saída, TEB, IDs, afinidade e prioridades básicas do modelo. |

Eventos auto-reset acordam um único waiter na ordem de espera; eventos
manual-reset acordam todos. A verificação de prazos ocorre antes de sinalizar,
para que um sinal posterior não transforme uma espera já expirada em sucesso.
A suspensão não perde a conclusão: uma espera pode ser satisfeita enquanto a
thread continua impedida de executar por sua contagem de suspensão.

Timeouts relativos negativos e absolutos positivos seguem a representação NT
em unidades de 100 ns. Prazos internos usam relógio monotônico; valores absolutos
são convertidos a partir do FILETIME UTC do host no momento da chamada. Quando
ninguém está pronto, o host aguarda em intervalos de aproximadamente 1 ms e
verifica prazos e cancelamento. Ainda não há um relógio único sincronizando
esses valores com todos os campos sintéticos de `KUSER_SHARED_DATA`.

Se todas as threads estão impedidas de executar e não há nenhum prazo pendente,
o monitor encerra a execução com diagnóstico de deadlock. Não devolve sucesso
ao waiter nem entra em espera infinita no host. Cancelar durante uma espera sem
vCPU em execução também é reconhecido. Depois de uma falha desse tipo, a VM
deve ser recriada; retomar contextos com estado incerto é recusado.

## Pilhas, TEBs e vida útil

A primeira criação ativa o escalonador e registra a thread que já executava.
Uma espera com prazo ou um delay também pode ativá-lo. Uma espera indefinida
sem escalonador ativo continua retornando `STATUS_NOT_SUPPORTED`, pois ainda
não existe outra thread ou fonte externa de conclusão nesse caminho.

Cada thread nova recebe uma reserva de pilha de 64 KiB, com a página inferior
inacessível, e um TEB mínimo de 8 KiB. O TEB contém self, limites de pilha,
client ID, referência ao PEB existente e base de desalocação. Os IDs ficam nos
metadados do host; a escrita do convidado em seu TEB não altera a identidade
mantida pelo escalonador.

Um stub RX do processo recebe o retorno da rotina e chama
`NtTerminateThread(NtCurrentThread(), status)`. O término libera a pilha e o TEB
próprios mesmo enquanto um handle da thread continua aberto para consulta.
O TEB/pilha iniciais, emprestados do ambiente já existente, não são liberados
como se pertencessem ao novo alocador. O stub RX permanece durante a vida da VM.

Os syscalls comuns de memória recusam liberar ou alterar as regiões que o núcleo
mantém para pilhas, TEBs e o stub de retorno. A limpeza usa o caminho interno do
gerenciador de memória. Isso evita liberar posteriormente um endereço já
reutilizado por outra alocação. O convidado continua podendo ler e escrever as
partes RW da própria memória, conforme as permissões WHP.

Threads integram a tabela de handles da etapa anterior. Consultar exige
`THREAD_QUERY_INFORMATION`, suspender/retomar exige `THREAD_SUSPEND_RESUME`,
terminar exige `THREAD_TERMINATE`, e esperar exige `SYNCHRONIZE`. O pseudo-handle
`NtCurrentThread()` é aceito nos syscalls específicos de thread; a duplicação
desse pseudo-handle ainda não foi implementada.

## Subconjunto e limites

`NtCreateThreadEx` aceita o processo corrente e os flags zero ou
`CREATE_SUSPENDED`. `ZeroBits`, tamanhos personalizados de pilha, lista de
atributos e outros flags são recusados. A rotina de entrada é chamada diretamente
pelo ambiente próprio: não executa `RtlUserThreadStart`, inicialização TLS do
loader nem callbacks de DLL. O TEB mínimo não constitui toda a inicialização de
uma thread Windows.

Só há espera simples em eventos e threads. Alertas/APCs, espera múltipla,
mutexes, semáforos, timers de objetos, esperas em arquivos e conclusão assíncrona
de I/O continuam pendentes. Os caminhos antigos de thread-alert e alguns locks
do loader ainda têm respostas provisórias. Não há prioridades dinâmicas nem
preempção: um loop que não faz syscalls continua sujeito ao watchdog existente.

Não há processo adicional nem SMP. `SnapshotManager` ainda precisa capturar os
contextos, filas, referências e prazos junto de RAM e permissões; restaurar só
RAM/registradores não restaura o `MiniNt`. O modelo A (`NtKernel`) continua
separado. Compatibilidade integral com Windows e isolamento de malware validado
permanecem fora do que os testes desta etapa demonstram.

## Compilação e testes

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

Os builds ficam em `build-codex`. O script agora executa oito testes: os seis
anteriores e os seguintes:

- `microvm_scheduler`: fila, suspensão, auto/manual-reset, prazos, corridas entre
  timeout e sinal, delays, término e capacidade, com tempo simulado.
- `microvm_threads_guest`: execução real em ring 3. Verifica handoff por evento,
  join, criação suspensa, contagens, direitos, retorno da rotina, consultas de
  thread, proteção e limpeza das regiões internas. Marcadores de pilha, GPR,
  XMM, valor e controle x87, MXCSR e GS/TEB são conferidos após trocas reais.
  Cenários adicionais verificam timeout relativo, prazo absoluto expirado,
  delay, término de outra thread, deadlock e cancelamento durante idle.

Os programas são gerados no próprio teste; não são carregadas amostras externas.
Os testes WHP retornam 77 quando o hipervisor não está disponível, caso em que o
CTest registra a execução como ignorada.

Os dez arquivos anteriores modificados nesta etapa estão preservados em
`snapshots/codex-before-threads-20261016-003531/`.

## Resultados verificados

Validação concluída em 16/10/2026, com o hipervisor WHP presente:

| Verificação | Release | Debug |
|---|---:|---:|
| Testes CTest aprovados | 8/8 | 8/8 |
| Testes ignorados | 0 | 0 |
| Exemplos existentes aprovados | 7/7 | 7/7 |

Os exemplos verificados foram `vmntdll`, `vmntfile`, `vmntsect`, `vmntimage`,
`vmntquery`, `vmntreg` e `vmwininit`. O build completo em Release também
terminou com sucesso, com 39 executáveis. A compilação Debug desta etapa não
emitiu avisos nem erros do compilador.

Evidências mantidas em `build-codex/`:

- `threads-release-tests.log`: resultado dos oito testes em Release.
- `threads-debug-script.log` e `threads-debug-tests-details.log`: compilação,
  resultado e saída detalhada dos testes em Debug.
- `threads-regressions.json`: códigos de saída e saída capturada dos 14 exemplos.
- `threads-full-release-build.log`: compilação completa em Release.
- `threads-whp-availability.json`: consulta de disponibilidade do hipervisor.
- `threads-change-manifest.json`: arquivos alterados/criados, hashes SHA-256,
  localização da cópia anterior e contagens de validação.

## Referências de ABI

- [Declarações de criação, término e informação de threads no phnt](https://github.com/winsiderss/phnt/blob/master/ntpsapi.h)
- [NtWaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntwaitforsingleobject)
- [WHvGetVirtualProcessorRegisters](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetvirtualprocessorregisters)
