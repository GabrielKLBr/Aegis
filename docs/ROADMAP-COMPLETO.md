# Plano de desenvolvimento do GuardianAV e do kernel próprio

Data de referência: 26/10/2026.
Base ativa: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.

**Infraestrutura Windows solicitada em 28/10/2026:** controles independentes das
engines por serviços reais, com análise em processos restritos do usuário.
[Escopo, uso e validação](WINDOWS_ENGINE_SERVICES.md). Esta entrega é separada
dos frameworks da 14.11 e não os declara concluídos.

**Revisão Windows atual:** a continuação da 12B até a 13 passou na matriz
MSVC/WHP: 80 testes distintos Debug, com retestes documentados, e 80/80 Release
em uma suíte completa, sem ignorados. [Correções, evidências e limites](REVISAO-WINDOWS-FASES12B-13-20261023.md).
A integração PE/SMP da 12B e as referências NT específicas da 12C continuam
abertas. A fase 14 foi entregue no Windows em 23/10/2026; sua aceitação Linux é
parcial. As subfases 14.1–14.14 abaixo passam a ser a próxima prioridade de
compatibilidade, antes da fase 15. A implementação da 14.1 e seu escopo de
aceitação estão registrados em [PHASE-14.1.md](PHASE-14.1.md). A 14.2 amplia o
carregador externo e o volume de runtime: [escopo e aceitação](PHASE-14.2.md).
A 14.3 acrescenta serviços nativos e uma matriz limitada com NTDLL própria e
original Win10: [implementação, validação e pendências](PHASE-14.3.md).
A 14.4 implementa a matriz Win32 própria e leitura de schemas API set:
[contratos, testes e limites](PHASE-14.4.md). A 14.5 acrescenta o corpus C/C++
com runtimes originais no convidado: [implementação e limites](PHASE-14.5.md).
A 14.6 acrescenta registro persistente, tokens/ACLs e serviços LocalService com
vida útil própria: [entrega delimitada, testes e limites](PHASE-14.6.md).
14.7 tem implementação e aceitação gráfica próprias, descritas em [PHASE-14.7.md](PHASE-14.7.md).
A 14.8 acrescenta o lote delimitado de COM/RPC, Shell, propriedades e controles
descrito em [PHASE-14.8.md](PHASE-14.8.md). A 14.9 acrescenta rede contida, HTTP, TLS e criptografia conforme [PHASE-14.9.md](PHASE-14.9.md). A 12F acrescenta apresentação em janelas nativas conforme [PHASE-12F.md](PHASE-12F.md). A 14.10A acrescenta a ponte nativa delimitada de [PHASE-14.10A.md](PHASE-14.10A.md); 14.10B e C têm entregas delimitadas em [PHASE-14.10B.md](PHASE-14.10B.md) e [PHASE-14.10C.md](PHASE-14.10C.md); 14.10D tem entrega delimitada em [PHASE-14.10D.md](PHASE-14.10D.md); 14.10E tem entrega delimitada em [PHASE-14.10E.md](PHASE-14.10E.md); 14.10F tem entrega delimitada em [PHASE-14.10F.md](PHASE-14.10F.md); 14.10G tem o recorte PCM/WinMM/WASAPI descrito em [PHASE-14.10G.md](PHASE-14.10G.md); mídia comprimida e 14.11–14.14 continuam planejadas. Os registros abaixo preservam o histórico das
entregas anteriores e suas pendências.

**Prioridade gráfica revisada:** a 12F apresenta pixels desenhados no MiniNT.
A renderização de operações GUI pelas APIs reais do Windows host fica planejada
para **14.10A–14.10E**, antes de DirectX em **14.10F** e mídia/áudio em **14.10G**.
Por solicitação do usuário, a 14.10E prioriza ampliar a compatibilidade GDI
em janelas do host; DirectX e mídia/áudio avançam uma etapa.
As 14.10A–14.10E têm entregas delimitadas em [PHASE-14.10A.md](PHASE-14.10A.md),
[PHASE-14.10B.md](PHASE-14.10B.md) e [PHASE-14.10C.md](PHASE-14.10C.md) e [PHASE-14.10D.md](PHASE-14.10D.md) e [PHASE-14.10E.md](PHASE-14.10E.md); F–G têm entregas delimitadas em [PHASE-14.10F.md](PHASE-14.10F.md) e [PHASE-14.10G.md](PHASE-14.10G.md). Cada lote exige entrega e solicitação próprias; concluir um não autoriza iniciar o seguinte.

**Histórico das entregas:** 12A concluída para a matriz de referência, com validação Windows/WHP e Linux/KVM. [Resultados e limites](PHASE-12A.md). A 12B está em implementação e validação: [recursos, cotas e pendências](PHASE-12B.md). O contrato próprio `resources` passou na matriz Linux (47 Debug, 47 Release e 30 ASan/UBSan), com SMP, volume escalável e pasta independente; a aceitação Windows da revisão e a integração aos perfis existentes continuam pendentes. [Continuação de 20/10](PHASE-12B-RECURSOS.md). A 12C passou na validação Linux em 21/10/2026 (50 Debug, 50 Release e 32 ASan/UBSan), com referências Windows ainda pendentes: [matriz, entrega e diferenças](PHASE-12C.md). A 12D foi autorizada em nova solicitação: sessões e interação passaram em 53 testes Debug, 53 Release e 33 ASan/UBSan na revisão estável da 12D. [Registro da 12D](PHASE-12D.md). A 12E foi implementada e aceita no Linux em 22/10/2026: 56 Debug, 56 Release e 34 ASan/UBSan, com perfis sintéticos funcionais e reproduzíveis. [Entrega e limites](PHASE-12E.md). A aceitação nativa Windows e os requisitos anteriores permanecem pendentes. A fase 13 passou na aceitação Linux em 22/10/2026: 59 Debug, 59 Release e 35 ASan/UBSan, com telemetria, políticas e análise comportamental. [Entrega e limites](PHASE-13.md). A aceitação Windows e os requisitos anteriores continuam abertos; a fase 14 não foi iniciada. Os registros históricos abaixo conservam os resultados e pendências da época.

**Progresso:** [fase 0](PHASE-0.md), [fase 1](PHASE-1.md),
[fase 2](PHASE-2.md), [fase 3](PHASE-3.md), [fase 4](PHASE-4.md) e
[fase 5](PHASE-5.md), [fase 6](PHASE-6.md) e [fase 7](PHASE-7.md) concluídas. O kernel próprio executa
código em CPL0 e CPL3, trata exceções, atende syscalls, recebe interrupções e
gerencia memória, threads com preempção, processos, handles, objetos e direitos
dentro do convidado, além de drivers internos e I/O assíncrono para console,
mensagens e blocos, arquivos e registro persistentes em um volume próprio com
overlays. Validação final: 784 verificações do volume, 32 da persistência no
host, 80 boots da fase 7, outros 480 de regressão das fases 1–6, 31 testes e
sete exemplos por configuração em Debug e Release, sem testes ignorados.
**Fase 8: implementação da matriz de consoles próprios concluída;
aceitação nativa Windows pendente.** Os marcos anteriores cobrem loader,
runtime, threads, filhos, encerramento explícito e unwind virtual. A continuação
em 18/10 acrescentou despacho/continuação de exceções em CPL3, unwind não local,
DLLs dinâmicas com TLS/referências/rollback e a auditoria de comportamento por
aplicação. A matriz Linux aprovou 132 boots reais e 60 execuções CTest em
Debug, Release e Sanitize. Evidências, limites e retomada:
[PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md).

O produto permanece Windows/MSVC/WHP. A fase 8 não está formalmente encerrada:
faltam 40 CTests em cada configuração Windows, incluindo os boots WHP e
153 + 59 comparações nativas de unwind. O último marco aceito no Windows
continua sendo o quinto marco, registrado em [PHASE-8-UNWIND.md](PHASE-8-UNWIND.md).
A fase 9 foi autorizada posteriormente, em 18/10, e passou na aceitação Linux:
[PHASE-9.md](PHASE-9.md). A fase 10 foi autorizada posteriormente, também em 18/10. Implementação e aceitação Linux aprovadas; gate nativo Windows pendente: [PHASE-10.md](PHASE-10.md). A fase 11 foi autorizada em nova solicitação e passou na aceitação Linux: [PHASE-11.md](PHASE-11.md).

**Fluxo Linux autorizado em 18/10:** scanner, hash, inspeção, algoritmos,
recompilação dos convidados e executor KVM funcionam nativamente, sem Wine ou
QEMU. NX e acesso ao KVM foram confirmados após a mudança na BIOS. Comandos,
validação e diferenças de proteção em [LINUX-NATIVE.md](LINUX-NATIVE.md) e
[LINUX-KVM.md](LINUX-KVM.md).

Este plano segue as decisões posteriores ao pedido original: aproveitar esta
base, usar WHP diretamente, não usar QEMU e priorizar o kernel básico antes de
ampliar a compatibilidade e finalizar a interface do antivírus.

## Estado de partida

O MiniNt atual implementa no host heap, regiões virtuais com permissões WHP,
handles tipados, threads cooperativas, eventos, mutexes e semáforos. A última
validação aprovou dez testes e sete exemplos em Debug e Release; o build
completo em Release produziu 41 executáveis. Esses resultados cobrem os casos
exercitados, não constituem uma medição de compatibilidade geral com Windows.

Existem também scanner por assinaturas, monitor de diretório, quarentena,
interface Direct2D, carregadores de executáveis, modelos de disco e snapshots
parciais. Cada componente deverá ser integrado e validado no contexto do produto.

O MiniNt ainda executa a lógica de kernel no processo host. Os fundamentos
testados serão referência e, quando adequado, código reaproveitado para um
kernel que execute dentro do convidado. A migração exige substituir dependências
do host e adaptar memória, interrupções, sincronização e vida útil dos objetos.

## Arquitetura de destino

- **Host Windows:** serviço de proteção, scanner, controle do sandbox, VMM WHP,
  armazenamento do produto e interface WinUI 3.
- **Convidado:** kernel próprio, processos, memória, drivers, arquivos, registro,
  runtime compatível e aplicações analisadas.
- **Fronteira entre ambos:** dispositivos virtuais e protocolo explícito,
  versionado, validado e com limites de recursos.

O WHP continua sendo o mecanismo de virtualização do host. Kernel próprio no
convidado não exige reescrever também o hipervisor. A primeira plataforma será
x64, uma vCPU e dispositivos virtuais definidos pelo projeto. Inicialização em
hardware físico constitui uma plataforma adicional, com firmware e drivers
próprios; não é requisito para executar o kernel dentro deste sandbox.

## Como concluir uma fase

Uma fase só fica concluída quando seu comportamento implementado é demonstrado
por testes, os testes anteriores relevantes continuam passando, a integração
compila em Debug e Release e suas limitações estão documentadas. O teste deve
exercitar o comportamento final, incluindo falhas e falta de recursos.

Botões, stubs, chamadas que apenas retornam sucesso e números de executáveis
compilados não são critérios de conclusão funcional. Uma funcionalidade sem
suporte deve produzir resultado explícito e consistente.

