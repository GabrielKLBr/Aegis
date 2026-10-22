# Fase 4 — Threads e sincronização no convidado

Base ativa: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Imagem: `guardian-kernel-threads.gk`, versão 0.4.0, x64, uma vCPU, WHP.
Estado: concluída em 16/10/2026. A fase 5 não foi iniciada.

## Implementação

`guest/Scheduler.cpp` adapta as regras de `GuestScheduler.h` e
`GuestSynchronization.h` para estruturas limitadas, sem STL, alocação do host
ou APIs Windows. O escalonador e os objetos executam em CPL0 no convidado.
`ThreadKernel.cpp` liga esse núcleo à memória da fase 3 e às interrupções e
syscalls da fase 2. O host injeta o timer existente; não escolhe a próxima
thread nem emula operações de locks.

Cada thread tem contexto próprio e pilha de 64 KiB, com 60 KiB comprometidos
e uma página inferior não presente. Criação incompleta desfaz reserva,
frames, entrada de thread e objeto de conclusão. O teste inclui falta de
frames durante essa criação. IDs têm geração, para recusar referências
antigas após reciclagem. A remoção de uma thread terminada libera seu objeto
de conclusão; as pilhas são recuperadas pelo proprietário no encerramento.

### Escalonamento e estado de CPU

- Estados: pronta, executando, esperando e terminada; suspensão tem contador
  separado. Uma espera pode concluir enquanto a thread está suspensa, mas ela
  só volta a executar depois da retomada.
- Há oito prioridades. Entre prioridades efetivas iguais, a ordem é FIFO com
  rotação. O envelhecimento eleva a prioridade efetiva de uma thread pronta a
  cada oito avanços do relógio lógico, limitado ao nível sete. Isso dá uma
  oportunidade de execução a uma thread de baixa prioridade mesmo com
  concorrentes continuamente prontos.
- O timer de 5 ms provoca preempção de código CPL3. Uma thread de cálculo não
  precisa chamar o kernel para outra avançar. Na ausência de threads prontas,
  um contexto ocioso CPL3 aguarda novas interrupções; timeouts e timers podem
  torná-las prontas novamente.
- O kernel mantém interrupções mascaradas durante o tratamento de syscall ou
  interrupção. A troca acontece na saída, sobre o frame que será restaurado
  por IRETQ. Não existem ainda threads de kernel preemptíveis ou SMP.
- O contexto contém os 15 registradores gerais, RIP/RSP, RFLAGS, seletores,
  bases FS/GS e FXSAVE64 de 512 bytes: x87/MMX, XMM0–15 e MXCSR. O ambiente de
  ponto flutuante do kernel é inicializado após salvar o estado do usuário.
  O retorno filtra flags privilegiadas e valida RIP e a pilha da thread.
  O teste também escreve NT com POPFQ em CPL3. A máscara de entrada SYSCALL
  remove NT do estado ativo do kernel e a seleção de contexto limpa a cópia
  de retorno, inclusive após preempção, evitando #GP em IRETQ. A tentativa
  intermediária que revelou esse caso foi preservada em `phase4-nt-first-failure-*`.
- OSXSAVE, AVX, PKU e FSGSBASE não são habilitados. Não se anuncia suporte de
  contexto a recursos estendidos sem salvamento correspondente. O código
  verifica essa configuração antes de iniciar threads.

### Esperas e objetos

Eventos manuais liberam todos os aguardando; eventos automáticos liberam um e
consomem o sinal. Semáforos validam contagem inicial, limite e liberação; uma
espera concluída consome exatamente um crédito. Mutexes e critical sections
têm dono, recursão e liberação por dono. Uma thread que termina ou sofre fault
abandona os locks que possui; a aquisição seguinte recebe o resultado de
abandono. Para critical section, esse resultado é uma política explícita do
projeto, sem promessa de reproduzir comportamento indefinido do Windows.

SRW permite leitores simultâneos ou um escritor exclusivo, recusa recursão,
upgrade implícito e liberação sem propriedade. Escritores já aguardando têm
preferência sobre novos leitores. A saída de uma thread remove sua propriedade
de leitura/escrita e permite que outros avancem. Não há garantia de fairness
idêntica à implementação do Windows.

Esperas aceitam até oito objetos, com qualquer um ou todos. Wait-any escolhe
o menor índice disponível. Wait-all valida todos antes de consumir eventos,
créditos ou ownership; duplicatas são recusadas. Referências de esperas mantêm
os objetos vivos e impedem fechamento prematuro. Threads terminadas são
aguardáveis por seus objetos de conclusão.

