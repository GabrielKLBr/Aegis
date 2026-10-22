# Continuação da 12B: escopo completo

**Estado atual:** revisão full-04 aprovada no Linux: 47/47 Debug, 47/47 Release e
30/30 ASan/UBSan, com 368 fontes estáveis. A 12B permanece aberta para validação
Windows e integração aos perfis PE. [Relatório atual e entrega](PHASE-12B-RECURSOS.md).
As descrições de rascunho abaixo são históricas. A revisão já foi transferida
para o HD original: [registro de cópia e backup](HD-TRANSFER-20261020.md).

Em 20/10/2026, o usuário confirmou que o fechamento deve manter o escopo
completo: SMP real, volume maior e pasta de disco independente. A subfase
continua aberta. Não iniciar 12C nem rotular o perfil antigo de 1 vCPU/16 KiB
como encerramento completo.

Antes da autorização posterior de escrita, o NTFS original foi montado
normalmente e permaneceu somente leitura porque o Windows deixou metadados
em cache. Naquele momento não foi forçada a limpeza. A cópia que
preserva a entrega anterior está em /home/gabri/Projetos/GuardianAV-12B-20261020.
A implementação nova está em /home/gabri/Projetos/GuardianAV-12B-complete-20261020.
Até aquele momento, as fontes originais e os registros Windows permaneciam
intactos no NTFS. A transferência posterior está registrada no final deste arquivo.

Os XMLs Windows existentes contêm 62 aprovações distintas por configuração,
combinando execuções completas e retestes. Seus 347 hashes conferiam com a
entrega copiada. Isso não constitui execução Windows nova desta continuação.

LLVM 18.1.3 foi extraído no cache, sem instalação no sistema, para reproduzir
o fluxo nativo e evitar conflito de intrínsecos com o LLVM 23. As imagens
recompilaram. O GCC também recusou um campo agregado omitido: budgetRoot recebeu
inicializador padrão explícito, preservando seu comportamento. Essa alteração
exige nova identificação das fontes; os hashes Windows anteriores são históricos.

## Histórico do trabalho em andamento

O volume escalável usa blocos de 64 KiB, metadados redundantes e extents COW.
O código inicial está em guest/ScalableVolume.* e shared/guardian/ScalableStorage.h.
É um rascunho ainda sem aceitação, não habilitado nos builds do produto.
Faltam validar atomicidade, falta de espaço, exclusão e recuperação de falhas.

SMP precisa executar vCPUs reais, com topologia coerente, sincronização e
checkpoints de todos os processadores. A pasta de disco independente precisa
isolar propriedade, migração, recuperação, clone e cobrança agregada dos dois
locais. Nenhum desses itens foi declarado concluído.

Após a integração, repetir a matriz e a aceitação Windows da revisão final.
A fonte original só deve receber as mudanças quando o volume estiver liberado
para escrita pelo Windows.

## Validação intermediária preservada

A cópia de referência executou 42 testes Linux/KVM Debug. 41 passaram na
primeira execução. O teste guest_kvm continha a expectativa antiga de rejeitar
65 MiB, incompatível com o limite documentado de 1024 MiB da 12B. O caso negativo
passou a solicitar 1024 MiB + uma página. O reteste passou. XMLs em
../GuardianAV-12B-20261020/build-phase12b/linux-baseline-debug.xml e
linux-baseline-kvm-retest.xml. São evidências da referência, não da implementação nova.

O núcleo ScalableVolume passou no ASan/UBSan: 65931 verificações de conteúdo,
cota física, persistência, truncamento, exclusão e prefixos de falhas.
O SparseDisk passou em 252 verificações com ASan/UBSan, usando arquivo esparso
real no ext4: volume lógico de 2 GiB, arquivo convidado de 5 MiB, alocação física
inferior a 8 MiB, reabertura, snapshot, clone independente e devolução de espaço.
Resultados em /tmp/guardian-phase12b-scalable-volume.log e
/tmp/guardian-phase12b-sparse-disk.log; serão copiados para a pasta de evidências.

O novo ResourceKernel e ResourceSession estão em integração. SmpMachine cria
processadores adicionais na mesma partição/VM e captura o estado de cada um.
Isso ainda não valida SMP no agendador das aplicações PE existentes. A aceitação
Windows da revisão final e a pasta independente continuam pendentes.

## SMP integrado ao kernel de recursos

A primeira execução revelou a ausência da subfolha 1 da topologia CPUID 0xB na
tabela oferecida pelo KVM deste host. A projeção passou a materializar as
subfolhas 0, 1 e de término, com índices significativos, IDs APIC distintos,
um socket e um thread por núcleo. Não há aumento fictício de contagem.
ResourceBootTests passou com 1, 2, 3 e 4 processadores reais na mesma VM:
barreira compartilhada, contador com incremento interlocked, arquivo de 5 MiB,
checkpoint após 120 requisições e restauração de todos os processadores.
Resultado intermediário: 21 verificações de integração aprovadas. Logs ainda
serão preservados junto à matriz final. Isso comprova o kernel de recursos;
o agendador PE legado ainda usa uma CPU e não recebeu aceitação SMP.

## Integração e matriz congelada

Os seis grupos de pré-testes passaram: resource_budgets, scalable_volume,
sparse_disk, resource_boot, scalable_resource_manager e disk_location.
A revisão posterior acrescentou recuperação de blocos órfãos, consulta estável
de espaço, rejeição de backing com links físicos, checkpoint durante incrementos
concorrentes e verificação do MXCSR de cada CPU. A matriz final recompila tudo.