Cada fase termina com código revisável, registros de validação e cópia ou
versionamento recuperável dos arquivos anteriores. As evidências históricas
permanecem identificadas por etapa.

## Execução por etapas e parada obrigatória

Ao concluir **cada fase**, entregar mudanças, testes, resultados, limitações e
arquivos relevantes; em seguida, **parar e encerrar o turno**. Só iniciar a
próxima fase após nova solicitação do usuário. A regra também vale para cada
subfase 12A–12F e 14.1–14.14 abaixo. A autorização da fase corrente permite
concluir suas tarefas e correções, mas não autoriza avançar automaticamente para
a seguinte.

Um teste aprovado isoladamente não encerra uma fase cujo restante está pendente.
Se houver bloqueio real, registrar o ponto exato e não declarar conclusão.
Esta regra foi registrada antes da implementação. O progresso de cada fase é
informado no início deste documento; não inicia uma fase seguinte automaticamente.

**Versionamento autorizado em 24/10/2026:** ao concluir cada nova fase ou
subfase, revisar, fazer um commit identificado pela etapa e enviá-lo ao
`GabrielKLBr/Aegis` usando `tools/aegis_git.py`, sem nova confirmação. Conferir
o commit remoto antes de encerrar a entrega. A regra permanente está em
[AGENTS.md](../AGENTS.md); builds continuam fora do repositório público.

## Realismo do ambiente e avaliação de detecção de sandbox

Objetivo acrescentado: aumentar a fidelidade do ambiente e reduzir diferenças
observáveis por aplicações, preservando o isolamento. Hardware, drivers,
recursos, tempo, arquivos e registro devem descrever o mesmo ambiente.

Perfis poderão tomar como referência modelos e capacidades não sensíveis do
host. Identificadores únicos serão substituídos por valores virtuais estáveis
por perfil. A identidade será apresentada exclusivamente no convidado:
inicialmente no MiniNt e, após a migração, também no kernel e nos dispositivos
convidados. Todos esses caminhos devem concordar. Não alterar a identidade do
Windows host nem expor seus números de série, credenciais ou arquivos pessoais.

Uma identidade de CPU/GPU não implementa instruções, firmware, comandos ou
interfaces do dispositivo. Separar identidade configurável, dispositivo
implementado e driver validado. Não prometer suporte a qualquer driver nem
sandbox indetectável. As estruturas necessárias serão previstas nas fases
iniciais, com implementação e validação nas subfases 12A–12E.

## Fase 0 — Consolidar arquitetura, escopo e compilação

**Trabalho:** inventariar os módulos existentes e suas dependências; separar
formalmente host, VMM e convidado; definir ABI de boot, dispositivos e mensagens.
Preparar alvos distintos para código Windows e código freestanding do kernel.
Planejar a migração do código nativo aplicável de C++17 para C++20 e registrar
as restrições de runtime do kernel. Definir aplicações de referência, versões
alvo e medidas de CPU, memória, disco e desempenho que serão usadas na aceitação.

**Conclusão:** arquitetura e matriz inicial registradas; builds reproduzíveis;
suíte atual preservada; todo recurso identificado como existente, parcial ou
pendente. Resultados desta etapa: [PHASE-0.md](PHASE-0.md).

## Fase 1 — Inicialização do kernel próprio no convidado

**Trabalho:** gerar imagem do kernel; estabelecer ponto de entrada, mapa de
memória, pilha inicial e passagem de parâmetros; carregar e iniciar essa imagem
pelo VMM existente. Implementar console de diagnóstico, identificação da versão,
parada controlada e relatório de falha. Não depender do runtime Windows dentro
do kernel convidado.

**Conclusão:** iniciar repetidamente o kernel próprio em WHP, executar uma rotina
de autoteste e encerrar com resultado observável pelo host. Imagens e parâmetros
inválidos devem ser recusados de forma controlada.

**Resultado:** concluída em 16/10/2026. Implementação, evidências, comandos e
limitações: [PHASE-1.md](PHASE-1.md). A execução foi encerrada nesta fase.

## Fase 2 — CPU, exceções, interrupções e relógio

**Trabalho:** implementar descritores e estados de CPU, tabelas de interrupção,
tratamento de faults e transição entre kernel e usuário. Preparar entrada de
syscalls e retorno, temporizador virtual e relógio monotônico. Registrar estado
de CPU e causa de falha sem depender de sucesso simulado no host.

**Conclusão:** um programa de usuário chama o kernel e retorna; faults de CPU
têm destino definido; o temporizador gera interrupções verificáveis; prazos e
leituras de tempo são consistentes. Falha de uma aplicação não pode derrubar
silenciosamente o kernel.

**Resultado:** concluída em 16/10/2026. Implementação, evidências, comandos e
limitações: [PHASE-2.md](PHASE-2.md). A execução foi encerrada nesta fase.

## Fase 3 — Memória gerenciada pelo kernel convidado

**Trabalho:** implementar alocação de páginas físicas e tabelas de páginas do
convidado; separar espaços de kernel e usuário; implementar reserva, commit,
decommit, liberação, consulta e proteção. Adaptar os alocadores existentes,
acrescentar guard pages, tratamento de page faults e contabilização de recursos.
Manter separadas as permissões de páginas do convidado e as proteções WHP.

**Conclusão:** acesso inválido é bloqueado, memória do kernel não é acessível
ao usuário, falhas de alocação preservam o estado e ciclos de alocação/liberação
não deixam vazamentos. Testes do modelo atual servem como referência semântica.

**Resultado:** concluída em 16/10/2026. Implementação, evidências, comandos e
limitações: [PHASE-3.md](PHASE-3.md). A execução foi encerrada nesta fase.

## Fase 4 — Threads, preempção e sincronização no convidado

**Trabalho:** portar criação e término de threads, troca de contexto e estados
de espera. Implementar preempção em uma vCPU, prioridades básicas, suspensão,
eventos, mutexes e semáforos. Completar esperas múltiplas, timers, alertas/APCs,
critical sections e SRW locks; retirar respostas provisórias desses caminhos.
Preservar todo o estado de CPU exigido pelas capacidades anunciadas.

**Conclusão:** várias threads progridem mesmo quando uma delas não faz syscalls;
esperas e abandono têm resultados corretos; corridas, timeouts e cancelamentos
são testados. Nenhum lock necessário à aplicação pode permanecer um no-op.

**Resultado:** concluída em 16/10/2026. Implementação, evidências, comandos e
limitações: [PHASE-4.md](PHASE-4.md). A execução foi encerrada nesta fase.

## Fase 5 — Processos, objetos e modelo de segurança

**Trabalho:** criar e terminar processos com espaços de memória independentes;
integrar handles, duplicação, herança e referências. Implementar tokens,
identidades, direitos de acesso e descritores de segurança. Acrescentar namespace
de objetos com nome, limites por processo e limpeza de processos incompletos.

**Conclusão:** processos não acessam memória ou objetos alheios sem autorização;
direitos são aplicados; falha ou término de um processo libera seus recursos
sem corromper os demais. Processos filhos funcionam com herança controlada.

**Resultado:** concluída em 16/10/2026. Implementação, evidências, comandos e
limitações: [PHASE-5.md](PHASE-5.md). A execução foi encerrada nesta fase.

## Fase 6 — Infraestrutura de I/O, dispositivos e drivers

**Trabalho:** definir barramento e contratos de dispositivos virtuais; preparar
gerenciador de I/O, requisições, conclusão assíncrona, cancelamento, carregamento
e descarregamento de drivers próprios. Implementar inicialmente console, canal
host/convidado e armazenamento. Validar filas, tamanhos e endereços em ambos
os lados da fronteira.

**Conclusão:** drivers próprios atendem operações reais e sobrevivem a entradas
inválidas, cancelamento e falhas de dispositivo. Um driver incompatível produz
diagnóstico. Suporte a drivers Windows de terceiros depende também da fase 12.

**Resultado:** concluída em 16/10/2026 com drivers internos versionados e
armazenamento volátil. Implementação, evidências, comandos e limitações:
[PHASE-6.md](PHASE-6.md). A execução foi encerrada nesta fase.

## Fase 7 — Disco, sistema de arquivos e registro persistente

**Trabalho:** integrar armazenamento virtual persistente e overlays; implementar
arquivos, diretórios, leitura, escrita, renomeação, exclusão, compartilhamento,
permissões, flush e recuperação. Completar registro virtual com tipos, raízes,
enumeração, exclusão, notificações e permissões. Tratar caminhos, links e
separação entre volumes convidados e armazenamento do host.

**Conclusão:** dados persistem após reinício; operações negadas não alteram
estado; falhas durante escrita têm comportamento definido; mudanças chegam
somente ao armazenamento autorizado do convidado. Nenhum caminho recebido de
uma aplicação vira acesso arbitrário ao sistema de arquivos do host.

**Resultado:** concluída em 16/10/2026 com formato próprio limitado, overlays
persistentes e snapshots alternados para recuperação. Implementação,
evidências, comandos e limitações: [PHASE-7.md](PHASE-7.md).
A execução foi encerrada nesta fase.

## Fase 8 — Loader, runtime e aplicações de console

**Estado: em andamento.** Primeiro marco implementado: loader no convidado e
console próprio com DLL/TLS; validação e limites em [PHASE-8.md](PHASE-8.md).
O segundo marco adiciona APIs de arquivos, registro e console, heap e locks;
validação e limites em [PHASE-8-RUNTIME.md](PHASE-8-RUNTIME.md).
O terceiro marco implementa criação e término por retorno de threads PE, com
TEB/TLS privados e contenção; critérios e limites em [PHASE-8-THREADS.md](PHASE-8-THREADS.md).
O quarto marco acrescenta filhos PE e encerramento explícito de processos e
threads, incluindo a continuação sem a thread principal. Critérios e limites
em [PHASE-8-LIFETIME.md](PHASE-8-LIFETIME.md).
O quinto marco executa unwind virtual v1 e captura de chamadas em pilhas
privadas; não chama handlers nem retoma contextos. Critérios e limites
em [PHASE-8-UNWIND.md](PHASE-8-UNWIND.md).
A continuação implementa despacho/retorno de exceções, unwind não local,
DLLs dinâmicas e a matriz de falhas. A aceitação Linux foi aprovada; a conclusão
formal depende da matriz nativa Windows desta revisão. Critérios e evidências
em [PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md). A fase 9 foi autorizada posteriormente; ver [PHASE-9.md](PHASE-9.md).

**Trabalho:** completar carregamento PE, relocations, imports, DLLs, TLS,
inicialização de processo e thread, PEB/TEB, exceções e unwind. Implementar o
conjunto NT/Win32 necessário à matriz inicial e integrar heap e locks reais.
Remover dependências de patches provisórios nos caminhos declarados suportados.

**Conclusão:** aplicações de console selecionadas iniciam e terminam corretamente,
carregam DLLs, criam filhos e threads, tratam exceções e usam arquivos e registro.
Cada aplicação deve ter critérios de comportamento, não apenas retornar sem crash.

## Fase 9 — Snapshots completos e gerenciamento de VMs

