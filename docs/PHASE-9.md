# Fase 9 — Snapshots completos e gerenciamento de VMs

Iniciada em 18/10/2026 por solicitação explícita do usuário.
**Implementação concluída para a matriz própria; aceitação Linux aprovada;
aceitação nativa Windows pendente.** A pendência Windows da fase 8 permanece registrada.
A fase 10 foi autorizada posteriormente em 18/10; sua implementação e evidências estão em [PHASE-10.md](PHASE-10.md). Os números abaixo pertencem à aceitação histórica da fase 9.

## Resultado em 18/10/2026

Evidência final:
`build-phase9/validation-20261018-103055-f78507d1/validation.json`.
Estado registrado: `passed` / `awaiting_native_windows_acceptance`.

| Matriz executada no Linux | Resultado |
| --- | --- |
| Debug, Release e ASan/UBSan | 23 CTests por configuração, 69 execuções aprovadas, sem testes ignorados |
| Regressão das fases 1–8 | 132 boots reais com 11 imagens, duas configurações e 16/32/64 MiB |
| Fatal/IST | Quatro execuções corretamente recusadas como sucesso |
| Snapshot do console completo | 81 retomadas/boots concluídos e 27 pausas conferidas, nas três configurações |
| Aplicações pelo gerenciador | 24 boots concluídos, incluindo cópias simultâneas e recuperação de falha |
| Snapshot e estado arquitetural | 221 verificações por configuração, incluindo CPUID e interrupção pendente |
| Catálogo | 64 transações interrompidas por configuração e leitura durante publicação atômica |
| Primeiro commit do volume | 589 verificações por configuração, incluindo 52 prefixos de escrita/flush |
| Preparação de fontes Windows | 11 módulos compilados para objetos PE/COFF por LLVM nativo; sem execução Windows |
| Aceitação Windows/MSVC/WHP | **Pendente**: 43 CTests por configuração e comparações nativas |

Os 23 CTests Debug foram executados na árvore nova
`validation-20261018-102029-2fe9ca3a`. Depois, o boot completo Debug de 64 MiB
alcançou o prazo de cinco segundos da regressão. O prazo dessa matriz passou
para 20 segundos. O verificador conferiu que somente o script/contrato de
aceitação mudou, validou hashes dos binários/relatórios e reaproveitou os testes
Debug aprovados. Todos os 132 boots foram repetidos; Release e Sanitize foram
compilados e testados em árvores novas. O manifesto registra essa proveniência.
Não houve alteração posterior nas fontes compiladas durante a aceitação final.

A primeira tentativa, `validation-20261018-094050-657b339e`, identificou uma
consulta transitória de status durante substituição de arquivo no volume montado.
A leitura agora repete de forma limitada consultas de ausência, mantendo a
checagem do tipo do arquivo aberto. Foi acrescentado um teste concorrente de
publicação/leitura. As tentativas anteriores e seus erros permanecem preservados.

## Entrega

O novo `guardian-vm` cria, inicia, pausa, retoma, reinicia, desliga, clona e
exclui VMs. Configura RAM e prazo de execução, importa imagens próprias `.gk`
e bases de disco de 64 KiB e mantém snapshots nomeados. Usa processos nativos
separados, uma vCPU por VM e diretórios próprios, com controles de propriedade
e exclusão mútua. Windows usa MSVC/WHP; Linux usa executáveis ELF/KVM.
O `SnapshotManager` antigo do MiniNt continua como referência histórica;
o fluxo novo usa `SessionSnapshot` e o kernel próprio das fases 1–8.

Um snapshot inclui uma geração consistente de:

| Estado | Captura/restauração |
| --- | --- |
| CPU | Registradores gerais, controle, segmentos/tabelas, depuração, perfil CPUID virtual, MSRs usados pelo kernel, XSAVE/x87/XMM e interrupções pendentes |
| Memória | RAM completa, tabelas de páginas e permissões físicas por página |
| Kernel | Escalonador, pilhas, processos, objetos, handles, tokens, loaders, TEB/TLS, heap, arquivos e registro, conservados na RAM |
| Relógio | Tempo virtual acumulado, próxima interrupção, contadores, confirmação de timer e estado de interruptibilidade |
| Protocolos/dispositivos | Sequências, relatórios, console, fila do canal, bloco em RAM e estado de falha dos dispositivos |
| Disco | Prefixo integral e sincronizado do overlay, incluindo sua referência à base imutável |

A captura ocorre com a vCPU parada e sem uma troca de dispositivo em execução.
No KVM, uma saída de I/O pendente é concluída antes da leitura de registradores,
sem deixar o convidado executar novas instruções. Os callbacks e ponteiros do
host são reconstruídos; não são serializados. O tempo pausado fica fora do
relógio virtual, e o TSC salvo é reinstalado antes de retomar a execução.

