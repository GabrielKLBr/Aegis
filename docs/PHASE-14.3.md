# Fase 14.3 — NTDLL e serviços nativos do MiniNT

Entrega Windows x64 de 24/10/2026. Esta subfase acrescenta uma matriz limitada
de chamadas nativas ao kernel próprio da 14.2. O carregador e a execução CPL3
continuam dentro do convidado WHP. A entrega não declara compatibilidade geral
com aplicativos Windows, com o Flappy ou com a inicialização completa da NTDLL.
A 14.4 exige outra solicitação.

## Caminho das chamadas

`guardian-kernel-native-nt.gk` reaproveita memória, processos, objetos,
escalonador, TLS, carregador PE e tratamento de exceções existentes. Os serviços
nativos operam sobre esse estado real: reservar/comprometer/proteger memória,
compartilhar páginas, bloquear/acordar threads e manter referências de handles
produz efeitos no convidado. Não há um encaminhamento de syscalls ao Windows host.

A NTDLL própria exporta 48 entradas Nt e seus 48 aliases Zw. Usa um pacote
versionado de 104 bytes, aceito apenas quando a chamada parte desse módulo
convidado. Serviços sem implementação retornam `STATUS_NOT_IMPLEMENTED` e geram
telemetria. Argumentos, ponteiros, tamanhos, permissões, cotas e propriedade de
processo são conferidos antes de acessar dados do usuário.

Para uma NTDLL original fornecida legalmente, `native_nt.py` lê os stubs e o
recurso de versão do mesmo conjunto imutável de bytes. Gera um mapa de números
de serviços para operações internas, vinculado à DLL e ao perfil. O kernel
recebe os argumentos pelo ABI x64 da DLL selecionada; não reutiliza a tabela
de syscalls do host. Entradas desconhecidas, inclusive números coincidentes com
o protocolo privado antigo, não entram nesse protocolo.

O perfil determina versão, build e service pack nas estruturas convidadas.
Há um PEB separado, TEB por thread e uma página de dados compartilhados em
`0x7FFE0000`, somente leitura e sem execução para CPL3, com relógios coerentes
com a fonte publicada pelo host. São estruturas parciais para esta matriz;
`PEB_LDR_DATA`, parâmetros de processo completos e `LdrInitializeThunk` não
estão implementados.

## Matriz implementada

| Família | Recorte funcional |
|---|---|
| Memória | NtAllocate/Free/Protect/QueryVirtualMemory no próprio processo; reserva, commit, decommit, release e arredondamento de páginas |
| Seções | NtCreateSection/MapViewOfSection/UnmapViewOfSection anônimas; duas views compartilham os mesmos frames; views sobrevivem ao fechamento do handle |
| Handles | NtClose, NtDuplicateObject no mesmo processo com SAME_ACCESS, NtQueryObject/BasicInformation; contagem e direitos preservados |
| Processos/threads | NtOpenProcess/Thread, informações básicas do próprio processo/thread, NtCreateThreadEx, NtResumeThread, término do chamador/processo atual |
| Sincronização | Eventos manuais/automáticos, mutants, semáforos e timers; espera simples/múltipla; prazos absolutos/relativos |
| APC e tempo | NtQueueApcThread com três argumentos, NtTestAlert, NtDelayExecution alertável, horário e contador; retorno restaurado após callback |
| Arquivos | NtCreate/Open/Read/WriteFile e QueryInformationFile/StandardInformation, I/O síncrono no volume RAM convidado |
| Registro | NtCreate/OpenKey, Set/Query/DeleteValueKey e DeleteKey; direitos e ACLs do volume convidado |
| RTL | Versão, inicialização/comparação de strings, conversão limitada de status, heaps com zero/realloc/free/size/destroy |
| TLS/FLS | TLS PE já existente; FLS da fibra padrão por thread, valores separados e callbacks em saída normal/liberação |
| Ldr | LoadDll, UnloadDll, GetDllHandle e GetProcedureAddress sobre catálogo e referências reais do carregador próprio |
| Exceções | RtlRaiseException, SEH e captura de pilha pela infraestrutura existente; unwind x64 v1 e subconjunto v2 validado |

