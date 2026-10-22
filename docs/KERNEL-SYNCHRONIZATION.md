# Mutexes e semáforos do MiniNt — 16/10/2026

Esta etapa continua o núcleo existente em
`D:\Desktop Folders (DO NOT ERASE)\AntiVirus`, usando WHP diretamente.
Os objetos e as filas vivem no MiniNt; handles do convidado não se tornam
handles de sincronização do Windows host.

## Operações implementadas

| Operação | Comportamento |
|---|---|
| `NtCreateSemaphore` | Cria um semáforo sem nome, com contagens inicial e máxima válidas. |
| `NtReleaseSemaphore` | Acrescenta uma quantidade positiva, verifica o máximo antes de alterar o estado e pode retornar a contagem anterior. |
| `NtQuerySemaphore` | Retorna as contagens corrente e máxima em oito bytes. |
| `NtCreateMutant` | Cria um mutex sem nome, livre ou inicialmente pertencente à thread corrente. |
| `NtReleaseMutant` | Só a thread dona pode liberar; aquisições recursivas exigem liberações correspondentes. |
| `NtQueryMutant` | Retorna contagem, propriedade pela thread chamadora e estado de abandono, em oito bytes. |
| `NtWaitForSingleObject` | Espera também em semáforos e mutexes, com poll, timeout relativo/absoluto ou bloqueio conforme o escalonador existente. |

As criações validam a saída, atributos, direitos e contagens antes de publicar
um handle. Nomes, diretórios de objetos e descritores de segurança continuam
fora deste subconjunto. `NtOpenSemaphore` e `NtOpenMutant` retornam
`STATUS_NOT_SUPPORTED`; não fabricam handles para objetos com nome.

## Contagens e transferência

Um semáforo tem contagem entre zero e seu máximo positivo, limitado a `INT32_MAX`.
Uma espera satisfeita consome uma unidade. Uma liberação que excederia o máximo
retorna `STATUS_SEMAPHORE_LIMIT_EXCEEDED` sem alterar a contagem. Valores de
liberação nulos ou negativos são recusados.

Quando existem waiters, a liberação reserva uma unidade para cada espera
concluída antes de permitir que a thread chamadora continue. Portanto, ela não
pode consumir com um poll a unidade já destinada a outra thread. Uma thread
suspensa pode ter sua espera concluída e sua unidade reservada; sua suspensão
continua impedindo a execução. O semáforo não tem proprietário e o término de
uma thread não devolve automaticamente unidades já consumidas.

O mutex mantém ID da dona e profundidade de recursão nos metadados do host.
A contagem consultada é `1 - profundidade`: um quando livre, zero na primeira
aquisição e valores negativos nas seguintes. A recursão não pode fazer essa
contagem ultrapassar o limite inferior de um `LONG`. Neste subconjunto, o limite
é comunicado por `STATUS_MUTANT_LIMIT_EXCEEDED`, sem injetar uma exceção NT no
convidado.

Na última liberação, o mutex é atribuído à próxima thread elegível antes de
concluir sua espera. Outra thread não pode adquiri-lo entre a sinalização e a
retomada da dona escolhida. O modelo usa a ordem FIFO de espera para esses dois
objetos; isso é uma escolha desta implementação, não uma garantia de ordem das
APIs do Windows.

Os novos caminhos do escalonador expiram os prazos antes de escolher um waiter.
Uma espera já expirada retorna `STATUS_TIMEOUT` e não consome a liberação nem
recebe propriedade posteriormente. A política continua sendo cooperativa, em
uma única vCPU, sem preempção.

## Abandono e vida útil

Quando uma thread termina, todos os mutexes que ela possui são abandonados,
independentemente da profundidade de recursão. Isso vale para o retorno natural
da rotina e para `NtTerminateThread` sobre outra thread, inclusive uma dona que
recebeu propriedade enquanto estava suspensa.

O próximo adquirente recebe `STATUS_ABANDONED_WAIT_0` (`0x80`) e passa a ser o
dono. O estado de abandono é consumido nessa aquisição; uma aquisição recursiva
seguinte retorna sucesso normal. Se não houver waiter, o objeto permanece
livre e marcado como abandonado até a próxima aquisição. O resultado informa
a perda da dona anterior; não repara os dados que ela protegia.

Uma espera mantém referência ao objeto mesmo se seu handle for fechado por
outra thread. Mutexes possuídos também mantêm referência de propriedade depois
do fechamento do último handle, até liberação ou término da dona. Essa lista
tem limite próprio de 4096 objetos para que criar, adquirir e fechar handles
repetidamente não permita crescimento ilimitado. O término do processo limpa
essas referências sem tentar executar waiters de um processo já encerrado.

