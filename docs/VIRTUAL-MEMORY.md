# Memória virtual do MiniNt — 16/10/2026

Esta etapa continua a implementação existente em
`D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. Usa diretamente o WHP, sem QEMU,
sem inicializar a ISO Windows e sem modificar a instalação do Windows do host.
O gerenciador é próprio e atende syscalls interceptados pelo `MiniNt` existente.
Ainda é uma personalidade NT implementada no host, não um kernel NT completo
que inicializa sozinho em hardware.

## Comportamento implementado

`src/microvm/host/GuestVirtualMemory.h` mantém os metadados de regiões no host e
o estado de cada página de 4 KiB. A arena padrão continua de `0xA00000` a
`0xE00000`; `SetArena` permite configurar outra arena alinhada antes do uso.
Reservas começam em múltiplos de 64 KiB. A arena não pode sobrepor o heap
configurado no `MiniNt`.

| Operação | Efeito verificável |
|---|---|
| `NtAllocateVirtualMemory` | Reserva endereços, faz commit de páginas ou combina as duas operações; valida limites e flags. |
| `NtAllocateVirtualMemoryEx` | Usa a ordem correta de argumentos desta API; aceita zero parâmetros estendidos e recusa opções estendidas não implementadas. |
| `MEM_RESERVE` | Mantém a região ocupada e inacessível ao convidado. |
| `MEM_COMMIT` | Zera páginas recém-comprometidas e aplica a proteção; novo commit de páginas já comprometidas preserva dados e proteção. |
| `NtFreeVirtualMemory` / `MEM_DECOMMIT` | Retira o acesso às páginas, mantendo a reserva; commit posterior zera os bytes antigos. |
| `NtFreeVirtualMemory` / `MEM_RELEASE` | Exige a base original e tamanho de entrada zero; libera a reserva inteira e permite reutilizar seus endereços. |
| `NtProtectVirtualMemory` | Exige páginas comprometidas da mesma reserva, arredonda o intervalo e devolve a proteção anterior em exatamente 4 bytes. |
| `NtQueryVirtualMemory` | Implementa `MemoryBasicInformation`, com estrutura x64 de 48 bytes, para endereços dentro da arena. Distingue livre, reservado e comprometido, proteção, base da reserva e tipo. |
| `NtMapViewOfSection` | Compartilha o controle de endereços com as alocações privadas, sem colisões; registra views como `MEM_MAPPED` ou `MEM_IMAGE`. |
| `NtUnmapViewOfSection` | Remove a view inteira, inclusive quando recebe um endereço interior, e retira seu acesso no WHP. Não libera alocações privadas. |

Os novos caminhos de alocação, liberação, proteção, consulta e mapeamento
verificam os ponteiros de entrada/saída, os limites da RAM e as permissões antes
de acessar os dados. Só atendem o processo corrente
(`HANDLE(-1)`). Os campos de saída são preservados quando a operação falha;
a consulta com buffer curto pode informar o tamanho necessário. Aliasing entre
campos de saída e campos que seriam protegidos/liberados é recusado neste
subconjunto. Valores e intervalos que causariam overflow são recusados.

## Permissões aplicadas à execução

`MicroVmMonitor::SetMemoryAccess` usa `WHvMapGpaRange` para substituir permissões
de páginas e `WHvUnmapGpaRange` para retirar o acesso. O controle de acesso não é
apenas um valor retornado pelo syscall: o WHP bloqueia as instruções do convidado.

| Proteção aceita | Leitura | Escrita | Execução |
|---|---|---|---|
| `PAGE_NOACCESS` | Não | Não | Não |
| `PAGE_READONLY` | Sim | Não | Não |
| `PAGE_READWRITE` | Sim | Sim | Não |
| `PAGE_EXECUTE_READ` | Sim | Não | Sim |
| `PAGE_EXECUTE_READWRITE` | Sim | Sim | Sim |

`PAGE_GUARD`, copy-on-write, execução sem leitura, `ZeroBits` diferente de zero,
large pages e outras flags não implementadas retornam erro. `MEM_TOP_DOWN` é
suportado dentro da arena. Uma página comprometida com `PAGE_NOACCESS` continua
distinta de uma página apenas reservada.

O primeiro uso do gerenciador retira o mapeamento de toda a arena livre. A RAM
do host continua pré-alocada: commit/decommit não implementam demanda de memória
física, paginação ou swap. As tabelas de páginas iniciais continuam usando o
mapeamento de identidade existente; as restrições adicionais são aplicadas no
segundo nível de tradução do WHP.

Um acesso proibido encerra a execução com o tipo de acesso, GPA e RIP. Ainda não
é entregue uma exceção de acesso ao código do convidado nem há recuperação SEH.
Se uma atualização de mapeamento falha, o gerenciador marca a VM como inválida
e impede novas execuções: é necessário recriar a VM. Não retorna sucesso com
permissões incertas nem tenta continuar após uma atualização parcial.

## Integração com seções e limites atuais

Views de dados respeitam as permissões máximas da seção também nas chamadas
posteriores de proteção. Fechar o handle da seção não destrói uma view já
mapeada. A cópia de dados respeita o tamanho da view e não escreve na reserva
vizinha. Offset diferente de zero, commit parcial de seção e flags adicionais
de mapeamento são recusados nesta etapa.

**Atualização de objetos:** a etapa de [handles e objetos](KERNEL-OBJECTS.md)
passou a manter referências do arquivo na seção e da seção nas views, além de
verificar os direitos do handle ao mapear. Fechar handles não invalida as
referências restantes; a última referência libera o objeto de arquivo.

O modelo de seções continua copiando dados para cada view; não há
coerência de páginas compartilhadas, persistência das alterações no arquivo ou
copy-on-write. Tokens, ACLs e compartilhamento de páginas continuam pendentes.
O carregamento de `SEC_IMAGE` mantém
a política anterior de imagem inteira RWX; proteção por seção PE ainda não foi
implementada. O parser PE antigo não foi integralmente auditado.

As permissões de CPU e a validação do novo ABI não significam que todos os
syscalls antigos estejam revisados: ainda existem cópias em massa e respostas
provisórias em outros caminhos de `MiniNt`. A consulta de regiões não cobre
módulos, stacks e heaps fora da arena. Há um único espaço de endereços e uma
vCPU. `NtKernel` (modelo A) continua separado.

`SnapshotManager` salva RAM e registradores, sem capturar os metadados de heap,
regiões, seções ou as permissões WHP. Seu restore não pode ser usado para
restaurar o estado completo do `MiniNt`; essa integração continua pendente.

## Compilar e validar

Requisitos: Windows x64, Visual Studio 2022 com C++, Windows SDK e CMake.

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

As compilações ficam em `build-codex`. O script executa quatro testes:

- `microvm_memory`: limites e operações do heap, incluindo 2.500 operações combinadas.
- `microvm_heap_guest`: execução do heap próprio em ring 3 pelo dispatcher existente.
- `microvm_virtual_memory`: estados, arredondamento, preservação/zeragem de dados,
  permissões, reutilização, views e falhas induzidas no backend de mapeamento.
- `microvm_virtual_memory_guest`: syscalls em ring 3, canários do ABI, consulta
  de regiões, leitura/escrita/execução permitidas e bloqueadas pelo WHP, execução
  de código RX, coexistência de views e memória privada e invalidação da VM.

Os programas dos novos testes são gerados no próprio código. Não usam amostras
externas. O watchdog limita as execuções; os testes de convidado retornam 77
quando o WHP não está disponível, e o CTest registra isso como ignorado.

Os exemplos existentes usados como regressão são `vmntdll`, `vmntsect`,
`vmntimage` e `vmwininit`. Executam stubs das DLLs do sistema ou a pequena DLL
`test/imgtest.dll` dentro do convidado. `vmntimage` agora encontra esse fixture
na pasta fonte configurada pelo CMake, sem depender do caminho de outro checkout.

## Resultados nesta máquina

Validação em Windows x64 com WHP disponível, MSVC 19.44 e SDK 10.0.26100.0:

| Verificação | Debug | Release |
|---|---|---|
| Quatro testes do CTest | 4 passaram, nenhum ignorado | 4 passaram, nenhum ignorado |
| `vmntdll`: stub real de alocação e leitura/escrita no convidado | Passou | Passou |
| `vmntsect`: conteúdo do arquivo virtual lido pela view | Passou | Passou |
| `vmntimage`: relocação e execução de `Probe()` da DLL de teste | `0xC0FFEE5`, correto | `0xC0FFEE5`, correto |
| `vmwininit`: exports reais atendidos pelo heap próprio | Passou | Passou |
| Compilação completa do projeto | Não executada | Passou, 35 executáveis |

Os testes de convidado realmente executaram; não foram ignorados por ausência
de hipervisor. O script `Build-Kernel.ps1 -Configuration Debug` também completou
com sucesso, sem avisos nos alvos compilados. A compilação completa Release
mantém avisos anteriores de conversão de tipos no modelo A e em outros módulos.

Evidências em `build-codex`:

- `virtual-memory-release-tests.log`: os quatro testes em Release.
- `virtual-memory-debug-script.log`: compilação e quatro testes em Debug.
- `virtual-memory-regressions.json`: saída completa das oito execuções dos exemplos.
- `virtual-memory-full-release-build.log`: compilação completa Release.
- `virtual-memory-change-manifest.json`: arquivos modificados, backup e hashes.

## Próximas partes do núcleo

O próximo passo é consolidar handles e objetos, incluindo direitos, referências
e vida útil de seções/arquivos. Depois vêm threads, esperas e sincronização com
efeitos reais, além da evolução das tabelas de páginas e da entrega de exceções.
Isso ainda não estabelece compatibilidade integral com NT, Win32 ou drivers,
nem execução segura de malware.

Os arquivos anteriores a esta etapa estão preservados em
`snapshots/codex-before-virtual-memory-20261015-233931/`.

## Referências de semântica

- [ZwAllocateVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwallocatevirtualmemory)
- [ZwFreeVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwfreevirtualmemory)
- [VirtualProtect e views mapeadas](https://learn.microsoft.com/pt-br/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect)
- [Gerenciamento de seções](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/managing-memory-sections)
- [WHvMapGpaRange](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange)
