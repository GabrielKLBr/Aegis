# Plano de desenvolvimento do GuardianAV e do kernel próprio

Data de referência: 19/10/2026.
Base ativa: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.

**Estado atual:** 12A concluída para a matriz de referência, com validação Windows/WHP e Linux/KVM. [Resultados e limites](PHASE-12A.md). A 12B está em implementação e validação: [recursos, cotas e pendências](PHASE-12B.md). O contrato próprio `resources` passou na matriz Linux (47 Debug, 47 Release e 30 ASan/UBSan), com SMP, volume escalável e pasta independente; a aceitação Windows da revisão e a integração aos perfis existentes continuam pendentes. [Continuação de 20/10](PHASE-12B-RECURSOS.md). A 12C passou na validação Linux em 21/10/2026 (50 Debug, 50 Release e 32 ASan/UBSan), com referências Windows ainda pendentes: [matriz, entrega e diferenças](PHASE-12C.md). A 12D foi autorizada em nova solicitação: sessões e interação passaram em 53 testes Debug, 53 Release e 33 ASan/UBSan na revisão estável da 12D. [Registro da 12D](PHASE-12D.md). A 12E foi implementada e aceita no Linux em 22/10/2026: 56 Debug, 56 Release e 34 ASan/UBSan, com perfis sintéticos funcionais e reproduzíveis. [Entrega e limites](PHASE-12E.md). A aceitação nativa Windows e os requisitos anteriores permanecem pendentes. A fase 13 passou na aceitação Linux em 22/10/2026: 59 Debug, 59 Release e 35 ASan/UBSan, com telemetria, políticas e análise comportamental. [Entrega e limites](PHASE-13.md). A aceitação Windows e os requisitos anteriores continuam abertos; a fase 14 não foi iniciada. Os registros históricos abaixo conservam os resultados e pendências da época.

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
subfase 12A–12E abaixo. A autorização da fase corrente permite concluir suas
tarefas e correções, mas não autoriza avançar automaticamente para a seguinte.

Um teste aprovado isoladamente não encerra uma fase cujo restante está pendente.
Se houver bloqueio real, registrar o ponto exato e não declarar conclusão.
Esta regra foi registrada antes da implementação. O progresso de cada fase é
informado no início deste documento; não inicia uma fase seguinte automaticamente.

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
| Análise integrada | Fases 13–16: evidências, scanner, quarentena e serviço. |
| Produto instalável | Fases 17–18: interface, idiomas, instalação e atualização. |
| Versão de lançamento | Fase 19 e matriz de suporte aprovada para essa versão. |

## O que significa estar completo

O produto estará completo para uma versão quando cumprir os requisitos
funcionais e os limites de compatibilidade declarados, com evidência para cada
item. Compatibilidade universal com todas as versões, APIs, aplicações e drivers
Windows não recebe uma promessa nem um percentual neste plano. O trabalho será
medido por critérios aprovados, não pela quantidade de funções com o mesmo nome.

**Entrega atual:** 12A concluída para sua matriz de referência em 19/10/2026.
Windows e Linux aprovados, com retestes e limites explícitos em
[PHASE-12A.md](PHASE-12A.md). A fase 12 continua aberta e a 12B não foi iniciada.

## Referências técnicas para os novos requisitos

- [Memória do convidado em WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange)
- [Componentes e interfaces de drivers gráficos WDDM](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-architecture)
- [Espaço livre e espaço disponível com cotas](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdiskfreespaceexa)

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