**Estado em 18/10/2026: implementação e aceitação Linux concluídas; aceitação
Windows pendente.**
Iniciada por nova solicitação explícita do usuário, preservando a pendência de
aceitação Windows da fase 8. Inclui snapshots, ciclo de vida, clonagem,
persistência, recuperação e VMs simultâneas. Critérios e evidências atuais:
[PHASE-9.md](PHASE-9.md). Não iniciar a fase 10 automaticamente.

**Trabalho:** capturar e restaurar CPU, memória, mapeamentos, escalonador,
objetos, relógios, dispositivos, arquivos, registro e disco de forma consistente.
Implementar criar, iniciar, pausar, reiniciar, desligar, clonar e excluir VMs;
configurar recursos e importar imagens suportadas. Distinguir ambientes
descartáveis e persistentes e recuperar operações interrompidas.

**Conclusão:** restaurar um snapshot devolve os recursos ao mesmo estado lógico;
ambiente descartável não altera sua base; limpeza e clonagem não atingem VMs
alheias. Execução simultânea de várias VMs respeita seus limites e identidades.

## Fase 10 — Rede virtual e contenção

**Estado em 18/10/2026:** implementação e aceitação Linux aprovadas, com 81 execuções CTest e 132 boots; gate nativo Windows pendente. Perfil e limites em [PHASE-10.md](PHASE-10.md). A fase 11 foi autorizada posteriormente em nova solicitação.

**Trabalho:** implementar dispositivo e pilha de rede do convidado, modo sem
rede, serviços simulados e rede isolada de laboratório. Adicionar captura de
tráfego, DNS e políticas por VM. Acesso externo por proxy deverá ser uma opção
explícita, com destinos e permissões definidos.

**Conclusão:** cada perfil permite exatamente o tráfego declarado; LAN e serviços
privados do host permanecem bloqueados por padrão; eventos e falhas são
registrados. Testes verificam também IPv6 e outros caminhos de saída suportados.

## Fase 11 — Gráficos, entrada e janelas de aplicações

**Estado em 18/10/2026:** autorizada explicitamente pelo usuário após a aceitação Linux
da fase 10. Implementação da matriz própria e aceitação Linux aprovadas: 96 execuções
CTest e 132 boots de regressão; gate nativo Windows pendente. Evidências e limites em [PHASE-11.md](PHASE-11.md). A fase 12 não foi iniciada.

**Trabalho:** implementar saída gráfica e entrada virtuais, gerenciador de
janelas e APIs gráficas da matriz inicial. Transferir pixels e metadados
validados ao host, encaminhar teclado/mouse e tratar foco, cursores, diálogos,
DPI e múltiplas janelas. Introduzir aceleração somente conforme capacidades
realmente implementadas.

**Conclusão:** aplicações gráficas selecionadas desenham e respondem à interação;
seus eventos não interferem com janelas reais fora do canal autorizado.
Clipboard, arrastar arquivos, impressoras, USB e unidades compartilhadas têm
permissões independentes, inicialmente desativadas.

## Fase 12 — Compatibilidade ampliada, SMP e dispositivos adicionais

**Trabalho:** ampliar APIs e comportamento por perfil, incluindo as diferenças
pretendidas de XP, Vista, 7, 8, 8.1, 10 e 11. Desenvolver e validar suporte x86
quando necessário; um perfil JSON não substitui suporte real à arquitetura.
Acrescentar múltiplas vCPUs e sincronização SMP, firmware/plataformas previstas,
áudio, classes adicionais de dispositivos e interfaces necessárias a drivers
Windows selecionados. Tratar requisitos de TPM e GPU por capacidade efetiva.

**Conclusão:** matriz por versão, arquitetura, aplicação, driver e dispositivo
com estados aprovado, parcial ou sem suporte. Instalação de drivers no convidado
não instala drivers no host. Compatibilidade anunciada corresponde às evidências;
não há presunção de que qualquer aplicativo ou driver Windows funcionará.

### Matriz adicional de aplicações da fase 12

O Flappy Window 1.5 fornecido pelo usuário passa a ser uma aplicação candidata
para um lote explícito de compatibilidade. O arquivo avaliado tem SHA-256
`f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6`.
Seu teste inicial no microsandbox não abriu o jogo: houve 167 importações
não implementadas e exceção #UD durante a inicialização, cuja causa exata
permanece por investigar.

O lote correspondente precisa integrar carregamento e inicialização de PEs
externos ao kernel próprio, runtime C, dependências, exceções, threads e as
operações USER32/GDI/DWM realmente utilizadas. Contar funções reconhecidas ou
retornar sucesso de stubs não comprova compatibilidade. O critério do jogo
exige abrir a janela, desenhar, receber entrada exclusivamente no convidado,
executar a partida, ler suas configurações no disco virtual e encerrar de
forma reproduzível. Esse lote não está concluído pela primeira entrega da 12A.

**Avanço em 23/10/2026:** o novo contrato experimental recebe um PE separado
do kernel e conecta um subconjunto de USER32 ao compositor do convidado.
A referência própria não encerra a aceitação do Flappy: limites de imagem,
runtime e APIs continuam pendentes. [Matriz, testes e avaliação da 12F](PHASE-12-GUI.md).

### Subfase 12A — Perfis coerentes de hardware e drivers

**Estado em 19/10/2026:** concluída para a matriz de referência. Identidade em CPUID, firmware e registro, importação INF e execução real do driver próprio no convidado, incluindo falhas e snapshots, foram validadas nos dois backends. Drivers comerciais, autenticação CAT e compatibilidade Windows geral não são declarados suportados. [Matriz, testes e limites](PHASE-12A.md). Parada realizada antes da 12B.

**Dependências:** fase 0 e infraestrutura das fases 6, 8, 11 e 12.

**Trabalho:** oferecer perfil baseado nos modelos/capacidades do host e editor
de CPU, GPU, disco, BIOS e placa-mãe virtuais. Permitir cadastrar modelos
personalizados, identificando o que está implementado. Centralizar informações
apresentadas por enumeração de dispositivos, firmware virtual, registro e
MiniNt/kernel convidado, com identificadores sintéticos persistidos no perfil.

Prever importação de pacotes de drivers selecionados do host ou fornecidos pelo
usuário para uso exclusivo no convidado. Verificar arquitetura, versão NT,
dependências e dispositivo virtual exigido. Registrar separadamente pacote
importado, driver instalado, inicializado, funcional ou incompatível.

Um perfil chamado GT 1030 só anunciará funcionamento com seu driver depois que
as interfaces e o comportamento necessários forem implementados e testados.
Até lá, mostrar suporte parcial. Selecionar o modelo ou importar o pacote não
garante instalação ou operação. Aplicar o mesmo critério aos outros dispositivos.

**Conclusão:** inventário coerente entre os caminhos implementados, ausência de
identificadores únicos reais do host e testes dos drivers declarados suportados.
Perfis incompatíveis têm diagnóstico. Parar ao concluir.

### Subfase 12B — RAM, núcleos e disco virtual com cota

**Dependências:** fases 3, 7 e 9; SMP da fase 12 quando solicitado.

**Trabalho:** permitir escolher RAM e vCPUs, incluindo uma configuração que use
a maior parcela disponível dos recursos do PC. Aplicar orçamento por VM e
agregado, mantendo reserva operacional para Windows, VMM e serviço. Exibir
valores solicitados, admitidos e provisionados. Recursos anunciados precisam
ter comportamento implementado; não prometer toda a RAM física enquanto o host
também precisa dela.

Permitir escolher pasta e limite de crescimento do disco virtual. Seu perfil
poderá usar modelo, fabricante e capacidade de referência do disco do host,
sem acessar o disco físico ou seus arquivos. Separar capacidade lógica, espaço
livre do volume virtual, disponível à cota e ocupação física do arquivo de
backing. Considerar também espaço real na unidade host, overlays e snapshots.

Ao atingir a cota, a disponibilidade para novas gravações deve chegar a zero e
as operações devem retornar erro de espaço/cota apropriado. Consultas e escritas
precisam concordar, inclusive após exclusão, reinício e restauração. Não mostrar
espaço utilizável ilimitado enquanto as operações falham por cota esgotada.

**Conclusão:** orçamentos e reservas respeitados, crescimento limitado e
tratamento recuperável de falta de espaço, sem esgotar silenciosamente o host.
Parar ao concluir.

### Subfase 12C — Fidelidade do comportamento do sistema

**Estado em 21/10/2026:** implementação e validação Linux aprovadas na pasta
original do HD: 50 Debug, 50 Release, 32 ASan/UBSan e 1.590 observações convidadas.
Faltam as referências Windows nativas por versão para encerramento formal.
A 12B mantém suas pendências de aceitação Windows e integração aos perfis PE.
[Matriz, evidências, entrega e diferenças](PHASE-12C.md).
Não iniciar a 12D por essa autorização.

**Dependências:** fases 2–8 e subfases 12A–12B.

**Trabalho:** criar testes de conformidade com programas próprios para tempo,
exceções, sincronização, memória, handles, I/O e resultados de APIs. Comparar
com ambientes de referência autorizados por arquitetura e versão NT. Priorizar
diferenças que mudam decisões das aplicações e retirar respostas provisórias
dos caminhos suportados. Conferir coerência entre capacidades anunciadas,
instruções executadas, topologia, dispositivos, registro e consultas.

Usar relógios consistentes para espera, tempo decorrido e dados compartilhados.
Cada divergência corrigida recebe teste de regressão; diferenças intencionais
ficam documentadas.

**Conclusão:** matriz de conformidade reproduzível, com diferenças corrigidas ou
declaradas e sem stubs nos comportamentos anunciados como completos. Parar ao
concluir.

### Subfase 12D — Sessões longas, reinicializações e interação

**Continuação autorizada em 21/10/2026:** sessões, suspensão, roteiros e interação manual implementados e aceitos no Linux: 139 execuções CTest aprovadas (53 Debug, 53 Release, 33 ASan/UBSan). Aceitação nativa Windows e pendências anteriores continuam abertas. [Resultados e limites](PHASE-12D.md).

**Dependências:** fases 2, 7, 9 e 11.

**Trabalho:** permitir duração configurável, continuidade por múltiplas sessões
e reinicializações, preservando estado de aplicação e persistência previstos
no perfil. Oferecer interação manual e roteiros de mouse/teclado enviados
exclusivamente às janelas do convidado.

Manter tempo real como referência inicial. Modos experimentais de tempo virtual
devem ser configuráveis, registrados e consistentes com os temporizadores e
relógios disponíveis. Não modificar o relógio do host nem concluir esperas
antecipadamente sem semântica definida. Medir o efeito desses modos na análise.

**Conclusão:** programas inofensivos que aguardam, retomam após reinício e
dependem de interação seguem o roteiro esperado; sessões são restauráveis e
entrada não alcança aplicativos do host. Parar ao concluir.

### Subfase 12E — Ambiente de uso e dados sintéticos

