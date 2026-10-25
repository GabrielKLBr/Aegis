# Fase 14.4 — KERNEL32/KERNELBASE e contratos básicos Win32

Implementação Windows x64 de 24/10/2026. A matriz desta subfase executa programas
externos de console com KERNEL32 e KERNELBASE próprias, apoiadas nos serviços
nativos da 14.3. Foram acrescentados processos filhos nesse caminho, arquivos
compartilhados no convidado, thread pool e conclusão de I/O. Isso não declara
compatibilidade geral com aplicativos Windows, CRT, GUI ou o Flappy. A 14.5
depende de outra solicitação.

## Bibliotecas e execução

`guardian-kernel-win32.gk` mantém o formato e o limite de imagem de 1,5 MiB dos
kernels anteriores. Os estados de até três processos são alocados em frames
do convidado quando necessários, liberados ao encerrar o boot. Cada processo
tem carregador, páginas, TEB/TLS, PEB, heaps e dados de DLL separados; quatro
threads por processo compartilham esse espaço. O escalonador tem 16 slots
globais. Esses são os limites existentes do gerenciador, não uma promessa de
que todas as cotas máximas cabem simultaneamente.

O volume externo inclui EXE principal, EXE filho, plugin, `kernel32.dll`,
`kernelbase.dll`, NTDLL própria, runtime Guardian e metadados API set. O código
dos aplicativos e dessas DLLs roda em CPL3. KERNEL32 contém forwarders para
KERNELBASE; esta implementa wrappers e rotinas em modo usuário, chama a NTDLL
própria e reutiliza as implementações existentes de carregamento, console,
threads e exceções. O host não resolve imports desses programas nem encaminha
seus pedidos às APIs do Windows host.

O novo PEB contém um prefixo de parâmetros de processo com imagem, linha de
comando, diretório e ambiente. Filhos recebem outra cópia dos parâmetros e do
ambiente, mantendo a identidade do pai e suas restrições no gerenciador.
Alterar o ambiente no filho não altera o pai. Os filhos podem iniciar suspensos,
ser retomados e aguardados por handles reais, inclusive após o término.

Há **128 nomes exportados** nas variantes Win10/Win11 e **127 no perfil Win7**.
`GetSystemTimePreciseAsFileTime` é omitida da exportação Win7. A contagem inclui
aliases/forwarders e não significa cobertura integral de cada combinação de
argumentos ou de toda a API Windows.

## Matriz funcional

| Família | Implementação exercitada |
|---|---|
| Memória/heaps | VirtualAlloc/Free/Protect/Query; heaps RTL, criação, tamanho, zero, realloc e liberação |
| Processos | CreateProcessA/W no catálogo imutável, suspensão/retomada, waits, IDs e códigos de saída; filho separado e linha de comando |
| Threads | CreateThread, consulta de saída, yield, saída normal, TLS dinâmico de 64 índices e FLS da NTDLL |
| Thread pool | Quatro objetos de trabalho, fila limitada, worker convidado, callbacks, espera, cancelamento de trabalhos ainda enfileirados e fechamento |
| Sincronização | Eventos manuais/automáticos, waits simples/múltiplos; mutexes e seções críticas reaproveitados; Sleep/SleepEx alertável |
| Console | GetStdHandle de saída/erro e WriteFile no console convidado |
| Arquivos | CreateFileA/W, Read/WriteFile, cursor, tamanho, flush, criação/remoção de diretórios e exclusão de arquivos |
| Mapeamentos | Seções anônimas e baseadas em arquivos pequenos; views compartilham frames, mantêm backing após CloseHandle e escrevem de volta no flush/último unmap |
| I/O assíncrono | Worker convidado, OVERLAPPED, evento, ERROR_IO_PENDING/IO_INCOMPLETE, GetOverlappedResult, CancelIoEx e ReadFileEx/WriteFileEx com APC de conclusão |
| Ambiente/caminhos | Variáveis A/W, cópia/liberação do bloco, herança do ambiente, diretório por processo, normalização de ponto/ponto-ponto e caminho da imagem |
| Unicode/NLS | UTF-8/UTF-16, pares substitutos, Windows-1252/ACP, tamanhos de buffer, erro de entrada inválida, comparação ordinal e maiúsculas/minúsculas no recorte descrito abaixo |
| Sistema/tempo | Versão do perfil, informações da CPU/arena, RAM total/disponível do convidado, QPC, ticks e horário existentes |
| Módulos/recursos | LoadLibraryW/A, GetModuleHandle, GetProcAddress, forwarders, unload e recursos PE pelo carregador existente |
| Exceções | SEH/RaiseException na aplicação e no processo filho, com o dispatcher/unwind existente |