## Direitos e ABI

Os novos tipos usam a mesma tabela de handles, duplicação com direitos reduzidos
e flags de fechamento das etapas anteriores. Esperar exige `SYNCHRONIZE`;
consultar exige o bit de consulta `0x1`; liberar semáforo exige `0x2`.
Liberar mutex exige um handle do tipo correto e propriedade pela thread
chamadora, inclusive através de um alias sem direitos adicionais.

Contagens anteriores e comprimentos retornados ocupam exatamente quatro bytes.
Buffers, parâmetros de pilha e timeouts passam pelas verificações de limites
de RAM, permissões WHP e regiões virtuais. Saídas inválidas são recusadas antes
de consumir ou liberar recursos. A consulta básica rejeita sobreposição entre
o buffer de dados e seu comprimento retornado.

## Testes e compilação

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

A suíte passa de oito para dez testes:

- `microvm_synchronization` verifica limites, overflow, recursão, abandono,
  transferência após expiração, suspensão e máscaras de direitos. O tempo é
  fornecido à política; o limite de recursão é reduzido nesse teste para que
  sua borda possa ser exercitada sem bilhões de aquisições.
- `microvm_synchronization_guest` executa programas x64 gerados pelo teste em
  ring 3. Verifica contagens e canários de saída, handles reduzidos, dois waiters,
  reserva de unidades para thread suspensa, fechamento de handle durante espera,
  timeout real, liberação recursiva, rejeição de outra dona, transferência e
  abandono por término natural e forçado.

Os cenários de VM executam sequencialmente, cada um com sua instância WHP.
Os slots de resultados começam com sentinelas para detectar continuações que
não executaram. Uma espera cuja thread é terminada antes da retomada deve
preservar sua sentinela. Não são usadas amostras de malware.

## Limites restantes

Esperas múltiplas, alertas/APCs, timers de objetos, namespace de objetos com
nome, ACLs e processos adicionais continuam pendentes. Uma espera indefinida
em semáforo ainda sem escalonador ativo segue a recusa já documentada; quando
o escalonador está ativo, deadlock sem prazo interrompe a VM com diagnóstico.
Snapshots ainda não restauram o estado completo do MiniNt.

Esta etapa não transforma o MiniNt em um kernel NT completo ou inicializável
sem host. As limitações de contexto, loader e isolamento descritas em
[KERNEL-THREADS.md](KERNEL-THREADS.md) continuam aplicáveis.

## Resultados verificados

Validação concluída em 16/10/2026, com execução real dos testes WHP:

| Verificação | Release | Debug |
|---|---:|---:|
| Testes CTest aprovados | 10/10 | 10/10 |
| Testes ignorados | 0 | 0 |
| Exemplos existentes aprovados | 7/7 | 7/7 |

A compilação completa em Release gerou os 41 executáveis previstos. Os builds
finais em Release e Debug não emitiram avisos nem erros do compilador.
Os exemplos verificados foram `vmntdll`, `vmntfile`, `vmntsect`, `vmntimage`,
`vmntquery`, `vmntreg` e `vmwininit`.

Os arquivos anteriores estão preservados em
`snapshots/codex-before-sync-20261016-144033/`. As evidências ficam em
`build-codex/`:

- `sync-full-release-script.log` e `sync-debug-script.log`: builds e dez testes
  em cada configuração.
- `sync-release-tests-details.log` e `sync-debug-tests-details.log`: saída
  detalhada dos testes, incluindo os cenários novos.
- `sync-regressions.json`: saída e código de retorno dos 14 exemplos.
- `sync-whp-availability.json`: consulta de disponibilidade do hipervisor.
- `sync-change-manifest.json`: hashes SHA-256 dos arquivos alterados e novos,
  localização da cópia anterior e contagens de validação.

## Referências

- [Declarações NT e estruturas no phnt](https://github.com/winsiderss/phnt/blob/master/ntexapi.h)
- [Semáforos — Microsoft](https://learn.microsoft.com/en-us/windows/win32/sync/semaphore-objects)
- [Mutexes, recursão e abandono — Microsoft](https://learn.microsoft.com/en-us/windows/win32/sync/mutex-objects)
- [Direitos de objetos de sincronização — Microsoft](https://learn.microsoft.com/en-us/windows/win32/sync/synchronization-object-security-and-access-rights)
- [Espera e limite do mutex — Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject)
