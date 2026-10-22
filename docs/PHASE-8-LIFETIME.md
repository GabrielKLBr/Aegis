# Fase 8 — Processos filhos e ciclo de vida PE

Data de referência: 17/10/2026. Estado: **fase 8 em andamento**.
Este é o quarto marco; a fase 9 não foi iniciada.

## Resultado

O convidado executa um console PE próprio que cria outros processos PE,
espera seu término e consulta os códigos do processo e da thread inicial.
A DLL própria exporta `CreateProcessA`, `GetExitCodeProcess`, `GetCommandLineA`,
`ExitThread` e `ExitProcess`. As APIs de espera e fechamento também aceitam
handles de processo. A imagem anterior do runtime permanece na regressão.

Cada processo tem seu próprio loader, espaço de endereçamento, módulos,
relocations, índices TLS, PEB e lock de inicialização. O catálogo é imutável
no kernel e contém somente os executáveis e a DLL de referência do projeto.
O filho usa a mesma identidade e sessão, com integridade reduzida para 1 e sem
privilégios adicionais. Não herda handles. Esta restrição é uma política do
convidado, mais estreita que a criação comum do Windows.

A inicialização prepara o filho integralmente antes de publicar seus handles.
Aceita execução imediata ou suspensa, com retomada pela thread inicial.
Os handles retornados pertencem ao pai; o filho não consegue usá-los como seus.
A consulta, espera e alteração continuam sujeitas a tipo, direitos e identidade.
Os objetos de término sobrevivem enquanto existirem referências, mesmo após
liberar o espaço de endereçamento e reutilizar os slots de processo/thread.

O serviço de arquivos/registro agora confere a limpeza por PID. Terminar um
filho não exige que a tabela global de handles esteja vazia e não fecha os
arquivos ou chaves do pai. O teste deixa handles do pai abertos durante todos
os términos dos filhos para exercitar esse isolamento.

## Encerramento

`ExitThread` redireciona a execução para um bootstrap de saída no convidado.
Ele abandona a pilha de chamadas do usuário, executa os callbacks de saída da
thread sob o lock de inicialização e sinaliza seu resultado. Pilha, vínculo e
slot são liberados depois de voltar à tabela de páginas do kernel.

A thread inicial pode sair e deixar outra thread ativa. O heap, PEB, módulos e
serviços do processo continuam disponíveis. Sua página inicial de bootstrap,
que também contém o PEB neste layout limitado, é mantida até o processo acabar;
a pilha e o slot de thread são liberados. O término da última thread executa a
finalização do processo e usa seu código como resultado do processo. O resultado
da thread inicial já encerrada permanece preservado no handle antigo.

`ExitProcess` e o retorno do ponto de entrada usam o caminho de término do
processo. O bootstrap adquire o lock de inicialização, encerra as outras threads
sem enviar `DLL_THREAD_DETACH` a elas, sinaliza seus resultados e chama os
callbacks de processo com o indicador de término. Por último, fecha serviços,
descarrega os módulos, fecha handles e libera o processo. Não termina filhos
como consequência de fechar apenas o processo pai.

Na DLL de referência, o término definitivo permite alocações de heap pendentes,
pois todo o espaço do processo será destruído. O workload anterior mantém a
verificação estrita de heap vazio e pares de callbacks equilibrados. Não há
execução de destrutores C++ automáticos em `ExitThread`, nem unwind/SEH de usuário.

## Provas por inicialização

O novo console primeiro executa toda a prova anterior do runtime: arquivos,
registro persistente, heap, 31 workers, contenção, TLS privado e preservação do
bloco TLS completo da DLL na thread inicial. Em seguida executa 23 filhos:

| Caso | Quantidade | Resultado do processo / thread inicial |
| --- | ---: | --- |
| Retorno normal | 4 | 11 / 11 |
| `ExitThread` na única thread | 4 | 33 / 33 |
| Worker chama `ExitThread(44)` e o principal retorna | 4 | 45 / 45 |
| Principal sai com 55 e o worker continua no heap | 4 | 66 / 55 |
| `ExitProcess(22)` com worker suspenso e heap pendente | 4 | 22 / 22 |
| Dois filhos suspensos simultâneos, retomados depois | 2 | 11 / 11 |
| Criação sem suspensão | 1 | 11 / 11 |

O console verifica `STILL_ACTIVE`, timeout antes da retomada, tipos dos handles,
resultados após espera, recusa de handle fechado e preservação dos resultados
antigos durante reutilização. Dois filhos coexistem com o pai; a tentativa de
criar mais um respeita a quota. Buffers do heap e TLS do pai devem permanecer
intactos, enquanto o heap e TLS do filho começam com os valores próprios.
O caso da thread principal também fecha o handle do worker ainda vivo e depende
do abandono do mutex para o worker continuar. Os callbacks dos filhos verificam
as notificações esperadas, incluindo saída da thread inicial sem attach de thread.

