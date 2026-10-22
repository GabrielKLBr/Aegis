# Fase 5 — Processos, objetos e segurança no convidado

Base ativa: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Imagem: `guardian-kernel-processes.gk`, versão 0.5.0, x64, uma vCPU, WHP.
Estado: concluída em 16/10/2026. A fase 6 não foi iniciada.

## Implementação

`guest/ProcessManager.cpp` implementa processos, tabelas de handles, objetos,
tokens e decisões de acesso em estruturas limitadas, sem STL, CRT ou APIs do
Windows host. `ProcessKernel.cpp` integra esse gerenciador à memória da fase 3,
ao escalonador da fase 4 e às entradas de syscall/interrupção da fase 2. O WHP
executa o código do convidado; o host transporta os relatórios, pedidos de
permissão física e interrupções de timer existentes.

### Processos e memória

Cada processo possui um espaço de endereços com CR3, dados, alocações privadas,
pilhas e contexto de segurança próprios. A seleção de uma thread também
seleciona o espaço de seu processo. O mesmo endereço virtual pode conter dados
diferentes em cada processo. As páginas de kernel permanecem supervisoras.

A entrada no kernel salva o contexto e ativa um espaço separado de kernel e
ociosidade. A recuperação de um processo encerrado acontece depois de sair
de seu CR3. Terminar um processo encerra todas as suas threads, cancela suas
esperas, abandona os locks possuídos e recupera memória e handles. Um fault em
CPL3 encerra o processo responsável; os demais continuam sendo escalonados.

Criar um filho deriva seu token do pai e recebe uma lista explícita de handles
herdáveis. A criação é transacional: falhas no espaço de memória, objetos,
handles ou herança parcial desfazem o que já foi criado. O programa de boot
também desfaz pilhas e threads se falhar a preparação do filho.

`ExecutionContext.h` reúne o mesmo contexto de 720 bytes usado na fase 4:
registradores gerais, RIP/RSP, RFLAGS filtradas, seletores, bases FS/GS e
FXSAVE64. As imagens de threads e processos usam essas rotinas comuns. A
regressão de threads continua verificando x87/MMX/SSE, XMM0–15, estado de
arredondamento e o caso NT/IRETQ corrigido na fase 4. AVX/XSAVE continuam
desabilitados. `StackProbe.asm` fornece um `__chkstk` real para frames de
pilha maiores que uma página, tocando cada página intermediária e preservando
os registradores exigidos pela ABI do compilador.

### Handles, referências e namespace

Handles pertencem à tabela de um processo e carregam tipo de objeto, máscara
de acesso concedida e atributos de herança/proteção contra fechamento. Valores
novos são monotônicos; IDs de processo e objeto têm geração. Copiar um número
de handle de outro processo não o torna válido na tabela local. Na herança
autorizada, o filho recebe intencionalmente o mesmo valor do handle herdado.

Duplicação exige o direito de duplicar no handle de origem e no handle do
processo de destino. O resultado não pode ampliar a máscara da origem e passa
pela ACL e integridade do token destinatário. A herança também revalida o
acesso do filho. Essa política é da ABI interna, mais restritiva que uma
transferência de capacidade sem revalidação; não se declara equivalência
binária com `DuplicateHandle` ou criação de processos do Windows.

Referências de handles, esperas pendentes e propriedade de locks mantêm o
objeto vivo. Fechar o último handle durante uma espera ou enquanto há dono não
destrói o objeto prematuramente. Um objeto compartilhado pode sobreviver à
saída de seu criador enquanto outro processo ou uma espera o referencia.
Objetos de conclusão de processo/thread também sobrevivem ao reap quando
existem referências externas; o código de saída permanece consultável.

Há nomes `\Global\nome` e `\Session\nome`, separados pela sessão numérica do
token. Criar um nome global exige o privilégio interno `CreateGlobal`; abrir
um nome existente exige os direitos solicitados na ACL. Nomes são ASCII,
sensíveis a maiúsculas, sem componentes de caminho, com até 63 bytes no nome
final. Colisão de tipos é recusada. Esses nomes não são caminhos do host.

### Tokens e decisões de acesso

Tokens imutáveis registram usuário, grupo primário, até quatro grupos
suplementares, sessão, três níveis de integridade e o privilégio de criar
objetos globais. As identidades são números internos do convidado; não são
tokens ou credenciais do Windows host. O filho mantém identidade, sessão e
grupo primário, pode reduzir integridade/privilégios e converter grupos
habilitados em grupos usados somente para negações. Não pode adicionar grupos,
remover grupos do pai ou reabilitar um grupo de negação. Pai e filho são
validados antes de comparar restrições.