O armazenamento mutável é um volume RAM compartilhado pelos serviços de cada
processo, separado do volume somente leitura de EXE/DLLs. ACLs, compartilhamento
de handles, propriedade e cotas continuam a ser verificadas pelo volume.
Fechar o último processo descarta seus arquivos. A CLI grava `boot.json`,
`analysis.json` e `analysis.html`; os testes conferem chamadas de arquivo do
filho, criação de processo, resultados, ausência de perdas e devolução de frames.
Rotinas puramente em modo usuário, como conversão de texto e acesso a TLS,
não se tornam automaticamente chamadas de kernel na telemetria.

## API sets e versões NT

API sets são nomes lógicos: o carregador resolve o host pelo schema, sem exigir
um arquivo físico chamado `api-ms-...dll` para cada import. Essa organização é
descrita na [documentação da Microsoft](https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets).

O novo leitor valida namespaces v2 (perfil Win7) e v6 (Win10/Win11), offsets,
comprimentos, índices, orçamento de entradas e nomes de host. Suporta host
padrão e substituição por módulo importador. No formato v6, resolve revisões
anteriores da mesma família pelo prefixo HashedLength; revisões acima da
selecionada são recusadas de forma conservadora. Ciclos de resolução têm limite
de profundidade. O host final precisa existir no catálogo convidado; não há
busca no System32 do host.

A referência real é `apisetschema.dll` x64, versão **10.0.19041.2788**, extraída
da imagem 1 do `install.wim` da ISO Win10 22H2 pt-BR fornecida pelo usuário.
SHA-256: `714abf6fb87298d04c3137fb2a53c86335fb6fbe1f3d922a000293482b21b576`.
Ela é lida como dados, sem execução no host. O ensaio do parser lê seus **891
contratos**; o pacote Win10 reencodifica apenas os **168** com hosts
KERNELBASE/KERNEL32/NTDLL na referência. Não inclui código ou cópia desse binário
da Microsoft. Contratos cujo host ainda não foi implementado continuam recusados.

Os schemas Win7/Win11 são fixtures próprias no formato correspondente, com
contratos limitados. Referências nativas exatas dessas versões, schemas adicionais,
todos os detalhes de lookup/revisão e as DLLs KERNEL32/KERNELBASE originais não
foram aceitos nesta etapa. A versão 19041 da referência e o perfil Win10 19045
continuam identificados separadamente, sem alegar conformidade de kernel exata.

## Limites conhecidos

| Recurso | Limite/recorte desta entrega |
|---|---|
| Arquivo mutável | Até 512 bytes por arquivo; 32 nós e volume RAM de 64 KiB, payload agregado até 16 KiB |
| ReadFile/WriteFile | Pedido Win32 até 1 MiB, dividido em operações nativas de 512 bytes; leitura pode terminar antes; escrita respeita a cota real do arquivo e informa falha/transferência parcial |
| Handles | Até 8 handles de arquivo por proprietário no backend, incluindo backing de mapeamentos; 32 handles nativos, além das cotas existentes do gerenciador |
| Mapeamento de arquivo | Até 512 bytes de conteúdo, backing com compartilhamento permitido, página de view arredondada para 4 KiB; seções anônimas mantêm o limite de 256 KiB |
| I/O | Fila de 8 pedidos/trabalhos, um worker por processo; offsets altos, IOCP, drivers e IRPs Windows completos pendentes |
| Cancelamento | Pedidos ainda enfileirados podem ser cancelados; operação RAM já despachada pode terminar antes do cancelamento |
| APCs | Quatro pendentes por thread; emissor precisa permanecer vivo e entrar em espera alertável; falha de despacho fica no status do OVERLAPPED |
| Thread pool | Quatro objetos; pool padrão serial; ambiente/pool personalizado, prioridades, callbacks autorreferentes de fechamento e toda a superfície Windows não implementados |
| Ambiente | Bloco de 512 caracteres UTF-16 por processo; nomes até 127 caracteres no repertório de case suportado; valores dentro da cota |
| Caminhos | Até 127 caracteres; armazenamento e nomes de módulos restritos a ASCII; caminhos Unicode fora desse recorte são recusados, embora conversão de texto suporte UTF-16/UTF-8 |
| CreateProcess | EXE no volume imutável, comando ASCII até 127 caracteres, flags básicas/suspensão; herança de handles, blocos personalizados de ambiente, diretório explícito e opções de segurança adicionais recusados |
| NLS | UTF-8 e Windows-1252; ACP 1252; case Latin-1 e pares selecionados de Windows-1252, locale invariável/pt-BR; repertórios de case e locales restantes recusados |
| Runtime | Apenas x64; CRTs, WOW64, GUI e compatibilidade binária geral ainda pendentes |

