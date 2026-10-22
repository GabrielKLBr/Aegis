# Fase 8 — Runtime de console, arquivos, registro, heap e locks

Base ativa: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Estado em 17/10/2026: segundo marco implementado e validado; **fase 8 em andamento**.
A fase 9 permanece fechada. O primeiro marco está em [PHASE-8.md](PHASE-8.md).

## Comportamento implementado

O novo `guardian-runtime-console.exe` roda em CPL3, usando uma DLL própria,
`guardian-runtime-api.dll`. São PEs compilados, sem CRT nem DLLs do Windows.
O loader, TLS, PEB/TEB inicial e proteções continuam executando no convidado.
O loader também valida metadados de unwind encadeados gerados pelo MSVC em
Release: referências devem apontar para entradas anteriores já verificadas,
com frame compatível. Ciclos, referências futuras e trailers truncados são
recusados. Isso ainda não executa o unwind de uma exceção.
O programa importa funções com assinaturas e comportamento do subconjunto
Win32 descrito abaixo. A DLL converte essas chamadas para uma ABI própria,
validada pelo kernel; não encaminha chamadas para o Windows host.

`guest/PeServices.cpp` mantém os handles de arquivo e chave no kernel, com
identidade do processo, tipo, direitos concedidos e posição de arquivo.
Um processo não pode usar handles de outro, transformar chave em arquivo,
escrever por um handle somente de leitura ou reutilizar um handle fechado.
As operações de arquivo e registro usam o volume e as permissões da fase 7.
Os caminhos se referem apenas ao disco virtual e ao registro do convidado.

O console usa a fila, o driver e o protocolo de I/O existentes. O kernel abre
o dispositivo com o direito de escrita e mantém sua referência até concluir
o pedido. A saída observada pelo host é `PE RUNTIME OK` seguida de `IO OK`,
ambas com nova linha. Bytes fora do subconjunto ASCII permitido são recusados
antes de chegar ao protocolo do dispositivo.

O heap de usuário usa o mesmo algoritmo do kernel, extraído para `Heap.h`:
alinhamento de 16 bytes, divisão e coalescência de blocos, zeragem e realloc
que mantém a alocação antiga se não houver espaço. Um mutex real do kernel
serializa esse heap. Mutexes e seções críticas usam o escalonador e a tabela
de objetos existentes; não são implementados como funções vazias.

`GetLastError` e `SetLastError` usam o campo do TEB da thread. As operações
de registro retornam seu código diretamente, preservando esse campo. As
operações de arquivo convertem os erros do volume para o subconjunto Win32.

## APIs presentes na DLL própria

| Grupo | Funções |
|---|---|
| Identidade e erro | `GetCurrentProcessId`, `GetCurrentThreadId`, `GetLastError`, `SetLastError` |
| Memória | `GetProcessHeap`, `HeapAlloc`, `HeapReAlloc`, `HeapFree` |
| Console | `GetStdHandle`, `WriteFile` para saída/erro padrão |
| Arquivos | `CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointerEx`, `GetFileSizeEx`, `FlushFileBuffers`, `CloseHandle` |
| Registro | `RegCreateKeyExA`, `RegOpenKeyExA`, `RegSetValueExA`, `RegQueryValueExA`, `RegCloseKey` |
| Mutex | `CreateMutexA`, `WaitForSingleObject`, `ReleaseMutex`, `CloseHandle` |
| Seção crítica | `InitializeCriticalSectionEx`, `EnterCriticalSection`, `TryEnterCriticalSection`, `LeaveCriticalSection`, `DeleteCriticalSection` |

O volume recebeu também uma operação de consulta de tamanho por handle, sem
alterar o tamanho das estruturas ou os contratos de relatório anteriores.

## Critérios verificados dentro do convidado

A aplicação valida TLS e identidade, preservação de erro, alinhamento e
zeragem do heap, crescimento com preservação dos dados, falta de memória,
dupla liberação, recursão de mutex e seção crítica, liberação sem propriedade
e uso de mutex fechado.

Em arquivo, verifica criação/abertura, leitura, escrita, tamanho, posições
relativas ao início e ao fim, EOF, tentativa de posição negativa, conflitos de
compartilhamento, criação exclusiva de arquivo existente, uso após fechamento
e flush. No registro, verifica criação/abertura, DWORD, consulta do tamanho,
buffer insuficiente sem sobrescrita e recusa de escrita por chave somente
de leitura. Tipos de registro sem conversão implementada retornam erro explícito.