Descritores guardam proprietário, grupo, integridade, política de não escrever
para nível superior e uma DACL de até oito entradas. A DACL aceita usuário,
grupo ou todos, com negações antes das permissões. DACL vazia nega; DACL ausente
permite, depois da política de integridade. O proprietário tem controle
implícito de leitura/alteração da DACL. Grupos de negação não concedem acesso.

Os direitos incluem esperar, consultar, modificar, duplicar, ler/escrever
memória de processo, terminar, criar filho e ler/alterar DACL. Direitos
genéricos são convertidos por tipo; bits inválidos são recusados. Alterar a
DACL exige `WriteDac` e não permite mudar proprietário, grupo ou rótulo por
esse caminho. A mudança afeta novas aberturas, preservando máscaras já
concedidas. A política de integridade bloqueia operações de escrita/controle
de nível inferior sobre objeto superior; leitura é decidida pela DACL.

Cópias entre processos exigem handles com direitos explícitos de leitura ou
escrita de memória. Todos os bytes de origem e destino são validados antes
de modificar qualquer destino. Há limite de 4 KiB e um buffer intermediário
para sobreposição. Endereços são traduzidos pelas tabelas do convidado; um
ponteiro recebido de CPL3 não vira um ponteiro arbitrário do host.

## Exercícios de aceitação

O mesmo núcleo de 693 verificações executa em teste nativo e em CPL0 a cada
boot da imagem de processos. Ele cobre restrições de token, DACL vazia/nula,
grupos de negação, direitos, integridade, tipos, namespacing por sessão,
duplicação, herança parcial, proteção contra fechamento, referências e
recuperação. Inclui esgotamento de 32 handles, 64 objetos e quatro threads por
processo, quota de commit, todas as 21 posições de falha na criação de um
espaço de memória e 128 ciclos completos de criação/encerramento.

O boot real cria três processos e quatro threads de usuário:

- Pai e adversário começam com identidades diferentes. O pai cria um filho
  por syscall, com integridade reduzida, sem privilégio global e com um único
  handle de evento herdado para esperar/consultar.
- Os três processos usam `0x40010000`, com conteúdos `0x1111`, `0x2222` e
  `0x3333` em frames diferentes. O pai lê e escreve na memória do filho por
  operações autorizadas. O filho não consegue abrir o pai para escrita.
- O adversário não consegue ler a memória do pai, usar seu número de handle
  ou sinalizar um evento aberto somente para esperar. Uma de suas threads
  lê uma página supervisora e produz #PF real, vetor 14, erro 5, CPL3.
  O kernel encerra suas duas threads, incluindo uma com laço sem syscalls.
- Pai e filho continuam: o pai sinaliza o evento, aguarda a conclusão do filho,
  consulta seu código de saída depois da recuperação e termina normalmente.

O relatório exige dois términos normais, um processo com fault contido, pelo
menos seis recusas de acesso, uma herança, duas cópias remotas autorizadas e
quatro interrupções de timer reconhecidas. Ao fim há zero processos, objetos,
handles e referências vivos. Restam somente os 21 frames do espaço de
kernel/ociosidade. O teste aguarda explicitamente o mínimo de ticks; não
presume que quatro interrupções sempre ocorreram antes de outra operação.

Cada configuração deve passar 40 boots de processos, 20 com 16 MiB e 20 com
64 MiB, mais 12 casos externos negativos: formato/sessão/reservados/máscara de
relatório inválidos, ordem, comando desconhecido, IN/OUT largo, replay e
ausência de timer com watchdog. As fases 1–4 somam outros 160 boots positivos
por configuração. Critérios: `config/phase5-acceptance.json`.

## Resultado verificado

| Verificação | Resultado |
|---|---|
| Build completo Debug / Release | aprovado / aprovado |
| CTest por configuração | 25 aprovados, zero falhas e zero ignorados |
| Núcleo de processos/segurança | 693 verificações, nativo e em cada boot |
| Boots positivos de processos | 80 (40 por configuração) |
| Boots positivos de regressão das fases 1–4 | 320 |
| Casos externos negativos | 160 (24 da fase 5 e 136 de regressão) |
| Exemplos confiáveis do projeto | sete por configuração, 14 execuções aprovadas |
| Imagens de produção auditadas | cinco por configuração, sem imports/TLS/relocations/W+X |
| Compilação Debug limpa independente | aprovada, com cinco auditorias de imagem |
| Artefatos do build completo | 54 executáveis e sete imagens por configuração |
| Evidências históricas preservadas | 184 arquivos com SHA-256 inalterado |

Na matriz de processos, os tempos observados foram 103,089–123,917 ms em Debug
e 75,177–103,123 ms em Release; são medidas desta máquina, não garantia de
prazo. A imagem ocupa 524.288 bytes de espaço virtual em Debug e 499.712 em
Release, ambas dentro do limite de 1 MiB do carregador. A verificação adicional
da CLI executou mais dois boots de processos por configuração; eles não estão
somados aos 400 boots da matriz acima.