**Implementada e aceita no Linux em 22/10/2026:** 146 execuções CTest aprovadas, sem Wine; documentos abertos pelo leitor próprio, reprodução de seis pares NT/semente, evolução persistente e teste de privacidade. Aceitação nativa Windows e pendências anteriores continuam abertas. [Registro, entrega e limites](PHASE-12E.md). A fase 13 não foi iniciada.

**Dependências:** fases 7, 8, 9 e 11.

**Trabalho:** criar perfis versionados com contas, programas instalados,
documentos de teste e histórico gerado dentro do convidado. Arquivos, atalhos,
associações, registro e datas devem concordar com operações realmente
suportadas. Preparar cenários por versão NT, com semente reproduzível e
possibilidade de preservar a evolução da VM.

Documentos e históricos serão sintéticos, sem copiar automaticamente contas,
credenciais, navegação ou arquivos pessoais do host. Importações explícitas
seguem o canal e as permissões de transferência. Os documentos devem abrir e
os programas declarados instalados devem funcionar no perfil correspondente.

**Conclusão:** perfil reproduzível e funcional, informações consistentes e teste
de ausência de dados pessoais do host. Parar ao concluir.

### Subfase 12F — Janelas integradas à área de trabalho do Windows

**Entrega delimitada em 26/10/2026:** [PHASE-12F.md](PHASE-12F.md).
Janelas principais/diálogos do corpus Win10 x64 aparecem separadamente no
Windows, com programa, DLLs e callbacks no MiniNT. Imagens por janela,
protocolo validado, entrada por sessão, modalidade, geometria e DPI virtualizado.
A aceitação exige 139 testes Release, 64 Debug e pacote extraído validado.
O visualizador único continua como alternativa. O conteúdo é rasterizado pelo
MiniNT e apresentado como imagens; essa entrega não implementa o encaminhamento
de operações de desenho às APIs nativas do Windows. A evolução solicitada de
renderização nativa fica em **14.10A–14.10E**, separada da aceitação desta 12F.

**Dependências:** saída gráfica e entrada da fase 11, carregamento e
inicialização de PEs externos no kernel próprio e o lote USER32/GDI da fase 12
necessário às aplicações escolhidas. Ampliar DWM e outras APIs conforme a matriz
efetivamente utilizada, sem pressupor compatibilidade geral.

**Trabalho:** oferecer um modo opcional em que cada janela de uma aplicação
convidada apareça como janela nativa independente na área de trabalho do Windows.
O aplicativo e suas bibliotecas continuam executando no convidado. Um componente
de apresentação no host cria e mantém as janelas correspondentes, recebe pixels
e comandos gráficos tipados e validados e devolve eventos de teclado/mouse
somente à sessão e à janela convidada associadas.

A ponte traduz identificadores de janela, coordenadas, foco, tamanho, títulos,
DPI, fechamento e parentesco/modalidade. Identificadores reais do host e
ponteiros não atravessam a fronteira. Não haverá encaminhamento arbitrário de
chamadas USER32/GDI, mensagens Windows, callbacks ou código do convidado para
execução no host. Operações fora do contrato retornam erro explícito.

Aplicar limites de quantidade de janelas, dimensões, memória, comandos e taxa
de eventos; validar também textos, imagens e transições de estado. Restringir
operações às janelas pertencentes àquela sessão, sem alcançar outras aplicações,
injetar entrada global ou instalar hooks globais. Clipboard, arrastar arquivos
e demais transferências permanecem sujeitos a permissões independentes,
inicialmente desativadas. Prever indicação visual de origem no sandbox e
limpeza das janelas na falha, no encerramento ou na restauração da sessão.

**Conclusão:** uma aplicação própria e ao menos um PE externo da matriz
selecionada abrem janelas independentes, desenham e recebem entrada; validar
múltiplas janelas, foco, redimensionamento, DPI, diálogo modal e fechamento.
Testes com comandos malformados, identificadores de outra sessão, excesso de
recursos e encerramento inesperado devem comprovar recusa ou término contido,
sem entrada ou alterações em janelas externas. Registrar suporte por aplicação
e operação e preservar o visualizador atual como alternativa. Não declarar
aceleração GPU ou passthrough de GPU sem implementação e validação próprias.
Parar ao concluir, antes de iniciar outro lote ou fase.

## Fase 13 — Telemetria, políticas e análise comportamental

**Implementada e aceita no Linux em 22/10/2026:** 153 execuções CTest aprovadas, sem falhas ou ignorados. Nove domínios coletados, políticas e efeitos reais no convidado, regras, exportação e diferenças de estado. Aceitação nativa Windows e pendências anteriores continuam abertas. [Registro, entrega e limites](PHASE-13.md). A fase 14 não foi iniciada.

**Trabalho:** reunir processos, threads, módulos, arquivos, registro, serviços,
drivers, rede e tentativas bloqueadas em uma linha do tempo. Criar correlação e
regras versionadas para persistência, ransomware e outros comportamentos;
distinguir legítimo, suspeito, confirmado e inconclusivo. Integrar política de
permitir, negar ou simular efeitos coerentes exclusivamente no estado convidado.
Exportar relatórios JSON/HTML e diferenças de estado.

**Conclusão:** programas de teste próprios geram eventos e classificações
esperados, com autoria e motivo rastreáveis. Falhas ou perda de telemetria são
visíveis. Resultados recebidos da VM são tratados como entradas não confiáveis.

## Fase 14 — Scanner, regras e inteligência de ameaças

**Entrega de 23/10/2026:** implementação e testes próprios descritos em
[PHASE-14.md](PHASE-14.md); aceitação Windows: 83/83 Release e 3/3 Debug
do scanner, sem ignorados. Linux Debug: scanner/pipeline 2/2 e CLI 1/1,
com aceitação parcial. O resultado
`unknown` não libera arquivos, e instaladores/formas sem suporte permanecem
inconclusivos. A fase 15 aguarda nova autorização.

**Trabalho:** evoluir o scanner existente: SHA-256 e hashes auxiliares,
assinaturas, regras YARA com licença verificada, análise PE, scripts, instaladores
e arquivos compactados. Integrar conectores configuráveis para MalwareBazaar,
VirusTotal e CIRCL hashlookup, conforme suas APIs e condições vigentes.
Implementar SQLite, cache com validade, limites de requisição, modo offline e
classificação justificada. Encaminhar desconhecidos ao sandbox por política.

**Conclusão:** casos positivos inofensivos e corpus limpo têm resultados
reproduzíveis; erros, formatos não suportados e limites não são classificados
como arquivo seguro. Consultas não enviam arquivos completos sem consentimento
explícito. Arquivos compactados têm limites de expansão, profundidade e tempo.

## Fases 14.x — Compatibilidade de DLLs e serviços Windows por versão NT

**Prioridade definida em 23/10/2026:** executar esta sequência de compatibilidade
antes da fase 15, aproveitando o kernel, o runtime, os perfis e o protótipo GUI
existentes. A 14.1 foi implementada por solicitação posterior: [contratos,
inventário, testes e limitações](PHASE-14.1.md). A 14.2 foi implementada por nova
solicitação: [carregador externo, testes e limites](PHASE-14.2.md).
Cada subfase exige solicitação e termina com entrega e parada.
As pendências das fases 8–13 permanecem registradas; os lotes abaixo integram e
ampliam esses caminhos, sem declarar sua conclusão retroativamente.

### Arquitetura escolhida e critérios comuns

**Abordagem híbrida:** implementar no MiniNT os serviços e contratos compatíveis
necessários e reutilizar gradualmente DLLs originais de modo usuário, quando o
carregador e suas dependências forem comprovadamente suportados. Manter DLLs
próprias para contratos já implementados e como referência de teste. A seleção
de cada implementação será explícita no manifesto da VM, sem substituições
silenciosas. O objetivo é ampliar compatibilidade funcional, com possibilidade
de testes diferenciais entre implementações próprias e originais.

Aplicativos executam em processos separados dentro do convidado, em modo
usuário, com suas DLLs carregadas no respectivo processo e atendidos pelo kernel
MiniNT. O processo gerenciador no host controla a VM; ele não executa livremente
os imports do programa como chamadas Windows locais. A ponte gráfica planejada
em **14.10A–14.10E** terá uma lista explícita de operações GUI traduzidas para
APIs Windows em um processo auxiliar restrito. EXE, DLLs importadas e callbacks
da aplicação continuarão no convidado; não se trata de carregar seu código no
host ou encaminhar syscalls gerais. DLLs
originais não substituem os serviços do kernel, do subsistema gráfico ou de
outros processos de que dependem. Reutilizar os componentes existentes antes de
criar implementações paralelas.

Pacotes de DLLs terão procedência, arquitetura, versão de arquivo, build/revisão
de referência e hashes registrados, fornecidos de uma instalação ou mídia
autorizada pelo usuário. Sua validação e execução ocorrerão no convidado; não
carregar essas DLLs no gerenciador host para descobrir dependências. Evitar
misturar componentes de instalações diferentes; pacotes próprios/originais
mistos só serão aceitos com contratos e testes explícitos. Repositório e builds
do projeto não incluirão automaticamente binários Windows importados.

**Meta de cobertura:** buscar ampla execução de programas Windows de desktop,
começando por x64. Os nomes de DLL abaixo representam famílias prioritárias,
não uma garantia de que sua presença, ou todos os seus exports resolvidos,
faça funcionar a maioria dos programas. Cada lote manterá inventário por
DLL/símbolo/ordinal/contrato, versão NT e comportamento: ausente, parcial ou
validado. Uma implementação só será chamada de completa para um escopo
explicitamente delimitado e testado; funções sem suporte não retornarão sucesso
fictício. A cobertura integral de cada família continua como objetivo de longo
prazo, com novos lotes para os itens restantes.

Todos os lotes integram os novos efeitos às políticas e à telemetria existentes,
incluem entradas inválidas, falhas de recursos, concorrência, limpeza, regressões
relevantes e aceitação Windows Debug/Release. Comparações
nativas usam programas de teste inofensivos em referências da versão alvo;
aplicações externas são executadas exclusivamente no convidado. Evidências
identificam o que foi apenas inventariado, carregado, inicializado ou realmente
executado até cumprir seus critérios de uso.

### Subfase 14.1 — Contratos versionados e matriz de compatibilidade

**Implementação de 23/10/2026:** manifesto persistente com SHA-256, três alvos
x64, vínculo de sessão/snapshot e inventário estático/dinâmico observado de
dependências. Consultas de versão usam contrato próprio, sem afirmar ABI Windows
completa. Referências nativas dos builds exatos e novos serviços Windows
permanecem pendentes. [Aceitação e continuidade](PHASE-14.1.md).
Parar nesta subfase; a 14.2 exige nova solicitação.

**Trabalho:** consolidar o inventário existente das fases 8/12 e das aplicações
externas; registrar imports estáticos, dinâmicos, ordinais, exports de dados e
dependências transitivas. Definir o manifesto único do perfil NT e do pacote de
runtime, sua persistência e a matriz de testes antes de ampliar funções.