I/O overlapped executa trabalho real no worker e registra sua conclusão; não é
uma devolução constante de sucesso. Contudo, esta fila em modo usuário não
implementa toda a semântica de I/O do kernel Windows. A possibilidade de conclusão
síncrona/assíncrona e os cuidados com buffers são descritos na
[documentação Microsoft sobre overlapped I/O](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-overlapped-i-o).

As demais cotas da 14.2/14.3 permanecem: imagens PE até 8 MiB, 16 entradas de
catálogo, arena de 256 MiB, 64 MiB de páginas privadas por processo, heap RTL
até 1 MiB por heap e estruturas nativas parciais. O host usa 256 MiB de RAM e
orçamento de boot de 30 segundos. Não há snapshots desse caminho nem integração
PE/SMP concluída. Código LoadConfig aceito como metadado não implementa CFG/CET.
Isso não é uma certificação de isolamento ou de detecção perfeita de malware.

## Validação e reprodução

Quatro testes novos cobrem: referência Windows nativa; gerenciadores reais com
FS compartilhado, backing e isolamento; ferramentas/parser API set; e boots dos
três perfis. Os boots executam threads, TLS, thread pool, arquivos, mapeamentos,
I/O por evento e por APC, cancelamento, processo filho suspenso, ambiente isolado,
recursos, carga dinâmica de DLL e SEH. Também testam escrita negada, diretório
ausente, filho inexistente, buffer maior que o arquivo, entrada UTF-8 inválida
e recusa de case fora do repertório. Alterar o volume invalida o vínculo antes
de preparar a sessão.

O corpus do parser inclui v2/v6, DLL de schema original, overrides por importador,
revisões, offsets/índices inválidos, PE truncado e host com travessia. O teste
nativo Windows usa somente executável de teste próprio e bibliotecas do sistema
host para comparar Unicode, API set, thread pool, filho suspenso, waits e SEH.
Esse ensaio não substitui referências exatas dos três kernels alvo.

A aceitação exige **25 testes por configuração**, sem ignorados: os 21 da 14.3
e quatro novos. A regressão completa Release contém **100 testes**. Os relatórios
locais em `build-phase14.4` e os relatórios incluídos na build registram os
resultados efetivos e SHA-256 estáveis dos fontes/binários. Falhas de preparação
das DLLs e logs anteriores são preservados. O pacote é conferido por inventário,
hashes e novo boot em Release/Debug antes da publicação.

Configure com os mesmos requisitos da 14.3, adicionando
`GUARDIAN_BUILD_WIN32_NATIVE=ON` e `GUARDIAN_APISET_REFERENCE_DLL` para a cópia
legal do schema. Mantenha `GUARDIAN_NATIVE_REFERENCE_DLL` para a regressão da
NTDLL original. Compile Release e Debug e use pastas novas para evidências:

```powershell
ctest --test-dir BUILD -C Release -j 1 --output-on-failure --output-junit REGRESSAO.xml
python tools/validate_phase14_4.py --build BUILD --config Release --original NTDLL-LEGAL --schema-reference SCHEMA-LEGAL --output ACEITACAO-RELEASE
python tools/validate_phase14_4.py --build BUILD --config Debug --original NTDLL-LEGAL --schema-reference SCHEMA-LEGAL --output ACEITACAO-DEBUG
python tools/package_phase14_4.py --build BUILD --release ACEITACAO-RELEASE --debug ACEITACAO-DEBUG --schema-reference SCHEMA-LEGAL --regression REGRESSAO.xml --output PACOTE-NOVO
python tools/smoke_phase14_4.py --package PACOTE-NOVO --output SMOKE-NOVO
```

Para montar o volume da matriz: `python tools/win32_runtime.py bundle --build
BUILD --config Release --profile win10 --schema-reference SCHEMA-LEGAL --output
VOLUME-NOVO`. A build distribuída já inclui seus volumes. Fontes completos vão
para Aegis; builds, para Aegis-builds privado. ISO, WIM e DLLs Microsoft originais
ficam fora dessas publicações.