O relógio é monotônico. Avanços para trás são recusados. No mesmo instante,
expiração de espera é processada antes do sinal de timer, definindo uma ordem
testável para a corrida. Timeouts e cancelamentos não consomem recursos que
chegaram depois. Timers podem ser manuais ou automáticos, de uma execução ou
periódicos; cancelar desativa disparos futuros sem apagar um sinal já entregue.

### Alertas e APCs

Uma espera alertável pode retornar por alerta ou APC. Uma não alertável
permanece esperando, conservando o alerta/APC pendente. Cancelar uma espera
retorna `Cancelled`; terminar uma thread cancela seus estados pendentes e
libera ownership. Alertas não chamam rotinas de usuário.

APCs possuem fila limitada e endereço validado na página executável de usuário.
Ao entregar, o kernel conserva o contexto original e entra no dispatcher CPL3,
que chama a rotina com seu argumento. A syscall de retorno restaura o contexto
original completo, inclusive ponto flutuante. Um retorno sem entrega ativa é
recusado. Espera alertável aninhada dentro de uma APC retorna `NotSupported`;
APCs aninhadas não são anunciadas nesta ABI.

### ABI interna e fronteira com o host

As syscalls `0x20..0x2F` implementam yield, término, espera, unlock, sinal,
delay, retorno de APC, espera múltipla, suspensão/retomada, cancelamento,
alerta, fila de APC, prioridade e criação. A chamada `0x30` é o bootstrap
controlado da primeira troca. A interface usa registradores; a lista de espera
é copiada somente após validar todo o intervalo em memória de usuário.
Endereços de kernel, overflow, flags inválidas, IDs truncados e entradas
incompatíveis não recebem sucesso provisório.
Criação retorna o ID da thread em RDX e o ID de seu objeto de conclusão em R8;
o segundo pode ser usado para aguardar seu término. Esses IDs internos ainda
não são os handles e direitos de processo previstos na fase 5.

O dispositivo de permissões físicas da fase 3 é reutilizado, inclusive para
criação de pilhas durante a execução. O relatório de threads tem contrato
próprio em `shared/guardian/Threads.h`: porta `0xEE`, GPA `0x10A000`, versão,
tamanho, sessão e campos reservados validados. O host exige o timer desarmado
e recebe o relatório uma única vez, antes do relatório de CPU e da parada.
Nenhum ponteiro do convidado é usado como ponteiro do host.

## Validação

Critérios: `config/phase4-acceptance.json`.

O build completo e os 23 testes passaram em Debug e Release, sem testes
ignorados. A matriz final executou 80 boots da fase 4 e 240 de regressão das
fases 1–3, além dos cenários negativos correspondentes. Os sete programas de
referência passaram em cada configuração. A compilação Debug em uma pasta
nova, sem BUILD_TESTING, também passou e auditou as quatro imagens de produção.
As imagens não têm imports Windows, TLS, relocations nem seções W+X.

| Configuração | RAM | Boots da fase 4 | Tempo por boot observado |
|---|---|---|---|
| Debug | 16 MiB | 20 | 195,613–208,190 ms |
| Debug | 64 MiB | 20 | 198,599–206,015 ms |
| Release | 16 MiB | 20 | 196,721–213,795 ms |
| Release | 64 MiB | 20 | 196,140–258,631 ms |

Os tempos incluem preparação e autotestes nesta máquina. Não representam
garantia de tempo real do host. A suíte final levou 27,70 s em Debug e 23,80 s
em Release. O kernel de threads ocupa 479232 bytes em memória em Debug e
462848 em Release. Cada configuração produziu 52 executáveis e seis imagens
de kernel, sendo quatro de produção e duas negativas da fase 2. A auditoria
dos projetos gerados verificou 20 combinações de alvo/configuração, mantendo
as verificações do runtime do host Debug separadas do convidado freestanding.

`guest_scheduler_core` executa o mesmo código freestanding usado no convidado.
Também é executado em CPL0 a cada boot da nova imagem. Suas verificações
cobrem rotação, prioridades, envelhecimento, suspensão, sinalização, consumo
atômico de wait-all, recursão, abandono, SRW, timers, alertas, APCs, filas e
limites. Incluem 128 interleavings de sinal, timeout e cancelamento, 256 ciclos
de criação/término/remoção e esgotamento das tabelas de threads e objetos.
São 983 verificações por execução.

`guest_threads` executa 20 boots com 16 MiB e 20 com 64 MiB por configuração.
Cada boot cria nove threads de usuário em duas rodadas:

- Dois workers incrementam contadores sem syscalls no laço. Eles verificam
  registradores gerais, DF, XMM0–15, estado x87, controles de arredondamento e
  FS/GS durante interrupções e trocas.