NtTerminateThread é término de baixo nível: não executa callbacks FLS nem
notificações de DLL. Saída normal de thread continua a executá-los. APCs aninhadas,
término remoto e criação de outro processo pelo ABI nativo permanecem recusados.
NtTestAlert retorna sucesso após entregar APC; uma espera alertável interrompida
retorna `STATUS_USER_APC`. Essa diferença foi conferida no Windows nativo.

Operações nativas passam pela política/telemetria existente. `guardian-pe` grava
`boot.json`, `analysis.json` e `analysis.html`. Há eventos de solicitação/resultado
com status; operações de arquivo/registro também registram o recurso no volume.
Isso amplia a observabilidade da fase 13, sem prometer detecção perfeita de malware.

## Limites e pendências

| Recurso nativo | Limite desta entrega |
|---|---|
| Alocação VM / rastreamento | 8 MiB por chamada / 32 intervalos nativos por processo |
| Seções anônimas | 8, até 256 KiB cada, 512 frames compartilháveis no total |
| Handles nativos | 32 por processo, sujeitos também às cotas dos gerenciadores existentes |
| Threads | Até 4 slots globais; stacks e TEB/TLS privados |
| Processos no caminho nativo | Um EXE externo por boot; criação nativa de filhos ainda recusada |
| Heaps RTL | 4, até 1 MiB cada; flags suportadas explicitamente |
| FLS | 31 índices na fibra padrão; outras fibras ainda não implementadas |
| Arquivos/registro mutáveis | Volume RAM de 64 KiB; 32 nós, payload agregado até 16 KiB, operações/arquivos até 512 bytes |
| Nomes nativos | ASCII imprimível em descritores Unicode, até 127 caracteres; comparação case-insensitive RTL apenas ASCII |
| Unwind PE no kernel nativo | Até 8.192 funções por módulo; kernels anteriores mantêm 4.096 |

As cotas não garantem que todos os máximos caibam simultaneamente. O volume RAM
mutável é separado do volume de EXE/DLLs somente leitura: não é NTFS, não persiste
e não permite que Ldr carregue uma DLL gravada nesse volume. Permanecem pendentes
seções baseadas em arquivo/SEC_IMAGE, I/O assíncrono, namespaces de objetos
nomeados, classes de informação adicionais, Unicode/NLS completo, outros flags
de heap e a maior parte dos exports da NTDLL original.

O carregador aceita o diretório LoadConfig como metadado limitado; isso não
implementa CFG, CET ou todas as mitigações descritas nele. Unwind v2 aceita os
metadados exercitados pela DLL e pela referência sintética. Epílogos fora do
decodificador existente retornam recusa explícita; não se presume cobertura de
todas as instruções possíveis. O host continua com o limite de imagem de kernel
de 1,5 MiB. A CLI usa 256 MiB e o orçamento de execução de 30 segundos existente.
Snapshots desse runtime e integração PE/SMP continuam pendentes.

## Referências executadas e aceitação

O ensaio com DLL original usa a NTDLL x64 extraída da imagem 1 do `install.wim`
da ISO Windows 10 22H2 pt-BR fornecida pelo usuário. A extração não executa a DLL
no host. A versão real do arquivo é **10.0.19041.2788**; o perfil Win10 tem
build 19045. Essa diferença da família de enablement é registrada, sem alegar
que houve comparação com um kernel Windows 19045 original.

- SHA-256 da DLL: `fdb2689bffabe7d2e300882ca1c3fc2fe24a998ffcbd5f48795c7d95712d1e98`.
- SHA-1 coincide com a entrada extraída: `959735a57905cb6037af73cb29a4cf99fb5f95fe`.
- 47 stubs reconhecidos no mapa. NtQuerySystemTime é uma rotina em modo usuário
  que lê os dados compartilhados; não recebe um número inventado de syscall.
- A matriz original exercita memória, seções, objetos, APCs, arquivos e algumas
  rotinas RTL. Não exercita heap, FLS, Ldr nem SEH da DLL original.