O programa mantém um contador em `C:\runtime.bin` e em `HKCU\Runtime\Counter`.
Na segunda inicialização, confere que os dois sobreviveram e têm o mesmo
valor antes de incrementá-los. Fecha os handles usados normalmente e deixa
outros dois abertos de propósito para o teste de limpeza no término.
O kernel verifica a ausência de handles e a devolução de todas as páginas
da prova. A desmontagem da DLL também exige que o heap esteja vazio.

Pedidos com endereço do kernel ou versão inválida são recusados sem provocar
fault do kernel. A mensagem `PE runtime services heap locks PASS` só é emitida
após o programa terminar com o resultado esperado e a limpeza ser verificada.

## Validação e preservação

O contrato exige 35 testes por configuração em Debug e Release, sem ignorados.
A matriz soma 720 boots: 80 do runtime novo, formando 40 pares persistentes,
e 640 de regressão, incluindo o primeiro console PE. Permanecem os 240 cenários
externos de falha das fases anteriores.

O teste nativo do novo serviço/heap cobre 185 condições, incluindo isolamento
de handles, direitos, EOF, quotas antes de efeitos colaterais, falha de backend
sem avanço da posição e falha de fechamento com nova tentativa de limpeza.
Os testes anteriores mantêm as verificações do loader, volume, persistência,
I/O, processos, escalonador e memória.
Há ainda uma prova nativa de cadeia de unwind em múltiplos níveis, seis
variações inválidas e falha injetada em cada operação de backend dessa carga.

Uma árvore nova, `build-phase8-runtime-clean`, compila as nove imagens de
produção e executa suas auditorias. Os sete exemplos de referência são
executados por configuração, junto com auditorias e provas da CLI. O build
completo contém 63 executáveis de ferramentas/testes, 11 imagens de kernel
(nove de produção e duas de falha de CPU) e quatro PEs próprios de console/DLL
por configuração.

Os arquivos novos de evidência e discos usam prefixo `phase8-runtime-`.
Os 401 arquivos históricos registrados no snapshot, os discos da fase 7
e os discos do primeiro marco da fase 8 permanecem conferidos por SHA-256.
O snapshot dos originais é
`snapshots\codex-before-phase8-runtime-20261017-010738`.

O verificador e o contrato deste marco são `tools/validate_phase8_runtime.py`
e `config/phase8-runtime-acceptance.json`. O resultado está em
`build-codex/phase8-runtime-change-manifest.json`, com estado da fase
`in_progress` e `next_phase_started: false`.

Para repetir a validação atual, guardando os logs por configuração:

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild *> build-codex\phase8-runtime-full-debug-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-runtime-debug-tests-details.log
.\Build-Kernel.ps1 -Configuration Release -FullBuild *> build-codex\phase8-runtime-full-release-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-runtime-release-tests-details.log
python tools\validate_phase8_runtime.py --snapshot "snapshots\codex-before-phase8-runtime-20261017-010738" --run-programs
```

## Limites atuais e continuação da fase 8

Esta matriz usa uma thread por processo PE. Ainda faltam criação de filhos e
threads por essas APIs, TEB/TLS privados por thread e testes de contenção do
runtime com várias threads. O escalonador possui suas provas independentes,
mas isso ainda não valida a combinação com este runtime PE.

O heap de usuário tem 8 KiB. Há até 16 handles de serviço, oito por processo,
além dos limites anteriores da tabela de objetos. Arquivos e operações de
dados mantêm o limite de 512 bytes do volume. Nomes/caminhos são ASCII e as
funções são síncronas, sem overlapped I/O. O registro suporta NONE, BINARY,
DWORD e QWORD; a conversão ANSI/Unicode de strings permanece pendente.

Há saída e erro padrão; não há leitura de console. Mutexes deste subconjunto
são anônimos e começam sem dono. A inicialização de seção crítica usa a função
`Ex` que pode retornar falha; o tratamento SEH de falhas ainda não existe.
Aplica-se o limite de 127 aquisições recursivas do escalonador existente.
As APIs recusam flags e opções sem implementação, incluindo atributos de
segurança, templates de arquivo, opções de registro e mutex com nome.

Continuam pendentes dispatch de exceções de usuário e unwind x64, carga e
descarga dinâmica de DLLs, a matriz de falha de inicialização/término e a
auditoria final por aplicação. Este marco não declara compatibilidade geral
com programas Windows. A fase 9 só poderá começar depois de concluir a fase 8
e respeitar a parada solicitada pelo usuário.

Referências de contrato: [CreateFileA](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea),
[GetStdHandle](https://learn.microsoft.com/en-us/windows/console/getstdhandle) e
[RegSetValueExA](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexa).
Metadados de unwind: [contrato x64 da Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).
