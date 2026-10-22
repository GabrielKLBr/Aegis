# Continuação sobre a base GuardianAV — 15/10/2026

A pasta `D:\Desktop Folders (DO NOT ERASE)\AntiVirus` é a base desta continuação.
O código existente foi aproveitado; nenhum diretório anterior de compilação foi
apagado ou substituído. As novas compilações ficam em `build-codex`.

**Atualização de 16/10/2026:** a etapa seguinte implementou estado de memória
virtual e permissões no WHP, descritos em [VIRTUAL-MEMORY.md](VIRTUAL-MEMORY.md).
Os resultados e limites do heap abaixo registram a etapa de 15/10; a documentação
nova detalha os quatro testes e o comportamento atual dos syscalls de memória.
Uma nova etapa no mesmo dia acrescentou a tabela única de handles e referências
de objetos; consulte [KERNEL-OBJECTS.md](KERNEL-OBJECTS.md) para o estado atual,
com seis testes de regressão do núcleo.

## Arquitetura aproveitada

O `MicroVmMonitor` já usa diretamente Windows Hypervisor Platform, sem QEMU.
O código do convidado executa na partition WHP. `MiniNt` atende os syscalls NT
interceptados, enquanto `PeLoader`, `WinLoader`, `VirtualFs` e os demais módulos
existentes fornecem o ambiente de execução. Essa personalidade NT é implementada
no host: ainda não é um kernel NT completo inicializável em hardware independente.

Nesta etapa, a prioridade escolhida foi **o núcleo básico antes de ampliar a
compatibilidade**. A interface Direct2D e os demais módulos existentes foram
preservados. A ISO Windows não é necessária para estes testes.

## Gerenciamento de heap implementado

`src/microvm/host/GuestHeap.h` substitui o bump allocator dos sentinelas
`Rtl*Heap` em `MiniNt`:

- Alinhamento de 16 bytes, arredondamento sem overflow e arena validada contra
  a RAM real do convidado. A primeira página fica reservada para o handle.
- Blocos livres são reutilizados; intervalos livres vizinhos são unidos.
- Liberação duplicada, ponteiro interior e handle de outro heap são recusados.
- Realocação preserva os dados. Falha por falta de memória ou tamanho inválido
  mantém o bloco original válido. Há crescimento no mesmo lugar, redução e
  movimentação quando necessária e permitida.
- `HEAP_ZERO_MEMORY` zera a região nova; `HEAP_REALLOC_IN_PLACE_ONLY` impede
  movimentação. Blocos de tamanho zero têm endereços distintos até serem liberados.
- `RtlSizeHeap` devolve o tamanho solicitado de blocos válidos e `SIZE_T(-1)`
  para blocos inválidos. Sem arena configurada não é fabricado um handle.
- Os metadados ficam no host e são limitados a 16.384 segmentos. O estado é
  vinculado a uma única RAM de convidado até sua reconfiguração explícita.

`GuestMemory.h` fornece cópia, leitura, escrita e zeragem com validação por
subtração antes de formar ponteiros. Os helpers escalares e a leitura de
`UNICODE_STRING` de `MiniNt` passaram a usar essa validação. Isso cobre esses
acessos, sem afirmar que todos os parsers e syscalls antigos foram auditados.

O caminho continua usando os patches já existentes: funções das DLLs reais
chegam ao alocador próprio de `MiniNt`. O teste `vmwininit` agora descreve isso
explicitamente, sem anunciar que o heap nativo do Windows foi inicializado.
Seu sinal de encerramento do watchdog também passou a ser atômico.

## Compilar e testar

Visual Studio 2022 com C++ x64, Windows SDK e CMake. No PowerShell comum:

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
# Compilar também todos os executáveis já existentes:
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

O script compila ferramentas e executa os testes de memória. Para executar
separadamente os exemplos antigos usados como regressão:

```powershell
.\build-codex\Release\vmpoc.exe
.\build-codex\Release\vmntdll.exe
.\build-codex\Release\vmwininit.exe
```

`microvm_memory` valida limites, overflow, reutilização, união de intervalos,
preservação de dados, crescimento e 2.500 operações combinadas contra blocos
vivos. `microvm_heap_guest` executa programas pequenos gerados pelo próprio teste
em **ring 3**, usando `SYSCALL`/`#UD` e o dispatcher existente de `MiniNt`; o
convidado escreve e lê um marcador no bloco antes e depois da realocação.
Não são carregadas amostras externas nesses testes.

O teste de convidado retorna código 77 se o hipervisor não estiver disponível;
o CTest o mostra como ignorado, não como evidência de execução bem-sucedida.
Nenhum recurso ou configuração de segurança do Windows é alterado.

## Validação executada nesta máquina

MSVC 19.44, SDK 10.0.26100.0, Windows x64 com WHP disponível:

| Verificação | Debug | Release |
|---|---|---|
| `microvm_memory` | Passou | Passou |
| `microvm_heap_guest` (execução real em ring 3) | Passou | Passou |
| Exemplo existente `vmntdll` | Passou | Passou |
| Exemplo existente `vmwininit` | Passou | Passou |
| Compilação de todos os alvos do projeto | Não executada | Passou, 33 executáveis gerados |

`vmpoc` também executou seu programa de 13 bytes com as três saídas de porta e
HLT esperados antes das alterações. O script `Build-Kernel.ps1` foi executado
com sucesso em Release. Os testes WHP desta máquina **executaram**; não foram
ignorados por falta de virtualização.

Evidências locais: `build-codex/release-tests.log`, `debug-tests.log`,
`existing-regressions.json`, `full-release-build.log` e `build-kernel-script.log`.
O build completo mantém avisos de conversão de tipos em código anterior do
modelo A; os novos alvos de memória foram compilados em Debug sem avisos.

## Limites e sequência do núcleo

O heap desta etapa tem uma única arena configurada e uma vCPU. Múltiplos heaps,
destruição de heaps, exceções de heap e sincronização entre vCPUs ainda precisam
ser implementados. As flags suportadas são `HEAP_NO_SERIALIZE`,
`HEAP_ZERO_MEMORY` e, na realocação, `HEAP_REALLOC_IN_PLACE_ONLY`; outras são
recusadas. `RtlCreateHeap` expõe a arena configurada, sem criar arenas independentes.

O heap de 15/10, isoladamente, não implementava reserva/commit/decommit ou
permissões de páginas. Esses recursos foram acrescentados na etapa seguinte de
[memória virtual](VIRTUAL-MEMORY.md), incluindo `NtFreeVirtualMemory` e
`NtProtectVirtualMemory` com efeitos verificáveis. Algumas esperas antigas
continuam provisórias; handles/objetos, processos/threads e sincronização ainda
precisam evoluir. O heap de `NtKernel` (modelo A) também permanece separado;
os novos gerenciadores foram integrados ao `MiniNt` (modelo B).

Os resultados históricos de chamar exports com argumentos nulos medem retorno
ou falha dessas chamadas. Eles não equivalem a uma porcentagem de compatibilidade
semântica com o Windows. Não foi estabelecida compatibilidade integral de NT,
Win32, drivers ou execução segura de malware.

Há cópias dos seis arquivos existentes modificados antes desta etapa em
`snapshots/codex-before-memory-20261015-231909/`.

Referências das semânticas usadas:

- [HeapReAlloc: preservação, falha e flags](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaprealloc)
- [HeapFree: liberação de blocos](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapfree)
- [HeapSize: retorno de tamanho](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsize)
- [Windows Hypervisor Platform](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/hypervisor-platform)