O arquivo `.gvs` tem versão, limites de tamanho e checksum. A restauração
valida backend, CPU, configuração, imagem e disco numa sessão candidata nova.
A sessão anterior só é substituída depois dessa preparação. Misturar RAM de um
snapshot com o disco de outra geração é recusado. Alterar a RAM configurada
pode tornar snapshots anteriores incompatíveis; a recusa conserva a VM atual.

## Operações e persistência

| Operação | Comportamento |
| --- | --- |
| `create` | Valida e copia a imagem/base para uma nova VM; recusa nome existente ou imagem inválida |
| `start` | Boot novo com identidade de sessão nova; `--paused` prepara a VM sem executar o convidado |
| `pause` / `resume` | Interrompe a vCPU, salva um checkpoint consistente e retoma desse estado |
| `snapshot` | Grava um ponto nomeado; se a VM estava executando, ela retoma após a captura |
| `restore` | Prepara o ponto salvo e deixa a VM pausada para inspeção/retomada |
| `restart` | Descarta CPU/RAM do boot anterior e inicializa novamente, respeitando a política do disco |
| `shutdown` | Para a vCPU e sincroniza o disco; não é uma solicitação de saída cooperativa de cada aplicação |
| `clone` | Cria identidade e arquivos próprios; VM ativa/pausada é clonada pelo checkpoint, VM desligada pelo disco |
| `configure` | Altera RAM e prazo de uma VM desligada; invalida seu checkpoint interno anterior |
| `delete` | Exige ausência de processo executor ativo; remove somente a VM identificada e suas cópias próprias |

No modo **persistente**, o desligamento/reinício conserva o disco. Se o processo
executor cair, o gerenciador recupera o último checkpoint inteiro e também
conserva os registros válidos do disco mais recente. `resume` escolhe o estado
completo do checkpoint, incluindo o disco daquele ponto; `restart` faz um boot
novo usando o disco persistente recuperado. Essa distinção evita combinar uma
RAM antiga com escritas posteriores. Registros incompletos de cauda são
recuperados pelo formato de overlay existente.

No modo **descartável**, cada boot novo parte da base; alterações do boot
anterior não são promovidas ao disco persistente. Retomar/restaurar um snapshot
continua preservando a geração completa daquele ponto. A base nunca é escrita.

Publicações de arquivos usam arquivo temporário, sincronização e troca atômica.
Criar, clonar e excluir diretórios usa intenção durável: uma operação interrompida
é recolhida sem adotar uma VM incompleta nem alcançar outros diretórios.
Uma lease distingue um executor ativo de um status deixado após uma falha.
Comandos têm confirmação e prazo; uma operação ainda pendente é indicada,
não substituída silenciosamente por outra solicitação.

A validação de reinício encontrou e corrigiu uma falha antiga do primeiro commit
do volume: dados parciais antes do primeiro superbloco válido eram recusados no
boot seguinte. Um marcador durável no setor reservado de offset 3072 agora
identifica a inicialização incompleta. Ele só autoriza reiniciar a formatação
quando ainda não existe cabeçalho de imagem; depois do commit é removido.
Volumes corrompidos sem marcador válido continuam recusados. O formato de
base/overlay e as sequências dos volumes já existentes permanecem compatíveis.

## Compilar e usar

No Linux, com KVM acessível e LLVM nativo disponível:

```bash
python3 tools/build_guest_linux.py --config Release --output build-phase9/guest-Release
GUARDIAN_LINUX_BUILD_DIR="$PWD/build-phase9/Release" ./Build-Linux.sh Release
```

Exemplo pelo pacote Linux extraído, a partir de sua pasta:

```bash
./guardian-vm --root "$PWD/vms" create demo kernels/guardian-kernel-complete.gk storage --ram-mib 32 --persistent
./guardian-vm --root "$PWD/vms" start demo --paused
./guardian-vm --root "$PWD/vms" snapshot demo inicial
./guardian-vm --root "$PWD/vms" resume demo
./guardian-vm --root "$PWD/vms" pause demo
./guardian-vm --root "$PWD/vms" clone demo copia
./guardian-vm --root "$PWD/vms" restore demo inicial
./guardian-vm --root "$PWD/vms" shutdown demo
```

Outros comandos: `status <id>`, `list`, `drop-snapshot <id> <nome>`,
`configure <id> --ram-mib N --timeout-ms N` e `delete <id>`.
O catálogo deve ser novo/vazio ou já pertencer ao gerenciador. Nomes de VMs e
snapshots começam com letra minúscula, têm até 32 caracteres e usam letras,
números, `_` e `-`; nomes de dispositivos e caminhos são recusados.