Pedidos com herança, opções de startup não suportadas, nome ausente no catálogo,
argumentos internos inválidos e linha de comando sem terminador são recusados.

Antes de entrar em CPL3, cada boot injeta dez falhas de orçamento na criação:
seis limites de handles e quatro de objetos. Isso alcança preparação do processo,
thread, lock do loader e publicação dos handles no pai. Em cada falha, handles de
saída ficam zerados, e páginas, processos, threads, objetos, handles e referências
voltam à contagem anterior. Um controle adicional prepara um filho suspenso e o
limpa sem executar sua entrada. A criação seguinte deve continuar funcionando.

O marcador `PE child creation rollback 10 fault budgets PASS` depende dessa
conferência. `PE child processes explicit exits last-thread lifetime PASS` só é
emitido depois do retorno zero do console e da limpeza completa de todos os PEs.

## Validação e preservação

O contrato exige 36 testes por configuração em Debug e Release, sem ignorados.
A matriz soma 800 boots: 80 do novo ciclo de vida e 720 de regressão. Os 80 boots
novos executam 1.840 filhos, 800 falhas de orçamento e 80 controles de limpeza
antes da entrada. Permanecem os 240 cenários externos de falha anteriores e os
pares de persistência do runtime.

O teste nativo preserva 904 verificações de ciclo de vida e 693 de processos,
acrescentando 17 sobre abertura/permissões de thread, ID obsoleto, falha de
alocação de handle e resultado preservado. O serviço mantém suas 185 verificações
anteriores e acrescenta dez sobre limpeza por PID, falha com nova tentativa e
preservação dos handles do outro processo.

São executados os sete programas de referência por configuração, auditorias das
dez imagens de produção e provas da CLI, incluindo dois boots persistentes do
novo workload por configuração. Uma árvore independente,
`build-phase8-lifetime-clean`, recompila e audita as dez imagens. O build completo
contém 63 executáveis de ferramentas/testes, 12 imagens de kernel (dez de produção
e duas de falha) e seis PEs de referência em cada configuração.

O snapshot é `snapshots/codex-before-phase8-lifetime-20261017`, com 20 originais.
Novos logs e discos usam `phase8-lifetime-`. Os 520 arquivos históricos de
relatórios/logs e os discos dos quatro marcos anteriores são conferidos por
SHA-256. Boot, segurança e NX do Windows não são alterados.

Contrato: `config/phase8-lifetime-acceptance.json`.
Verificador: `tools/validate_phase8_lifetime.py`.
Manifesto: `build-codex/phase8-lifetime-change-manifest.json`.

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild *> build-codex\phase8-lifetime-full-debug-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-lifetime-debug-tests-details.log
.\Build-Kernel.ps1 -Configuration Release -FullBuild *> build-codex\phase8-lifetime-full-release-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-lifetime-release-tests-details.log
cmake -S . -B build-phase8-lifetime-clean -G "Visual Studio 17 2022" -A x64 *> build-codex\phase8-lifetime-clean-configure.log
cmake --build build-phase8-lifetime-clean --config Debug --target guardian_guest --parallel 2 *> build-codex\phase8-lifetime-clean-debug-build.log
python tools\validate_phase8_lifetime.py --snapshot snapshots\codex-before-phase8-lifetime-20261017 --run-programs
```

## Limites e trabalho restante

Há até três processos, quatro threads por processo e uma vCPU. Tamanhos de pilha,
heap, I/O e strings mantêm os limites anteriores. `CreateProcessA` aceita somente
o nome do filho no catálogo, linha de comando ASCII de até 127 bytes, startup de
104 bytes com apenas `cb` definido e flags 0 ou `CREATE_SUSPENDED`. Não há busca em
PATH, execução de binário externo, herança de handles, ambiente/diretório próprio,
redirecionamento de console, opções avançadas ou suporte Unicode declarados.

Não há suporte declarado a saída recursiva durante callbacks de DLL, cancelamento
completo de I/O por thread ou código arbitrário. A validação de término com falhas
de callback, threads em todos os estados e outros erros de inicialização continua
na matriz ampliada. Faltam dispatch de exceções e unwind x64 de usuário, carga e
descarga dinâmica de DLLs e a auditoria final por aplicação para concluir a fase 8.
A fase 9 só poderá começar depois dessa conclusão e da parada solicitada.

Contratos de referência: [CreateProcessA](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa),
[ExitThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitthread),
[ExitProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess) e
[GetExitCodeProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess).