Usar inicialmente **Windows 10 x64, NT 10.0.19045**, como referência principal;
ampliar com **Windows 7 x64, NT 6.1.7601**, e **Windows 11 x64, NT 10.0.22631**,
já presentes nos perfis. Fixar também a revisão dos componentes utilizados.
Referências ausentes ficam pendentes, sem inferir conformidade de outra versão.
Windows 10 e 11 compartilham a versão NT 10.0; build, revisão e capacidades
fazem parte da seleção, não apenas major/minor.

O perfil deverá reger disponibilidade e assinaturas de APIs, resolução de
API sets, tabelas de syscalls, layouts utilizados, classes de informação,
NTSTATUS/LastError, tempos, sincronização, memória, arquivos e regras gráficas.
Conferir PEB/TEB, dados compartilhados, registro e consultas de versão, incluindo
as diferenças documentadas de GetVersionEx/VerifyVersionInfo conforme o
manifesto da aplicação. Não forçar todas as consultas a retornar a mesma string
quando o próprio Windows diferencia os contratos.

Fixar o perfil durante a sessão; trocar versão exige reinicialização com pacote
coerente. Snapshots e relatórios vinculam perfil, versão do contrato e hashes
dos módulos. Recusar combinações não suportadas. Os perfis de identidade XP,
Vista, 8 e 8.1 não passam a ter compatibilidade binária por já existirem no
inventário; sua promoção depende dos lotes de conformidade e da 14.12.

**Conclusão:** matriz e manifesto revisáveis, referências e lacunas explícitas,
ordem de símbolos por dependência e testes que distingam perfis, inclusive
recusa de combinações incompatíveis. Parar ao concluir.

### Subfase 14.2 — Carregador externo, módulos e memória escaláveis

**Implementação de 24/10/2026:** PE externo até 8 MiB, catálogo de até 16 módulos,
volume imutável convidado com busca controlada, forwarders, ordinais/dados,
delay imports antecipados, recursos numéricos e manifests/contextos delimitados.
Testes com DLLs próprias e três perfis NT; serviços de DLLs Windows originais
continuam pendentes. [Contrato, aceitação e limites](PHASE-14.2.md).
Parar nesta subfase; a 14.3 exige nova solicitação.

**Dependências:** 14.1 e fundamentos de memória/PE das fases 3/8/12B.

**Trabalho:** superar os limites do protótipo de arquivo/imagem de 128 KiB,
quatro módulos e orçamento de páginas privadas; dimensionar metadados, pilhas,
TLS e cotas juntos. Carregar EXE e DLLs do volume convidado com busca controlada,
forwarders, API sets por perfil, imports por nome/ordinal e de dados, delay
imports, recursos, manifests e contextos de ativação conforme a matriz.
Completar LoadLibrary/FreeLibrary/GetProcAddress, ordem de DllMain/TLS,
referências, loader lock, rollback e proteção das seções.

**Conclusão:** aplicações e bibliotecas externas maiores, com dependências já
atendidas pelo runtime existente, iniciam em CPL3; falhas de inicialização ou
limites liberam os recursos. Testar o carregador com bibliotecas independentes;
a aceitação de DLLs Windows originais que exigem novos serviços pertence às
subfases seguintes. Não basta mapear o PE. Parar ao concluir.

### Subfase 14.3 — NTDLL e serviços nativos do MiniNT

**Estado em 24/10/2026:** implementada para o recorte documentado em
[PHASE-14.3.md](PHASE-14.3.md): 48 serviços nativos com argumentos limitados,
RTL/Ldr próprios e ensaio de uma NTDLL original Win10. Inicialização completa
da NTDLL, todos os seus exports e referências nativas exatas por versão continuam
pendentes; não declarar compatibilidade Windows geral. Parar nesta subfase.

**Dependências:** 14.2; reaproveitar processos, memória, objetos e runtime atuais.

**Trabalho:** ampliar os contratos Nt/Zw, Rtl e Ldr exigidos por `ntdll.dll`:
memória virtual e seções, objetos/handles, processos/threads, esperas/timers/APCs,
consulta de informações, arquivos/registro, heap, TLS/FLS, exceções e unwind.
Ligar chamadas nativas ao estado real do kernel convidado; tabelas e estruturas
dependentes de arquitetura/build pertencem ao perfil validado, sem reutilizar a
tabela do host. Bibliotecas próprias e originais devem chegar aos mesmos
serviços convidados. Validar ponteiros, tamanhos, direitos e transições de modo.

**Conclusão:** inicialização e rotinas nativas da matriz funcionam com resultados
comparados à referência, inclusive exceções e efeitos sobre memória/objetos;
chamadas desconhecidas têm resultado explícito e telemetria. Parar ao concluir.

### Subfase 14.4 — KERNEL32, KERNELBASE e contratos básicos Win32

**Estado em 24/10/2026:** implementada para a matriz própria documentada em
[PHASE-14.4.md](PHASE-14.4.md): console, threads/pool, filhos externos isolados,
FS compartilhado, mapeamentos, I/O assíncrono, ambiente, recursos, Unicode e
API sets v2/v6. KERNEL32/KERNELBASE originais, compatibilidade Windows geral,
NLS completo e cotas maiores de arquivos permanecem pendentes. Parar nesta
subfase; a 14.5 exige nova solicitação.

**Dependências:** 14.3.

**Trabalho:** implementar e/ou viabilizar `kernel32.dll`, `kernelbase.dll` e
seus contratos `api-ms-win-core-*`/`ext-ms-*` efetivamente presentes no perfil.
Cobrir heap/memória, processos e filhos, threads/thread pool, sincronização,
console, arquivos/mapeamentos/I/O assíncrono, ambiente, caminhos, recursos,
tempo, Unicode/NLS, módulos, tratamento de erros e informações do sistema.
Resolver contratos API set pelo schema da versão, sem presumir um arquivo
físico para cada nome importado.

**Conclusão:** programas externos de console usam threads, filhos, arquivos e
exceções de ponta a ponta; aliases e forwarders preservam o comportamento e os
erros da versão alvo. Parar ao concluir.

### Subfase 14.5 — Runtime C/C++ e inicialização de aplicações

**Entrega delimitada:** [PHASE-14.5.md](PHASE-14.5.md), MSVC 19.44 `/MD`,
FH3/FH4, CRT original Win10/VC143 e recusas documentadas. Não significa
compatibilidade CRT geral; rethrow/nested C++ permanecem pendentes.

**Dependências:** 14.4 e suporte de exceções da 14.3.

**Trabalho:** atender `msvcrt.dll`, `ucrtbase.dll`, `api-ms-win-crt-*`,
`vcruntime140*.dll`, `msvcp140*.dll` e versões adicionais exigidas pelo corpus.
Cobrir exports de dados, startup/terminação, argv/ambiente, stdio/arquivos,
alocação, locale/Unicode, matemática, inicializadores, TLS, exceções C++ e
integração com unwind/locks. Distinguir versões de runtime da versão do NT e
verificar requisitos dos redistribuíveis antes de aceitar seu pacote.

**Conclusão:** binários externos de toolchains selecionadas iniciam, usam CRT e
C++, tratam erros/exceções e encerram com resultados e recursos corretos;
reutilização de runtime original é validada no convidado. Parar ao concluir.

### Subfase 14.6 — ADVAPI32, segurança, registro e serviços

**Entrega delimitada:** [PHASE-14.6.md](PHASE-14.6.md). DLLs próprias
ADVAPI32/SECHOST/USERENV no perfil Win10 x64, configurações reabertas em outra
sessão, tokens retidos pelo kernel, DACLs canônicas e serviço de teste sob
LocalService. O SCM preserva seu processo após a saída do criador e recolhe
recursos após falhas. A matriz não equivale às DLLs Windows completas; logon,
impersonação, ACLs gerais, RPC, autostart e outros perfis continuam pendentes.

**Dependências:** 14.4; integrar persistência e segurança das fases 5/7.

**Trabalho:** ampliar `advapi32.dll` e, quando aplicável, `sechost.dll`,
`userenv.dll` e contratos relacionados: registro, tokens/SIDs/ACLs, permissões,
perfis de conta, serviço de eventos e gerenciador de serviços do convidado.
Implementar dependências de IPC necessárias a esses cenários; processos de
serviço têm identidade e vida útil próprias dentro da VM.

**Conclusão:** aplicações externas persistem configurações e executam um serviço
de teste no convidado com autorização real e isolamento entre contas; referências
de handles e recursos sobrevivem a falhas de forma consistente. Parar ao concluir.

### Subfase 14.7 — USER32, GDI e compatibilidade gráfica externa

**Entrega Windows:** runtime gráfico próprio, visualizador único e corpus de aceitação
com o Flappy original, aplicações MSVC, configurações persistentes e entrada física/texto
separadas. A aceitação exige 116 regressões Release e 41 verificações Debug, além do
pacote validado. [Implementação, evidências e limites](PHASE-14.7.md). A 12F usa essa base na entrega delimitada descrita em [PHASE-12F.md](PHASE-12F.md).

**Dependências:** 14.2–14.6 e gráficos/entrada existentes; promover as pendências
do lote gráfico da fase 12 para esta entrega, preservando seu histórico.

**Trabalho:** ampliar `user32.dll`, `gdi32.dll`, `gdi32full.dll`, `win32u.dll`
onde existirem no perfil, e `dwmapi.dll`. Implementar o suporte correspondente
no subsistema gráfico do MiniNT: criação/hierarquia de janelas, estilos,
movimentação/redimensionamento, coordenadas/DPI, foco, mensagens síncronas e
assíncronas, pintura/invalidação, fontes/texto, objetos GDI e temporizadores.
Entrada terá teclas físicas pressionadas/soltas, modificadores, texto e mouse
com contratos distintos. Integrar configurações e arquivos ao disco convidado;
ampliar dimensões/cotas do visualizador conforme os requisitos das aplicações.

**Conclusão:** o Flappy Window 1.5 fornecido pelo usuário abre, desenha texto,
recebe os comandos necessários para jogar, movimenta suas janelas, lê/grava
configurações quando aplicável e encerra de modo reproduzível, além de outras
aplicações independentes da matriz. Conferir regressões e isolamento; não
confundir resolução dos 178 imports inventariados com execução funcional.
Reavaliar e registrar então os pré-requisitos da **12F**, que continua sendo
uma entrega separada, dependente de solicitação. Parar ao concluir.

### Subfase 14.8 — COM, RPC, Shell e controles de desktop

**Entrega delimitada:** [PHASE-14.8.md](PHASE-14.8.md). Factories em DLL e
processo separado, STA/MTA, marshaling de IDispatch, automação, RPC local,
propriedades, controles e diálogos próprios. A matriz exige regressão Release,
aceitação Debug e execução do pacote nas duas configurações. Não representa
COM/NDR completos nem ativa serviços do host. A 12F continua separada.

**Dependências:** 14.6–14.7.

**Trabalho:** atender `ole32.dll`, `combase.dll`, `oleaut32.dll`, `rpcrt4.dll`,
`shell32.dll`, `shlwapi.dll`, `comctl32.dll`, `comdlg32.dll`, `uxtheme.dll` e
famílias de propriedades exigidas pelas aplicações. Implementar inicialização e
apartments COM, marshaling/IPC, ativação de classes/servidores no convidado,
automação, controles, diálogos e operações de Shell em seu namespace. Serviços
auxiliares executam na VM; nenhuma ativação é delegada ao COM/Shell do host.