- A matriz com DLL própria exercita também registro, heaps, FLS, criação e
  encerramento de threads, SEH, captura de pilha e referências de Ldr nos três
  perfis: Win7 6.1.7601/SP1, Win10 10.0.19045 e Win11 10.0.22631.
- Um executável de teste próprio compara ABI, memória, seções, objetos, APCs e
  heaps com as APIs do Windows host. Outro compara unwind com RtlVirtualUnwind.
  Isso não substitui referências nativas exatas de Win7/Win10/Win11, ainda pendentes.

A aceitação exige 21 testes CTest por configuração, nenhum ignorado: os 15
critérios da 14.2 e seis novos da 14.3. O teste da DLL original é obrigatório
para a aceitação desta máquina; não é declarado aprovado por omissão quando
`GUARDIAN_NATIVE_REFERENCE_DLL` não foi configurado. A regressão completa Release
contém 96 testes. Relatórios locais em `build-phase14.3` e os arquivos de validação
do pacote registram o resultado efetivamente obtido e os hashes estáveis dos
fontes/binários. Tentativas anteriores e logs de falhas são preservados.

Na regressão inicial, os 90 testes anteriores passaram; três dos seis testes
novos falharam. A comparação nativa corrigiu a tradução de
`STATUS_NOT_IMPLEMENTED` para erro Win32 1, e a reabertura de arquivo/registro
passou a fornecer o tamanho exigido pela consulta de ACL. O reteste dos seis
testes nativos passou. O resumo combina 96 testes distintos, sem esconder as
falhas iniciais nem afirmar que a primeira execução completa foi aprovada.

Os testes negativos incluem ponteiros de supervisor, overflow, commit que cruza
página, handle obsoleto, direitos insuficientes, falha de alocação em múltiplos
pontos e rollback de views. Os boots exigem liberação de todos os recursos além
das 21 páginas permanentes do bootstrap, telemetria completa e chamadas
desconhecidas recusadas. O pacote é verificado por hashes e iniciado novamente
nas duas configurações.

## Reprodução

Com MSVC 2022, SDK Windows, CMake, Python 3, WHP e NX ativos, configure o build
com BUILD_TESTING, GUARDIAN_GUI_COMPATIBILITY_ONLY, GUARDIAN_BUILD_PE_GUI,
GUARDIAN_BUILD_EXTERNAL_PE e GUARDIAN_BUILD_NATIVE_NT ligados. Defina
`GUARDIAN_NATIVE_REFERENCE_DLL` para a cópia legal da DLL original Win10. Compile
Release e Debug antes de executar:

```powershell
ctest --test-dir BUILD -C Release -j 1 --output-on-failure
python tools/validate_phase14_3.py --build BUILD --config Release --original NTDLL-LEGAL --output ACEITACAO-RELEASE-NOVA
python tools/validate_phase14_3.py --build BUILD --config Debug --original NTDLL-LEGAL --output ACEITACAO-DEBUG-NOVA
python tools/package_phase14_3.py --build BUILD --release ACEITACAO-RELEASE-NOVA --debug ACEITACAO-DEBUG-NOVA --output PACOTE-NOVO
python tools/smoke_phase14_3.py --package PACOTE-NOVO --output SMOKE-NOVO
```

Para montar uma matriz própria: `python tools/native_nt.py bundle --build BUILD
--config Release --profile win10 --output VOLUME-NOVO`. A opção `--original`
seleciona a DLL legal em vez da implementação própria. ISOs, WIMs e DLLs da
Microsoft não fazem parte do Git público nem do pacote de builds. Os fontes
completos do projeto vão para Aegis; a build desta fase vai para Aegis-builds
privado, junto das fases anteriores.

## Fontes técnicas

- [NtCreateSection — Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatesection).
- [RtlGetVersion — Microsoft](https://learn.microsoft.com/en-us/windows/win32/devnotes/rtlgetversion).
- [Tratamento de exceções x64 — Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170).
- [Unwinder AMD64 do .NET — código oficial](https://github.com/dotnet/runtime/blob/main/src/coreclr/unwinder/amd64/unwinder.cpp).