- Dois workers disputam mutex, critical section, SRW exclusivo e semáforo.
  Cada seção lê o contador, cede a CPU ainda segurando o lock e depois escreve.
  O resultado deve ser oito incrementos preservados por tipo de lock, 32 no
  total. Um lock que só retornasse sucesso perderia atualizações nesse teste.
- Uma thread recebe uma APC em modo usuário e depois aguarda um timer. A rotina
  altera estado de ponto flutuante de propósito; o retorno deve restaurá-lo.
- Uma thread sofre #UD segurando um mutex; outra deve receber `Abandoned`.
  A controladora também testa um ponteiro inválido e wait-all por syscall.
- Na segunda rodada, uma thread cria uma filha suspensa por syscall, exercita
  os contadores de suspensão/retomada e aguarda um prazo. O timer deve também
  interromper o contexto ocioso e retomar a espera.
  A mãe aguarda o objeto de conclusão da filha antes de iniciar seu delay.

O resultado exige oito términos normais, um fault de thread contido, uma APC
entregue, progresso dos workers sem syscalls, interrupção durante ociosidade
e recuperação de todas as pilhas. Permanecem apenas os 21 frames do espaço de
memória ativo herdado da fase 3. A máscara final é 32767. Há ainda 12 cenários
externos negativos por configuração, incluindo relatório inválido, replay,
IN/OUT largo, ordem incorreta e ausência de timer limitada pelo watchdog.

## Compilar e executar

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-threads.gk
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-threads.gk --threads --ram-mib 64 --repeat 2 --report .\build-codex\phase4-cli-Release.json
```

`--threads` seleciona este contrato, separado de `--memory` e da imagem de
CPU. As imagens anteriores continuam sendo compiladas e testadas. O WHP é
usado diretamente, sem QEMU e sem alterar configurações do Windows host.

## Evidências e recuperação

Os relatórios novos usam exclusivamente o prefixo `phase4-`, inclusive os de
regressão. Os 134 arquivos de evidência histórica das fases anteriores mantêm
seus hashes. O backup dos dez arquivos anteriores alterados está em
`snapshots/codex-before-phase4-20261016-193923/`, com os hashes históricos em
`historical-evidence.json`. Não houve remoção de arquivos do projeto.

Evidências em `build-codex/`:

- `phase4-full-debug-script.log`, `phase4-full-release-script.log` e os
  correspondentes `phase4-*-tests-details.log`.
- `phase4-boots-Debug.json`, `phase4-boots-Release.json`, seus `.negative.json`
  e as regressões `phase4-regression-phase1-*`, `phase4-regression-phase2-*` e
  `phase4-regression-phase3-*`.
- `phase4-reference-programs.json`, `phase4-smoke.json` e
  `phase4-cli-Debug.json` / `phase4-cli-Release.json`.
- `phase4-image-audit.json`, `phase4-build-boundaries.json`,
  `phase4-clean-configure.log` e `phase4-clean-debug-build.log`.
- `phase4-change-manifest.json`: hashes, fontes novas/alteradas, backup e
  matriz verificada. O verificador pode ser repetido sem executar programas:
  `python tools/validate_phase4.py --snapshot snapshots/codex-before-phase4-20261016-193923`.

O aviso LNK4281 decorre da base fixa do kernel em 2 MiB, conforme o contrato
do carregador. Logs de tentativas intermediárias permanecem identificados,
incluindo a falha de NT corrigida e testada na entrega final.

## Limites e parada

Até 16 threads, 64 objetos, oito objetos por espera, quatro APCs pendentes por
thread, oito prioridades e recursão máxima de 127. Pilhas têm tamanho fixo.
Operações de kernel são serializadas na única vCPU. Não há preempção dentro
do kernel, SMP, APC de kernel ou suporte a estado AVX/XSAVE nesta etapa.

Esta é uma ABI interna do kernel próprio. Exports Rtl/Win32, estruturas de
loader e compatibilidade geral com aplicações Windows permanecem nas fases
correspondentes. O MiniNt legado continua como referência cooperativa do host;
seus atalhos de compatibilidade não são usados pela imagem de threads. Os
locks e esperas necessários ao programa desta imagem possuem implementação
real, sem no-ops. Processos, handles, direitos e tokens são assunto da fase 5.

Ao concluir a fase 4, parar. A fase 5 só começa após nova solicitação.

## Referências

- [Intel: programação de sistema, interrupções e estado de CPU](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Intel: bases FS/GS e troca de contexto](https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/best-practices/guidance-enabling-fsgsbase.html)
- [Microsoft: funções de espera](https://learn.microsoft.com/en-us/windows/win32/sync/wait-functions)
- [Microsoft: esperas e APCs](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/waits-and-apcs)
- [Microsoft: objetos de timer](https://learn.microsoft.com/en-us/windows/win32/sync/waitable-timer-objects)