**Conclusão:** aplicações externas com controles/diálogos e componentes COM
em processo e fora dele cumprem cenários de uso completos, com erros e
encerramento de servidores testados por perfil. Parar ao concluir.

### Subfase 14.9 — Rede, HTTP, criptografia e certificados

**Entrega delimitada:** [PHASE-14.9.md](PHASE-14.9.md). Dez DLLs próprias, transporte de camada 4 limitado, HTTP/WinINet, TLS 1.2 e chaves/certificados virtuais para o corpus Win10 x64. Rede offline por padrão; sem TLS, DNS ou credenciais pessoais do host. Matriz exigida: 133 Release e 58 Debug, com provas estáveis na build privada.

**Dependências:** 14.4/14.6/14.8 e rede contida da fase 10.

**Trabalho:** atender `ws2_32.dll`, `mswsock.dll`, `dnsapi.dll`, `iphlpapi.dll`,
`winhttp.dll`, `wininet.dll`, `bcrypt.dll`, `ncrypt.dll`, `crypt32.dll` e
`secur32.dll` conforme o corpus. Cobrir sockets/DNS/HTTP, operações assíncronas,
TLS, armazenamento de chaves, certificados e credenciais virtuais. Manter modos
offline/simulado/rede autorizada com comportamento documentado e política aplicada
no canal virtual; não usar certificados, chaves ou credenciais pessoais do host.

**Conclusão:** clientes de teste fazem comunicação e validação criptográfica
reais em cenários controlados, com negativas de política, certificados inválidos,
timeouts e cancelamento observáveis. Parar ao concluir.

### Subfase 14.10 — GUI nativa no host, DirectX, mídia e áudio

**Estado:** 14.10A com implementação delimitada em [PHASE-14.10A.md](PHASE-14.10A.md);
14.10B com entrega delimitada em [PHASE-14.10B.md](PHASE-14.10B.md);
14.10C com entrega delimitada em [PHASE-14.10C.md](PHASE-14.10C.md);
14.10D com entrega delimitada em [PHASE-14.10D.md](PHASE-14.10D.md);
14.10E–14.10G com entregas delimitadas em seus documentos de fase. A solicitação de 26/10/2026
prioriza fazer o Windows host executar operações de criação e desenho de GUI,
enquanto a aplicação permanece no MiniNT. A 12F fornece infraestrutura de sessão,
entrada e janelas, mas sua apresentação de pixels não satisfaz esse requisito.

**Dependências:** 14.7–14.9, a base da 12F e contratos das fases 6/12A conforme
o dispositivo. Reutilizar a ponte existente; versionar o novo contrato e declarar
o backend escolhido no manifesto, sem converter silenciosamente a sessão atual.

Os lotes abaixo têm critérios próprios. **Parar após cada lote**, executar suas
verificações, registrar limites e publicar sua entrega pelo fluxo do projeto.
A entrega de 14.10A não declara as operações de B–G implementadas.

#### 14.10A — Protocolo e processo auxiliar de GUI nativa

**Entrega delimitada:** [PHASE-14.10A.md](PHASE-14.10A.md). Gateway próprio,
janela/brush/DC, FillRect/GetPixel reais em auxiliar com token restrito,
integridade baixa, IPC limitado e Job supervisionado. A aceitação exige
143 testes Release, 68 Debug e pacote extraído. USER32 geral fica na 14.10B.

**Trabalho:** definir comandos/respostas tipados e versionados para operações
GUI específicas, com sessão, sequência, autoria e IDs opacos de recursos. Traduzir
estruturas e copiar dados validados; HWNDs/HDCs/HGDIOBJs, ponteiros de funções e
endereços reais do host não atravessam a fronteira. Não oferecer uma operação
genérica de chamar DLL, syscall ou enviar mensagem Windows arbitrária.

Executar a mediação em um processo auxiliar com privilégios e herança de handles
restritos, IPC limitado à sessão, cotas de recursos/comandos/dados e encerramento
supervisionado. Janelas, DCs e objetos GDI só podem pertencer à própria sessão;
recusar IDs expirados, tipos incompatíveis e referências externas. Registrar a
operação nativa executada, resultado e limpeza na telemetria.

**Conclusão:** uma aplicação mínima no MiniNT solicita uma janela e uma operação
simples de desenho que o auxiliar executa pela API real do Windows. Comprovar o
caminho nativo sem exigir uma imagem já rasterizada pelo MiniNT. Testar protocolo,
duas sessões, falhas/queda do auxiliar e liberação de recursos. Parar ao concluir.

#### 14.10B — USER32, janelas e controles nativos

**Entrega delimitada:** [PHASE-14.10B.md](PHASE-14.10B.md). Imports USER32 do SDK,
janelas e BUTTON/STATIC/EDIT reais, mensagens e timers, callbacks no MiniNT e
auxiliar restrito. Critério: 147 Release, 72 Debug e pacote extraído.
GDI de aplicações gerais fica na 14.10C; USER32 completa não é declarada.

**Dependência:** 14.10A.

**Trabalho:** encaminhar o conjunto declarado de criação/destruição, hierarquia,
geometria, estilos, foco, DPI, invalidação e temporizadores para USER32 do host.
Criar controles reais de classes permitidas, começando por BUTTON, STATIC e EDIT,
com estado e notificações traduzidos. Não aceitar classes, módulos ou procedimentos
arbitrários do convidado como código executável no host.

Manter WNDPROCs e callbacks da aplicação no MiniNT. Definir envio/postagem,
retornos síncronos, ordenação, reentrância, timeout e encerramento por thread,
incluindo mensagens que contêm estruturas. Não bloquear indefinidamente a thread
de interface do host esperando uma resposta do convidado.

**Conclusão:** programas independentes criam janelas filhas e controles Win32
reais, recebem cliques/edição/notificações e fecham com ordem e recursos corretos.
Conferir modalidade, falhas, concorrência e isolamento entre sessões. Parar ao
concluir.

#### 14.10C — GDI, texto, fontes e imagens desenhados pelo Windows

**Entrega delimitada:** [PHASE-14.10C.md](PHASE-14.10C.md). DCs e objetos próprios,
clipping, formas, texto, métricas, DIBs e cópias por GDI real do auxiliar, com
comparação nativa. Critério: 152 Release, 77 Debug e pacote extraído.
USER32/GDI completos não são declarados; a 14.10D tem seu escopo próprio descrito abaixo.

**Dependências:** 14.10A–14.10B.

**Trabalho:** traduzir o conjunto da matriz de DCs, seleção/liberação de objetos,
clipping, preenchimento, linhas, texto, fontes e bitmaps para chamadas GDI reais,
como FillRect, TextOutW/DrawTextW e operações BitBlt/StretchDIBits suportadas.
Os dados de um bitmap da aplicação podem ser transferidos; a janela completa não
deve depender de uma rasterização prévia pelo MiniNT para ser considerada nativa.

Confinar DCs e operações de leitura/desenho às superfícies e recursos da sessão;
não fornecer DC do desktop, captura de outras janelas ou acesso a dispositivos
externos. Controlar fontes permitidas, tamanhos, modos e cotas. Devolver métricas,
resultados e erros pelo contrato. Registrar versões do Windows host e do perfil
NT alvo: usar GDI do host não comprova automaticamente comportamento de outro NT.
Diferenças de fontes, métricas, DPI e APIs precisam de comparação e suporte
declarado por perfil.

**Conclusão:** a mesma aplicação de teste executa no MiniNT e numa referência
nativa, com texto, imagens, clipping, redimensionamento e métricas comparados.
Evidências identificam as chamadas GDI do host; testes incluem objetos inválidos,
seleção/restauração, falhas parciais e vazamentos. Parar ao concluir.

#### 14.10D — Controles adicionais, diálogos e aceitação da GUI nativa

**Entrega delimitada:** [PHASE-14.10D.md](PHASE-14.10D.md). Controles e menus
nativos, diálogos no namespace convidado e Flappy original com GDI no host,
sem fallback. Critério: 158 Release, 83 Debug e pacote extraído.
A expansão GDI da 14.10E continua pendente.

**Dependências:** 14.8 e 14.10B–14.10C.

**Trabalho:** ampliar controles comuns/menus e diálogos conforme as aplicações
selecionadas, usando controles desenhados pelo Windows. Arquivos, registro,
processos, COM e Shell permanecem no namespace convidado. Um seletor de arquivos
deve trabalhar com esse namespace; não delegar navegação ao disco pessoal nem
ativar componentes/serviços arbitrários do host. Transferências, clipboard e
drag/drop exigem seus próprios contratos e políticas, não entram implicitamente
pela adoção de controles nativos.

**Conclusão:** aceitar o Flappy original e programas independentes com desenho
GDI e controles/diálogos nativos, incluindo entrada, texto, modalidade, DPI,
concorrência, falhas e limpeza. Distinguir nos relatórios as operações nativas
das operações sem suporte e do modo de pixels da 12F; fallback não conta como
aprovação do requisito nativo. Executar regressões pertinentes, aceitação Windows
Release/Debug e teste do pacote extraído. Publicar o escopo efetivamente aceito;
não anunciar USER32/GDI completos. Parar ao concluir.

#### 14.10E — Compatibilidade GDI ampliada em janelas do host

**Entrega delimitada:** [PHASE-14.10E.md](PHASE-14.10E.md), com matriz de APIs,
corpus ANSI/Unicode independente e comparação nativa. Pendências e restrições
permanecem explícitas; não se declara GDI universal. A 14.10F exige nova solicitação.

**Dependências:** 14.10A–14.10D.

**Trabalho:** ampliar a ponte USER32/GDI conforme lacunas medidas em programas
independentes, antes de iniciar DirectX. Manter um inventário versionado de
funções, variantes ANSI/Unicode e dependências, indicando para cada uma:
implementada, implementada com restrições, recusada por política ou pendente.
Inventário de exports não conta como implementação.

Expandir em lotes estados e contextos gráficos, regiões/clipping, paths,
curvas e transformações, operações de bitmaps, formatos e modos raster, além
de texto, Unicode, glifos, espaçamento, métricas e fontes adicionais por
catálogo controlado. Priorizar as famílias usadas pelo corpus e publicar os
formatos, flags e combinações aceitos; não prometer GDI universal.

Criação, apresentação e desenho das janelas usam USER32/GDI reais no auxiliar
do host, incluindo texto e fontes desenhados pelo Windows. Código, DLLs e
callbacks do aplicativo permanecem no MiniNT. Bitmaps da aplicação podem ser
transferidos pelo contrato limitado; uma janela previamente rasterizada no
MiniNT não satisfaz a aceitação nativa.

Cada operação permitida especifica argumentos, tipos e autoria dos objetos,
permissões, cotas, erros e ciclo de vida. Validar dados nos dois lados da ponte,
copiar buffers e traduzir IDs próprios, sem encaminhar ponteiros, callbacks ou
handles reais do host. Desktop, captura de outras janelas, caminhos arbitrários
de fontes, dispositivos, impressão e escapes de drivers não são liberados
implicitamente. Uma lista de nomes não garante ausência de escape.