No Windows, `Build-Kernel.ps1` inclui `guardian-vm.exe` em `guardian_reference`.
O executável fica em `build-codex\Release\`; as imagens ficam em
`build-codex\guest\Release\`. Os mesmos comandos aceitam caminhos Windows e
nomes de diretório Unicode. O verificador completo usa uma árvore nova e não
substitui as evidências históricas de `build-codex`.

## Aceitação

```bash
python3 tools/validate_phase9.py
```

No Windows nativo, usar `python tools\validate_phase9.py` com Visual Studio
2022 C++ x64, Windows SDK, CMake 4+, Python e WHP funcionando.

O contrato é `config/phase9-acceptance.json`. A matriz exige 23 CTests em cada
configuração Linux (Debug, Release e ASan/UBSan), ou 43 CTests em cada
configuração Windows (Debug/Release), sem falhas ou testes ignorados.
Também exige 153 + 59 comparações nativas de unwind no Windows. A regressão
completa usa prazo de 20 segundos por boot, incluindo as imagens Debug.

Critérios por aplicação/cenário:

| Prova | Comportamento exigido |
| --- | --- |
| Probe x64 próprio | Retoma em outra VM os 16 GPRs, XMM0/XMM15, x87/MXCSR, FS/GS/kernel-GS, CPUID, flags, tabelas e permissões; entrega exatamente uma interrupção pendente |
| Console completo em snapshots | 16/32/64 MiB × três pontos de I/O × duas retomadas independentes e um boot novo do disco; repete os critérios de aplicações da fase 8 |
| Snapshot/restauração | RAM, metadados, permissões e prefixo do disco coincidem com o ponto salvo; arquivos corrompidos/incompatíveis são recusados |
| Gerenciador | Duas VMs executam simultaneamente; clonagem e restauração de uma não alteram disco/relatório da vizinha |
| Disco | Reinício persistente recupera a sequência; dois boots descartáveis partem do zero; a base importada permanece intacta |
| Falha do executor | Interrupção real conserva checkpoint e registros recentes; testa retomada do checkpoint numa cópia e boot novo do disco recuperado |
| Inicialização do volume | 52 prefixos de escrita/flush; após falha existe o estado vazio ou o commit completo de arquivo e registro, nunca uma mistura |
| Catálogo | 64 transações de criar/clonar interrompidas, recuperação de intenções, nomes/links inválidos e preservação de arquivos externos |
| Limites | Quatro VMs residentes; quinta recusa; alterações de recursos e exclusão de VM ativa recusadas |
| Regressão | 132 boots das 11 imagens anteriores e quatro recusas de kernel fatal/IST no Linux |

As evidências ficam em `build-phase9/validation-<data>-<id>/validation.json`,
com JUnit, logs, hashes, relatórios de boots e confirmações das operações.
O verificador só marca a fase completa quando encontra aceitação Linux e
Windows para os mesmos hashes das fontes. Resultado de uma plataforma não é
apresentado como validação da outra.

## Pacote Linux verificado

`dist/guardianav-phase9-linux-x86_64-20261018.tar.gz` inclui `guardianav`,
`guardian-kvm`, `guardian-vm`, assinaturas, 13 imagens, documentação e evidências.
SHA-256: `a789fd35e145eecda30bfa3d5446aafb49cc062248611cf17cb52da9c5df491c`.

Os 78 arquivos foram extraídos e conferidos por hash. O gerenciador funcionou
a partir de outra pasta, criou uma VM pausada, salvou um snapshot, clonou e
executou as duas cópias até o resultado esperado. As bases ficaram intactas,
os recursos de processo ficaram zerados e o catálogo de teste foi excluído.
A CLI do scanner também funcionou fora da pasta do pacote. Não houve chamada
a Wine ou QEMU. Evidência: `package-validation.json` na pasta final acima.
Os pacotes e logs anteriores permanecem preservados.

## Limites

Uma vCPU por VM; 16–64 MiB de RAM; 16 VMs cadastradas, quatro residentes e
256 MiB de RAM de convidados por catálogo; oito snapshots nomeados por VM;
prazo de 100–30.000 ms por trecho de execução. A restauração do relógio neste
perfil aceita menos de uma hora virtual acumulada. O espaço de snapshots é limitado
por esses números e pelo tamanho máximo do estado; não é uma quota geral do
heap do processo host. O overlay mantém o limite anterior de 4.096 registros.

Snapshots de CPU são específicos do backend, CPU/configuração e revisão
suportados. Não há conversão KVM/WHP ou promessa de migração arbitrária entre
máquinas. Importação cobre os kernels próprios `.gk` e a base de 64 KiB; não
é importação de VHD/VMDK/ISO nem execução de PEs arbitrários no host.
O KVM continua publicando `secondStageExecuteProtection: false`; é o backend
de desenvolvimento, com NX nas tabelas do convidado. O produto Windows
continua usando as permissões físicas do WHP.

Originais preservados: `snapshots/codex-before-phase9-20261018/manifest.json`.
Referências técnicas: [conclusão de I/O e estado de CPU no KVM](https://docs.kernel.org/virt/kvm/api.html),
[registradores WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetvirtualprocessorregisters) e
[XSAVE no WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetvirtualprocessorxsavestate).
