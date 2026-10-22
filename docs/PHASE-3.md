# Fase 3 — Memória no kernel convidado

Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Imagem: `guardian-kernel-memory.gk`, versão 0.3.0, x64, uma vCPU, WHP.
Estado: concluída em 16/10/2026. A fase 4 não foi iniciada.

## Implementação

O gerenciador executa em CPL0 dentro do convidado. O host fornece RAM e aplica
as permissões físicas do WHP; reserva de endereços, escolha de frames, tabelas
de páginas e proteção virtual são responsabilidade do convidado. O código
freestanding não usa alocadores, contêineres ou APIs do Windows host.

`guest/MemoryManager.cpp` adapta as regras dos modelos existentes
`src/microvm/host/GuestVirtualMemory.h` e `GuestHeap.h`. O código anterior
permanece como referência semântica e continua sendo testado. As imagens das
fases 1 e 2 continuam disponíveis; a nova imagem reutiliza as tabelas de CPU,
transições CPL0/CPL3, syscalls e tratamento de exceções da fase 2.

### Frames físicos e tabelas

- O alocador admite exclusivamente regiões `Available` do mapa de boot
  previamente validado. Boot, imagem, pilha do kernel, região de bootstrap de
  usuário e o primeiro MiB ficam fora da lista livre.
- Frames de dados e frames de tabelas têm contabilização e tipos distintos.
  Liberação inválida, com tipo incorreto ou repetida, é recusada. Páginas são
  zeradas na alocação e liberação. Há alocação de blocos físicos contíguos para
  arenas internas e uma cota determinística para testes de falta de recursos.
- Cada espaço tem PML4, PDPT, diretórios e tabelas próprios. A região baixa
  possui um mapeamento de identidade supervisor; código de bootstrap de usuário
  é compartilhado como RX e pilha/dados de bootstrap são privados por espaço.
- A arena privada de usuário começa em `0x40000000`. Commit cria tabelas e
  frames de dados conforme necessário. Trocar espaço escreve CR3; alterações
  no espaço ativo recarregam CR3 para invalidar traduções antigas. Não há PCID,
  páginas globais, múltiplas vCPUs ou necessidade de TLB shootdown nesta etapa.
- Destruir o espaço ativo é proibido. O teste final conserva deliberadamente
  um espaço com cinco frames de tabelas e 16 de pilha/dados. Esses 21 frames
  são recursos vivos contabilizados, não vazamento.

### Operações virtuais

| Operação | Comportamento |
|---|---|
| Reserva | Granularidade de 64 KiB na base; extensão arredondada em páginas de 4 KiB. Registra a região sem consumir frames. Conflitos e limite de metadados têm falha explícita. |
| Commit | Arredonda o intervalo em páginas; exige uma reserva única que contenha o intervalo. Aloca e zera os frames. Recommit preserva conteúdo e proteção das páginas já comprometidas. |
| Decommit | Retorna páginas ao alocador e conserva a reserva. Tamanho zero na base da reserva abrange a região inteira. |
| Liberação | Exige a base exata e tamanho zero; remove reserva, páginas e tabelas vazias. |
| Consulta | Distingue livre, reservado e comprometido; informa base de alocação, proteção original, proteção atual e extensão até a próxima mudança. |
| Proteção | Valida todo o intervalo antes de mudar qualquer página. Exige todas as páginas comprometidas e retorna a proteção anterior da primeira. |

As proteções suportadas são sem acesso, leitura, leitura/escrita e
execução/leitura, com guarda opcional nas três últimas. Escrita e execução
simultâneas são recusadas. Intervalos inválidos, overflow e cruzamento de
reservas não alteram o estado anterior. Falta de frames durante commit desfaz
apenas as páginas novas e remove tabelas que ficaram vazias. Reservas e dados
já existentes sobrevivem à falha.

### Page faults e isolamento

O processador executa leituras, escritas e chamadas em CPL3 para testar as
permissões. A IDT do convidado recebe o page fault, registra endereço e código
de erro e termina a entrada de usuário de forma controlada. Uma nova entrada
continua funcionando. Fault inesperado de kernel segue o encerramento fatal
diagnosticado da fase 2.

Guarda é uma condição de uso único. O primeiro acesso recebe page fault e
consome a guarda; a operação é encerrada e registrada. A próxima entrada pode
acessar a página com a proteção restaurada. Ainda não existe entrega de
`STATUS_GUARD_PAGE_VIOLATION` a um dispatcher de exceções NT em modo usuário.

Dois espaços mapeiam o mesmo endereço virtual para dados diferentes. O código
de usuário não consegue ler a imagem do kernel nem o alias físico dos dados
do outro espaço. A troca de CR3 e a remoção de acesso são exercitadas após o
endereço já ter sido usado, verificando a invalidação de traduções anteriores.

### Heap e fronteira com WHP

O heap interno usa uma arena de frames obtidos pelo alocador convidado,
alinhamento de 16 bytes, divisão e união de blocos, alocação zerada e realloc
que preserva a alocação original quando não consegue crescer. Seus metadados
ficam na arena supervisor, sem acesso de CPL3. Não é ainda a implementação dos
exports Rtl/Heap do Windows dentro do novo kernel.

Permissões virtuais nas PTEs e permissões físicas WHP são controles separados.
O dispositivo `0xED`, definido em `shared/guardian/Memory.h`, aceita requisições
fixas de uma página em `0x108000` e recebe o relatório em `0x109000`. O host
valida versão, tamanho, sessão, sequência, alinhamento, limites, direitos
R/RW/RX e pertencimento a uma região `Available`. Nenhum ponteiro fornecido
pelo convidado é convertido em ponteiro do host. Páginas de boot, kernel e
regiões reservadas não podem receber novas permissões por esse dispositivo.

Transições para RX alteram tanto a PTE quanto a permissão física; antes de
zerar e reciclar o frame, a permissão física volta a RW. Requisição inválida
ou falha do WHP encerra a execução, sem simular sucesso. Nos testes nativos,
falhas transitórias de concessão também exercitam a reversão do estado.
As proteções físicas existentes da imagem e dos dados de boot são preservadas.

## Compilação e execução

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild

.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-memory.gk
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-memory.gk --memory --ram-mib 64 --repeat 2 --report .\build-codex\phase3-cli-Release.json
```

`--memory` seleciona explicitamente o contrato da fase 3 e habilita a base de
CPU. A imagem é carregada no WHP; ela não é executada como processo Windows.
Não há QEMU nem alteração nas configurações de boot ou segurança do host.
Compilador e SDK são os mesmos da fase 2. O aviso LNK4281 é esperado para a base
fixa do kernel em 2 MiB, definida pelo contrato do carregador.

## Validação e evidências

Critérios reproduzíveis: `config/phase3-acceptance.json`.

O build completo e os 21 testes passaram em Debug e Release, sem testes
ignorados. As duas configurações executaram 80 boots da fase 3 e outros 160 de
regressão das fases 1 e 2. Os sete programas de referência passaram em cada
configuração. As três imagens de produção foram inspecionadas pelo carregador
estrito; uma compilação Debug nova, sem BUILD_TESTING, também compilou e
auditou as três. O kernel de memória ocupa 430080 bytes em Debug e 421888 em
Release. Cada configuração produziu 50 executáveis e cinco imagens de kernel
(três de produção e duas negativas da fase 2).

| Configuração | RAM | Boots da fase 3 | Tempo por boot observado |
|---|---|---|---|
| Debug | 16 MiB | 20 | 76,175–88,307 ms |
| Debug | 64 MiB | 20 | 295,859–311,536 ms |
| Release | 16 MiB | 20 | 73,655–86,793 ms |
| Release | 64 MiB | 20 | 268,526–330,636 ms |

Os tempos incluem a preparação do host e o autoteste, inclusive zerar e
esgotar a RAM livre. São medições desta máquina, sem garantia de latência em
tempo real. A suíte levou 15,43 s em Debug e 16,29 s em Release.

- `guest_memory`: executa o mesmo núcleo freestanding com backing de RAM
  nativo. Cobre arredondamento, consulta, proteção, guarda, falta de frames e
  metadados, rollback com conteúdo anterior, falhas de concessão física,
  esgotamento real, limites de espaços, 1.000 ciclos, heap e contrato do canal.
- `guest_memory_boot`: 20 boots com 16 MiB e 20 com 64 MiB por configuração.
  Cada boot executa 150 entradas em CPL3: 142 completam e oito page faults são
  contidos. A máscara final de verificações é 4095.
- Cada boot contém uma guarda consumida, 30 falhas de alocação esperadas,
  esgotamento e recuperação de todos os frames disponíveis, 128 ciclos de
  reserva/commit/liberação e cinco verificações do heap. O pico de frames é
  igual ao total inicialmente livre; o estado final retém apenas os 21 frames
  do espaço ainda ativo.
- O canal do host é exercitado com 18 cenários externos inválidos por
  configuração: formato, versão, sessão, replay, GPA inválido ou protegido,
  direitos W+X, tamanho, bits reservados, IN, OUT largo, ordem e relatório.
- A regressão preserva 40 boots da fase 1 e 40 da fase 2 por configuração,
  incluindo os respectivos cenários negativos e os dez testes do MiniNt.

Os relatórios novos usam o prefixo `phase3-`, inclusive nas regressões. Os
JSONs, logs e manifestos das fases anteriores não são sobrescritos. O backup
dos oito arquivos anteriores alterados está em
`snapshots/codex-before-phase3-20261016-170923/`; o arquivo
`historical-evidence.json` registra os hashes anteriores para comparação final.
Os 99 arquivos históricos verificados mantêm os hashes anteriores.

Evidências em `build-codex/`:

- `phase3-full-debug-script.log`, `phase3-full-release-script.log` e os logs
  correspondentes `phase3-*-tests-details.log`.
- `phase3-boots-Debug.json`, `phase3-boots-Release.json` e seus
  `.negative.json`; regressões em `phase3-regression-phase1-*` e
  `phase3-regression-phase2-*`.
- `phase3-reference-programs.json`, `phase3-smoke.json` e
  `phase3-cli-Debug.json` / `phase3-cli-Release.json`.
- `phase3-image-audit.json`, `phase3-build-boundaries.json`,
  `phase3-clean-configure.log` e `phase3-clean-debug-build.log`.
- `phase3-change-manifest.json`: fontes alteradas e novas, hashes, backup e
  matriz de resultados. O verificador pode ser repetido sem executar programas:
  `python tools/validate_phase3.py --snapshot snapshots/codex-before-phase3-20261016-170923`.

## Limites e parada

O contrato atual suporta até quatro espaços e 64 reservas por espaço, com
arena privada de 16 MiB. A matriz de hardware cobre RAM de 16 e 64 MiB. As
estruturas aceitam o teto de RAM de 1 GiB da ABI, que não foi medido nessa
matriz. A pilha de usuário continua fixa em 60 KiB utilizáveis, com guarda de
4 KiB. O heap tem tamanho de arena definido pelo chamador.

As operações são APIs internas do kernel. Processos, identificação de donos,
handles, syscalls de memória compatíveis com NT e tratamento de exceções NT
ficam nas fases correspondentes. O commit é imediato e zerado; não há swap,
pagefile, copy-on-write ou mapeamento de arquivos nesta etapa. O mapeamento de
identidade do kernel continua supervisor, com proteções físicas da imagem
aplicadas pelo host. Há uma vCPU e não há concorrência entre threads.

A conclusão desta fase não implica compatibilidade com aplicações ou drivers
Windows nem conclusão do antivírus. Ao concluir a fase 3, parar. A fase 4 só
começa após nova solicitação do usuário.

## Referências técnicas

- [Manuais Intel: paginação, CR3, permissões e page faults](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Estados das páginas virtuais](https://learn.microsoft.com/en-us/windows/win32/memory/page-state)
- [Reserva e commit com VirtualAlloc](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc)
- [Decommit e liberação com VirtualFree](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree)
- [Guard pages](https://learn.microsoft.com/en-us/windows/win32/memory/creating-guard-pages)
- [WHP: mapeamento de memória física do convidado](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange)