**Conclusão:** ampliar o corpus da 14.10D com programas externos independentes
e publicar a matriz de APIs. Comparar os mesmos cenários no MiniNT e no Windows
nativo: pixels, texto, fontes, métricas, DPI, redimensionamento, concorrência e
perfis NT anunciados. Exigir entradas inválidas, objetos de outras sessões,
falhas parciais, cotas e vazamentos, regressões Windows Release/Debug e teste
do pacote extraído. Publicar aplicações aprovadas, cobertura, diferenças e
lacunas; fallback de pixels não conta como aprovação nativa. Parar ao concluir
e aguardar solicitação para a 14.10F.

#### 14.10F — DirectX, desenho 2D e codecs de imagem

**Entrega delimitada:** [PHASE-14.10F.md](PHASE-14.10F.md). Direct2D por software,
texto DirectWrite, PNG/BMP/JPEG e operações de textura D3D11 WARP com
apresentação DXGI por cópia em janela própria. O mesmo EXE SDK e uma amostra
independente são comparados ao Windows nativo. Shaders, Draw, pipeline 3D,
recursos compartilhados, aceleração física e DirectX completo permanecem
pendentes. D3D9 não tem backend; D3D12/OpenGL aguardam lotes posteriores.
A matriz COM registra explicitamente os métodos recusados.
Parar após validar e publicar esta entrega; aguardar solicitação para G.

**Dependências:** 14.10A–14.10E e contratos de dispositivos necessários.

**Trabalho:** priorizar `dxgi.dll`, `d3d9.dll`, `d3d11.dll`, `d2d1.dll`,
`dwrite.dll` e `windowscodecs.dll` conforme o corpus. Definir backend por software
ou dispositivo virtual com recursos, formatos, sincronização, cotas e protocolo
limitado. Acrescentar D3D12/OpenGL e outros backends em lotes conforme requisitos
medidos; não confundir identidade de GPU com aceleração ou suporte a drivers.

**Conclusão:** aplicações independentes desenham e decodificam imagens nos
backends anunciados, com testes de recursos, formatos, sincronização e falhas.
Backend ausente gera limitação explícita. Parar ao concluir.

#### 14.10G — Mídia, áudio e dispositivos de aplicação

**Entrega delimitada:** [PHASE-14.10G.md](PHASE-14.10G.md), reprodução PCM/WAV, WinMM e WASAPI compartilhado. Media Foundation, vídeo, mídia comprimida, captura e notificações permanecem pendentes; não se declara áudio universal. Parar após validar/publicar este recorte.

**Dependências:** 14.10A e, conforme a aplicação, 14.10F.

**Trabalho:** atender `winmm.dll`, `mmdevapi.dll` e famílias de áudio/mídia da
matriz com buffers, formatos, relógios, sincronização, cancelamento e recuperação
de dispositivos. Manter arquivos e dados no convidado e mediar dispositivos por
contratos específicos; suporte a reprodução não concede automaticamente captura
de microfone, câmera ou outros dados pessoais do host.

**Conclusão:** aplicações independentes reproduzem os formatos e usam os
dispositivos declarados, com testes de concorrência, limites, falhas e liberação
de recursos. Parar ao concluir.

### Subfase 14.11 — Frameworks e instalação de aplicações

**Entrega inicial delimitada:** [PHASE-14.11.md](PHASE-14.11.md), interpretador
CIL de console próprio, contratos WinRT escalares, recursos de versão e
instalação portátil no convidado. CLR/CoreCLR completos, WinForms completo/WPF, WinUI,
Windows App SDK e instaladores MSI/SetupAPI continuam pendentes desta subfase
ampla; esse recorte não os declara concluídos. Parar após validar/publicar o lote.

**Continuação da base gerenciada:** objetos, campos, construtores, inicializadores,
arrays, boxing e operações selecionadas de String implementados e validados
na mesma 14.11 (56/56 Release e Debug). Isso não encerra a subfase ampla: ainda faltam GC/EH/dispatch,
BCL e contratos de GUI/runtime exigidos pelos aplicativos alvo dos frameworks.