## Compilar e executar

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-processes.gk
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-processes.gk --processes --ram-mib 64 --repeat 2 --report .\build-codex\phase5-cli-Release.json
```

`--processes` seleciona este contrato. Combinações com os contratos de memória
ou threads são recusadas antes de alocar uma VM. As imagens anteriores continuam
sendo compiladas e testadas. O transporte do novo relatório usa a porta EF e
o endereço físico fixo `0x10B000`, com tamanho, versão, sessão e ordem validados.
O WHP é usado diretamente, sem QEMU e sem alterar configurações do Windows.

## Evidências e recuperação

Os resultados desta fase usam o prefixo `phase5-`, inclusive as regressões.
Os 184 arquivos de evidência histórica mantêm seus hashes. O backup dos onze
arquivos anteriores alterados fica em
`snapshots/codex-before-phase5-20261016-201504/`, junto do inventário histórico.
Há 14 arquivos novos de implementação, testes, configuração e documentação.

Evidências em `build-codex/`:

- `phase5-full-debug-script.log`, `phase5-full-release-script.log` e
  `phase5-*-tests-details.log`: builds completos e resultados individuais.
- `phase5-boots-Debug.json`, `phase5-boots-Release.json`, seus `.negative.json`
  e as regressões `phase5-regression-phase1-*` até `phase5-regression-phase4-*`.
- `phase5-reference-programs.json`, `phase5-smoke.json` e
  `phase5-cli-Debug.json` / `phase5-cli-Release.json`.
- `phase5-image-audit.json`, `phase5-build-boundaries.json`,
  `phase5-clean-configure.log` e `phase5-clean-debug-build.log`.
- `phase5-change-manifest.json`: hashes das fontes anteriores/atuais, arquivos
  criados, executáveis, imagens, evidências e matriz verificada. Revalidar sem
  executar programas: `python tools/validate_phase5.py --snapshot snapshots/codex-before-phase5-20261016-201504`.

A pasta `build-codex-phase5-clean/` contém a compilação Debug independente,
com as cinco imagens de produção auditadas. LNK4281 é o aviso esperado da base
fixa em 2 MiB, exigida pelo carregador. Tentativas intermediárias foram
preservadas, inclusive a primeira execução que ainda não aguardava quatro ticks
e a inclusão posterior do stack probe necessário ao freestanding Debug.

## Limites e parada

| Recurso | Limite atual |
|---|---:|
| vCPUs | 1 |
| Processos de usuário / espaços incluindo kernel | 3 / 4 |
| Threads por processo | 4 |
| Handles por processo, incluindo os internos | 32 |
| Objetos no gerenciador de processos | 64 |
| Arena virtual por processo | 16 MiB |
| Páginas privadas comprometidas na arena por processo | 128 (512 KiB) |
| Custo fixo adicional por espaço | 21 frames |
| Grupos suplementares / entradas da DACL | 4 / 8 |
| Tamanho de cópia remota | 4 KiB |

As pilhas consomem a quota de páginas privadas. Objetos do escalonador também
têm seu próprio limite herdado da fase 4. Na fachada de processos, eventos e
timers criados são automáticos; eventos manuais continuam no escalonador e
nos objetos de conclusão. Esta imagem usa apenas a página RX de programas
próprios de teste. Ainda não carrega aplicações Windows arbitrárias.

Esta é uma ABI interna. Não implementa SIDs do Windows, logon, LSA,
impersonação, privilégios NT completos, jobs, auditoria SACL, sessões gráficas
ou paridade geral com estruturas/syscalls NT. A integração de permissões com
I/O, arquivos, registro e rede pertence às fases seguintes. Loader e runtime
são assunto da fase 8; compatibilidade ampliada com binários e drivers Windows,
da fase 12. A conclusão desta fase não significa conclusão do antivírus nem
garantia de que um programa não reconhecerá o ambiente.

O MiniNt do host permanece como referência; seus atalhos não atendem as
syscalls da nova imagem. Foram executados somente programas próprios e
exemplos confiáveis do projeto.

Ao concluir a fase 5, parar. A fase 6 só começa após nova solicitação.

## Referências de semântica

- [Microsoft: listas de controle de acesso](https://learn.microsoft.com/en-us/windows/win32/secauthz/access-control-lists)
- [Microsoft: controle de acesso](https://learn.microsoft.com/en-us/windows/win32/secauthz/access-control/)
- [Microsoft: Mandatory Integrity Control](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control)
- [Microsoft: direitos de acesso a processos](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/ProcThread/process-security-and-access-rights.md)

Essas referências orientam o modelo; as diferenças e limites da ABI própria
estão declarados acima.