O status consolidado e os limites atuais estão em PHASE-12B-RECURSOS.md.
A matriz congelada está em build-phase12b/acceptance-20261020-full-01, com 368
arquivos fonte identificados. A revisão cruzada Windows passou sem avisos para
os módulos novos, mas não equivale a MSVC/WHP. Não encerrar 12B antes da aceitação
Windows e da revisão de integração dos perfis. A origem NTFS permanece intacta.

## Revisão após a primeira matriz limpa

acceptance-20261020-full-01 passou em Debug (47), Release (47) e ASan/UBSan (30),
com fontes estáveis. A revisão adicional encontrou o fluxo hardware-config do
novo contrato aceitando um perfil que sua inicialização não aplica. O gerenciador
passou a rejeitar essa configuração e a criação/importação inconsistente, com
verificação de que o arquivo de configuração anterior permanece idêntico.
As fontes corrigidas foram congeladas novamente e a matriz completa está em
acceptance-20261020-full-02. O resultado da primeira revisão não foi reetiquetado
como evidência da segunda. Sem aceitação Windows nova, a fase continua aberta.

## Proteção de propriedade e recuperação sob cota cheia

A revisão de diretórios passou a rejeitar discos aninhados dentro de outros discos
ou do estado de uma VM. A exclusão valida a árvore antes de mover o estado para
a transação de remoção. Gerações ativas também precisam ter proprietário correto;
a limpeza automática recusa arquivos/diretórios desconhecidos.

Foi reproduzido um bloqueio com RAM de 16 MiB, snapshot de 16 MiB e orçamento de
40 MiB: o guest concluiu o arquivo de 5 MiB, mas publicar sua imagem excedeu a cota.
Depois da saída do worker, status e drop-snapshot falhavam ao tentar a recuperação
antes de liberar espaço. A consulta agora retorna uma falha recuperável, conserva
a geração ativa e permite remover o snapshot que libera a cota. O teste confirma
recuperação, aumento do orçamento e novo boot. A prova anterior à correção está
em build-phase12b/quota-recovery-repro.json.

O watchdog do kernel de recursos repete o cancelamento até o reconhecimento,
seguindo o comportamento do boot anterior, para cobrir a corrida com a entrada no
hipervisor. O teste usa guests sem saídas com prazos de 1 e 40 ms e quatro CPUs.

As matrizes full-02 e full-03 foram substituídas durante essas correções e não
aceitam a revisão final. A próxima matriz congelada será full-04. Não somar os
resultados dessas tentativas aos resultados de uma execução completa e estável.

## Congelamento para full-04

O pré-teste final aprovou novamente os seis grupos: orçamento, filesystem,
backing, SMP, ciclo de vida e propriedade/migração. A recuperação sob cota cheia
foi testada pelo CLI, sem remover manualmente arquivos do catálogo.

A recuperação temporária agora faz sua própria reserva de memória/CPU. O
encerramento de uma VM pausada sem worker extrai o disco por streaming; não
materializa toda a RAM apenas para obter o backing. Os limites de leitura e
restauração acompanham a cota de crescimento configurada antes de alocar buffers.
O backing também limita a representação materializada de seus blocos mapeados,
vinculando o tamanho dos snapshots à reserva contabilizada. A disponibilidade
publicada considera esse custo além da alocação física.

Os nomes internos dos discos e gerações foram encurtados e os diretórios de disco
ficam ocultos ao enumerador de VMs. Foi testada a escolha da própria pasta do
catálogo como destino sem confundir um disco com uma VM. Isso também reduz o
comprimento dos caminhos de teste no Windows.

As fontes não serão alteradas durante a full-04. Depois dela, publicar apenas o
resultado efetivo e conferir os hashes do pacote. A 12B permanece aberta para a
aceitação Windows e a integração aos perfis PE; manter o escopo completo escolhido
pelo usuário, sem substituir essas pendências por um aceite do contrato de teste.

## Fechamento do registro desta execução

A full-04 passou integralmente (47 Debug, 47 Release, 30 ASan/UBSan), com fontes
estáveis do começo ao fim. O próximo trabalho continua sendo 12B: aceitar no
Windows a revisão entregue e integrar os recursos aos perfis de aplicações PE.
Não declarar a fase concluída apenas com o kernel `resources`.
O relatório principal contém os caminhos dos pacotes, procedimento Windows,
limites implementados, XMLs e resultado consolidado. O original NTFS não mudou.

## Transferência para a pasta original no HD — 20/10/2026

O usuário autorizou a escrita e a desmontagem, confirmando que não havia
estado importante a preservar da sessão Windows. Foi criado e verificado um
backup dos 409 arquivos existentes alcançados pelo pacote antes de qualquer
substituição. A montagem normal continuou recusando escrita; após autenticação
de administrador, `ntfsfix --clear-dirty` corrigiu o estado incompleto e reiniciou
o diário NTFS. A montagem seguinte confirmou `rw` no volume de UUID
`01DB04DC26902A20`.

O pacote validado contém 505 arquivos: 126 foram gravados (30 substituições e
96 novos), e 379 já eram idênticos. Todos foram conferidos após a cópia, incluindo
os hashes dos 368 arquivos de código da full-04. Os diretórios históricos de
build e as evidências Windows anteriores foram preservados. Os relatórios
Markdown atuais também foram atualizados na pasta original.

Os pacotes, checksums, backup e seu manifesto estão em
`build-phase12b/delivery-20261020` no projeto original. O registro detalhado está
em `build-phase12b/original-hd-transfer-20261020.json` e o resumo em
[HD-TRANSFER-20261020.md](HD-TRANSFER-20261020.md). A 12B permanece aberta para
aceitação Windows/MSVC/WHP da revisão e integração dos recursos aos perfis PE.