**Primeiro alvo gráfico escolhido:** contratos .NET Framework 4 x64 no perfil
NT 10.0.19045, exemplo independente Rosetta Code Window creation (C#), fixado por
hash, e controles de aceitação próprios. Form/Button/Label/TextBox selecionados
usam janelas e controles do Windows; CIL e eventos permanecem no MiniNT. A matriz
inclui abrir, receber entrada, fechar, instalar, retomar e remover, sem mouse ou
teclado do desktop. Esse recorte não carrega o CLR Microsoft, não implementa
WinForms completo e não conclui WPF/WinUI. Ver reprodução e limites no documento
da 14.11; validar/publicar este lote e parar antes da próxima continuação.

**Dependências:** 14.5–14.10, conforme cada framework.

**Trabalho:** validar runtimes .NET/CLR selecionados, WinForms/WPF, WinRT,
Windows App SDK/WinUI e frameworks de terceiros da matriz. Implementar contratos
de ativação, metadados e componentes adicionais que cada um exigir; importar o
runtime não substitui essas dependências. Incluir `mscoree.dll`, `clr.dll`,
`coreclr.dll`, `msi.dll`, `version.dll`, `setupapi.dll` e outros módulos conforme
o pacote alvo. Instalação/extração e serviços auxiliares ocorrem no convidado;
instaladores que exigem drivers continuam dependentes de suporte específico.

**Conclusão:** ao menos uma aplicação externa por framework declarado suportado
instala/inicia, executa seu cenário e encerra; casos sem suporte têm diagnóstico.
Não anunciar todo .NET/WinRT por um exemplo aprovado. Parar ao concluir.

### Subfase 14.12 — Aplicações x86 e perfis NT adicionais

**Primeiro lote 14.12A em 29/10/2026:** PE32 em modo de compatibilidade x86,
DLLs próprias de 32 bits, TEB/PEB32, busca de DLLs em SysWOW64 e USER32 nativo
com callbacks stdcall no MiniNT para NT 10.0.19045. O console externo é
independente; a GUI validada neste lote é própria.
[Implementação, provas e limites](PHASE-14.12.md).
**A fase ampla continua aberta:** corpus GUI independente, redirecionamento de
arquivos/registro, TLS/SEH x86, mais APIs e aceitação de perfis adicionais.

**Dependências:** 14.1–14.8; demais famílias conforme o aplicativo.

**Trabalho:** adicionar PE32, execução x86 e camada de compatibilidade equivalente
ao propósito do WOW64, com ABI, estruturas, DLLs, exceções e redirecionamento de
arquivos/registro próprios de 32 bits. DLLs x64 não atendem imports de processos
x86. Ampliar as referências Vista/8/8.1 e perfis legados por demanda, com aceitação
separada por arquitetura e build; XP 5.1.2600 é perfil x86, enquanto XP x64 usa NT
5.2 e exige identificação própria. Corrigir a validação de perfis antes de
permitir essas combinações, sem promover o inventário atual a suporte binário.

**Conclusão:** aplicações externas x86 de console e GUI funcionam com separação
de arquiteturas e cada perfil adicional tem suas referências e regressões
aprovadas. Programas de 16 bits e ARM ficam fora desse lote. Parar ao concluir.

### Subfase 14.13 — Runtime unificado, análise e recuperação

**Entrega delimitada de 29/10/2026:** entrada comum de execução, fila sem
fallback legado, pacotes por SHA-256, autoria de efeitos e recuperação real
de USER32 básico x86/x64 no perfil NT 10.0.19045.
[Implementação e matriz de aceitação](PHASE-14.13.md).
**Continuação 14.13A:** preparação automática com modelos locais, progresso,
salvar/retomar no app e nos serviços efetivamente atualizados, e corpus real
com incompatibilidades registradas em [PHASE-14.13A.md](PHASE-14.13A.md).
Priorizar arquivos auxiliares explícitos e os bloqueios de CRT, contratos,
ordinais, console e diálogos observados antes de ampliar o corpus. A continuidade
x86/GDI da 14.12 e a aceitação ampla da 14.14 permanecem abertas.
GDI/DirectX/áudio vivos, timers/sends pendentes e transporte conectado continuam
fora do contrato de checkpoint; outras famílias seguem os limites já declarados.

**Dependências:** lotes de compatibilidade utilizados e fases 9/13/14.

**Trabalho:** integrar o carregamento externo e as novas DLLs ao mesmo kernel,
armazenamento, dispositivos, políticas, telemetria e ciclo de vida da análise.
Substituir o encaminhamento PE legado do scanner pelo caminho validado no
MiniNT, preservando resultado inconclusivo diante de falha ou incompatibilidade.
Registrar autoria dos efeitos e das chamadas nativas, perfil e módulos; funções
executadas inteiramente dentro de DLLs exigem instrumentação específica se forem
incluídas no escopo de observação, pois não geram necessariamente uma syscall.
Completar snapshots/restauração com módulos externos, processos, GUI, cotas e
estado persistente; vincular todos os artefatos ao perfil e hashes do runtime.

**Conclusão:** um EXE externo enviado pelo scanner percorre análise no kernel
próprio, gera relatório com efeitos atribuídos e suporta recuperação testada;
troca indevida de pacote/perfil ou snapshot é recusada. Parar ao concluir.

### Subfase 14.14 — Aceitação de compatibilidade ampla por versão NT

**Dependências:** todos os lotes necessários às classes de aplicação anunciadas.

**Trabalho:** manter corpus versionado de programas independentes e inofensivos,
com amostragem definida antes da avaliação, cobrindo console, GUI clássica,
utilitários, instaladores, rede, COM, frameworks, mídia e x86/x64. Registrar
versão/hash, perfil NT, dependências, roteiro e resultado por programa: inicia,
cumpre uso previsto, parcial ou incompatível. Testar versões diferentes do mesmo
programa sem contar cada teste interno como uma aplicação independente.

Medir proporção de aplicações que completam o uso previsto por categoria,
arquitetura e perfil, desempenho, falhas e diferenças em relação às referências.
Separar chamadas implementadas de aplicações compatíveis; compatibilidade não
mede eficácia de detecção de malware. A expressão "maioria" só será usada para
um corpus identificado em que mais da metade cumpra os critérios, com resultados
e limitações publicados; não extrapolar automaticamente para todo o ecossistema.
Definir na 14.1 a composição e a meta de aceitação desse corpus antes dos testes.

**Conclusão:** matriz reproduzível e resultados auditáveis, sem esconder cenários
falhos; lacunas viram lotes subsequentes. Reavaliar as pendências das fases
8–13/12F e a prontidão da fase 15. Parar ao concluir e aguardar nova solicitação.

## Fase 15 — Quarentena e gerenciamento de ameaças

**Trabalho:** substituir o armazenamento simples por formato com integridade,
controle de acesso e metadados consistentes. Implementar isolamento, listagem,
restauração, conflitos de destino, exclusão e trilha de ações. Tratar arquivos
bloqueados, links, interrupção de energia e falta de espaço sem perda silenciosa
do original nem liberação acidental da ameaça.

**Conclusão:** isolamento e restauração preservam os bytes corretos e as
permissões previstas; falhas deixam estado recuperável; restauração não permite
escrever fora de destinos autorizados. A UI recebe o resultado real da operação.

## Fase 16 — Serviço de proteção, IPC e tempo real

**Trabalho:** mover tarefas privilegiadas para serviço Windows com privilégios
mínimos. Implementar IPC autenticado, permissões administrativas, filas,
cancelamento e recuperação de falhas. Evoluir monitoramento de arquivos,
varreduras rápida/completa/personalizada, exclusões, agendamento e notificações.
Adicionar minifilter opcional apenas quando necessário, com implantação
suportada e assinatura. Integrar o estado real da proteção à interface.

**Conclusão:** proteção continua sem a UI aberta, resiste a reinícios e carga,
não perde eventos silenciosamente e não permite comandos de clientes sem
autorização. A instalação não desativa proteções do Windows nem depende da VM
para oferecer a verificação básica de arquivos.

## Fase 17 — Aplicação WinUI 3 e internacionalização

**Trabalho:** integrar os serviços reais em C++/WinRT e WinUI 3, preservando os
fluxos existentes durante a migração. Implementar dashboard, verificação,
proteção, sandbox, VMs, comportamento, quarentena, histórico e configurações.
Aplicar Fluent Design, temas, acessibilidade, DPI, navegação por teclado,
progresso e cancelamento. Validar e incorporar JSON de pt-BR, inglês e espanhol
no build, com fallback, parâmetros, pluralização e formatação regional.

**Conclusão:** todos os fluxos da matriz de produto funcionam de ponta a ponta,
sem operações privilegiadas ou código analisado dentro da UI. Idiomas funcionam
sem JSON externo e podem ser trocados conforme o suporte da aplicação. Estados
de recurso indisponível e falha são compreensíveis e correspondem ao backend.

## Fase 18 — Atualizações, instalação e distribuição

**Trabalho:** verificar integridade e autenticidade de regras, bases e binários;
implementar atualizações recuperáveis, migração de dados e rollback. Preparar
EXE/MSI, runtime do WinUI escolhido, instalação do serviço e permissões de
diretórios. Detectar pré-requisitos de virtualização, permitir uso sem sandbox,
tratar atualização/reparo e desinstalação. Revisar licenças e distribuição de
componentes e perfis.

**Conclusão:** instalar, atualizar, reparar e remover em máquinas de teste
limpas; pacotes adulterados são recusados; interrupções não deixam uma instalação
sem recuperação. Dados preservados ou removidos seguem a escolha explícita do
usuário. Não se distribuem componentes Windows sem autorização adequada.

## Fase 19 — Validação de lançamento, desempenho e manutenção

**Trabalho:** consolidar testes automatizados, fuzzing de parsers e protocolos,
revisão especializada de isolamento e testes de resistência a entradas hostis.
Executar a matriz de compatibilidade, falsos positivos, desempenho e recuperação
de falhas; medir CPU/RAM/I/O e otimizar filas, cache e varredura incremental.
Verificar múltiplas VMs, falta de recursos, reinício, atualização e perda de
conectividade. Preparar diagnóstico, documentação e processo de correções.

Avaliar também os cinco grupos das subfases 12A–12E com uma suíte versionada,
inicialmente de programas próprios inofensivos. Registrar sinais observados,
diferenças em relação à referência e efeitos sobre o comportamento das
aplicações. Cada achado deve apontar para uma correção ou limitação conhecida.
Qualquer proporção de amostras que reconhece ou não o ambiente exige corpus,
método, versões e limites publicados. Alguns exemplos não sustentam afirmar
que a maioria dos vírus não identifica o sandbox.

**Conclusão:** critérios mensuráveis definidos na fase 0 atendidos, problemas
críticos tratados, limites publicados e revisão de isolamento concluída para
os usos anunciados. Testes de segurança, revisão e desempenho também acontecem
nas fases anteriores; esta é a aceitação integrada de lançamento.

## Dependências e marcos

A sequência numérica é a ordem principal de execução. Componente sem
dependência do kernel pode ser preparado dentro de uma fase explicitamente
solicitada, mantendo a prioridade no núcleo. A parada obrigatória impede
avançar para outra fase ou subfase sem nova solicitação. A compatibilidade da
fase 12 é ampliada por lotes; não bloqueia indefinidamente o primeiro produto
com um subconjunto claramente declarado.

**Prioridade revista em 23/10/2026:** após a entrega da fase 14, as subfases
14.1–14.14 organizam a próxima expansão de compatibilidade e precedem a fase 15.
Começar pela 14.1 e parar ao final de cada subfase. A aceitação gráfica da 14.7
alimenta a decisão de iniciar a 12F; sua implementação continua separada e exige
solicitação. Essa ordem não transforma fases antigas pendentes em concluídas.

| Pedido de realismo | Subfase | Validação |
|---|---|---|
| CPU/GPU, BIOS, placa-mãe, dispositivos e drivers | 12A | Inventário consistente, interfaces e drivers funcionais. |
| RAM, núcleos e disco semelhante ao host com limite | 12B | Provisionamento, orçamento e cotas coerentes. |
| Tempo, exceções, memória e operações do sistema | 12C | Testes diferenciais por perfil NT. |
| Esperas longas, reinícios e atividade de entrada | 12D | Sessões persistentes e roteiros no convidado. |
| Documentos, contas, programas e histórico | 12E | Perfis sintéticos funcionais e reproduzíveis. |

| Marco | Evidência necessária |
|---|---|
| Kernel inicia | Fases 0–2: imagem própria inicia e atende uma chamada de usuário. |
| Kernel multitarefa | Fases 3–5: memória isolada, preempção, processos e direitos. |
| Ambiente de console | Fases 6–8: dispositivos, persistência e aplicações selecionadas. |
| Sandbox gerenciável | Fases 9–10: snapshots consistentes, VMs e rede contida. |
| Aplicações gráficas | Fase 11 e lote de compatibilidade correspondente da fase 12. |
| Janelas independentes no desktop Windows | Subfase 12F: ponte de apresentação validada, aplicativo no convidado e entrada restrita à sessão. |
| Janelas, controles e desenho executados pelo Windows host | 14.10A–14.10E, entregas delimitadas e expansão planejada: operações GUI mediadas, callbacks no MiniNT e aceitação específica do backend nativo. |
| DLLs Windows no MiniNT | 14.1–14.6: perfis NT, carregador e serviços nativos/Win32/CRT comprovados no convidado. |
| Aplicativos externos de desktop | 14.7–14.12: GUI, COM, rede, gráficos, frameworks e x86 conforme a matriz por perfil. |
| Compatibilidade ampla mensurada | 14.13–14.14: análise integrada, recuperação e corpus independente com resultados por versão NT. |
| Análise integrada | Fases 13–16: evidências, scanner, quarentena e serviço. |
| Produto instalável | Fases 17–18: interface, idiomas, instalação e atualização. |
| Versão de lançamento | Fase 19 e matriz de suporte aprovada para essa versão. |

## O que significa estar completo

O produto estará completo para uma versão quando cumprir os requisitos
funcionais e os limites de compatibilidade declarados, com evidência para cada
item. Compatibilidade universal com todas as versões, APIs, aplicações e drivers
Windows não recebe uma promessa nem um percentual neste plano. O trabalho será
medido por critérios aprovados, não pela quantidade de funções com o mesmo nome.

**Entrega atual:** fase 14 aceita no Windows, com validação Linux parcial;
protótipo gráfico externo da fase 12 aceito para a referência própria, com
aceitação delimitada do Flappy e de janelas nas entregas 14.7/12F. A 14.1 implementa contratos, inventário e
vínculo de perfil. A 14.2 implementa o carregador externo e volume de runtime,
com escopo e aceitação próprios; a 14.3 implementa a matriz nativa limitada
descrita em PHASE-14.3.md. A 14.4 acrescenta o runtime Win32 próprio descrito em
PHASE-14.4.md. A 14.5 implementa o corpus CRT original delimitado em
PHASE-14.5.md. Rethrow/nested C++ e compatibilidade CRT geral continuam abertos.
A 14.6 implementa a matriz delimitada de segurança/registro/serviços descrita em
PHASE-14.6.md e PHASE-14.7.md. A 14.8 entrega a matriz delimitada de componentes
de PHASE-14.8.md. A 14.9 entrega o corpus de rede e criptografia de PHASE-14.9.md. A 12F acrescenta a apresentação nativa delimitada de PHASE-12F.md. A 14.10A entrega o recorte de PHASE-14.10A.md; 14.10B e C entregam os recortes de PHASE-14.10B.md e PHASE-14.10C.md; 14.10D entrega o recorte de PHASE-14.10D.md; 14.10E entrega o recorte de PHASE-14.10E.md; 14.10F entrega o recorte de PHASE-14.10F.md; 14.10G entrega o recorte de PHASE-14.10G.md; mídia comprimida e a aceitação ampla de 14.11–14.14 permanecem abertas. A 14.11 entrega os contratos limitados de PHASE-14.11.md; a 14.12A entrega o núcleo x86 de PHASE-14.12.md. Os resultados históricos e as pendências
anteriores permanecem identificados nos respectivos documentos.

## Referências técnicas para os novos requisitos

- [Memória do convidado em WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange)
- [Componentes e interfaces de drivers gráficos WDDM](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-architecture)
- [Espaço livre e espaço disponível com cotas](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdiskfreespaceexa)
- [Resolução de API sets pelo carregador](https://learn.microsoft.com/en-us/windows/win32/apiindex/api-set-loader-operation)
- [Versões NT, incluindo Windows 10/11 e XP x64](https://learn.microsoft.com/en-us/windows/win32/sysinfo/operating-system-version)
- [GetVersionEx e manifesto de compatibilidade](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversionexa)
- [Execução x86 em Windows x64 e separação de DLLs](https://learn.microsoft.com/en-us/windows/win32/winprog64/running-32-bit-applications)
- [Janelas Win32 e classes de controles](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw)
- [Mensagens, estruturas de parâmetros, envio e filas](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues)
- [DCs e objetos gráficos do GDI](https://learn.microsoft.com/en-us/windows/win32/gdi/device-contexts)

## Documentação do estado implementado

- [Heap e continuação da base](KERNEL-CONTINUATION.md)
- [Memória virtual](VIRTUAL-MEMORY.md)
- [Objetos e handles](KERNEL-OBJECTS.md)
- [Threads](KERNEL-THREADS.md)
- [Mutexes e semáforos](KERNEL-SYNCHRONIZATION.md)

Os documentos anteriores registram resultados das respectivas etapas. Este
arquivo é um plano de trabalho; suas fases pendentes não são implementações
concluídas. A arquitetura histórica de MicroVM deve ser lida junto das decisões
atuais registradas aqui, inclusive a disponibilidade de WHP já validada nesta
máquina e a mudança de prioridade para um kernel próprio no convidado.
