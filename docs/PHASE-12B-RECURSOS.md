# Contrato de recursos da 12B — continuação de 20/10/2026

A 12B permanece aberta até a aceitação Windows/MSVC/WHP da revisão final e a
revisão do escopo completo. A 12C foi autorizada posteriormente em 21/10/2026:
[matriz e estado atual](PHASE-12C.md). A implementação de referência da 12B está em
`/home/gabri/Projetos/GuardianAV-12B-complete-20261020`; a entrega anterior foi
preservada em `/home/gabri/Projetos/GuardianAV-12B-20261020`.

Após autorização do usuário, o NTFS foi desmontado, seu diário e estado de
encerramento incompleto foram corrigidos e o volume foi montado com leitura e
escrita. A revisão aceita foi copiada para a pasta original no HD e os 368 hashes
das fontes foram conferidos. O backup anterior está preservado. Detalhes em
[Transferência para o HD](HD-TRANSFER-20261020.md). Nenhuma execução Windows
foi substituída por Wine.

## O que foi implementado

O novo contrato `resources`, com imagem própria `guardian-kernel-resources.gk`,
executa de 1 a 4 processadores reais em uma única VM/partição, compartilhando
RAM. Os backends KVM e WHP criam cada processador; CPUID apresenta um socket,
um thread por núcleo e IDs APIC distintos. O teste executa uma barreira e
incrementos interlocked concorrentes. Cada núcleo configura um MXCSR próprio.
O checkpoint captura registradores, XSAVE/FPU e eventos de cada processador,
com RAM compartilhada capturada uma vez na representação persistente.
Cancelamento e pausa interrompem e recolhem todos os threads de execução.

A imagem declara sua capacidade explicitamente. Selecionar o contrato novo
para uma imagem antiga é recusado antes de inicializar a VM. Perfis PE,
gráficos, de rede, hardware e o contrato `storage` anterior continuam com sua
matriz de uma CPU. Não há declaração de um agendador PE multiprocessador nesta
continuação. Esse ponto precisa permanecer explícito na revisão de escopo.

A RAM aceita 16 a 1024 MiB. O modo máximo respeita esse teto implementado, a
memória disponível e a reserva operacional do sistema. A reserva global por
usuário continua comum aos catálogos, limita quatro workers e preserva uma CPU
lógica em hosts com mais de uma CPU. O custo do novo contrato é oito vezes a
RAM, mais 128 MiB, mais quatro vezes a cota de crescimento do backing para
cobrir materializações de snapshot e substituição. A representação dos blocos
mapeados também é limitada pela cota de crescimento, mantendo os buffers de
snapshot dentro da reserva. A extração de disco de um checkpoint usa streaming
quando a RAM não precisa ser restaurada. Os perfis antigos conservam
seu cálculo anterior, sem esse custo adicional de disco.

O filesystem próprio novo aceita capacidade lógica de 1 a 8 GiB, com padrão de
2 GiB, 32 arquivos em namespace plano e até 256 MiB por arquivo. Usa blocos de
64 KiB, dois conjuntos de metadados com checksum, publicação COW, intenção de
formatação e recuperação de blocos órfãos após interrupção. Extensões sem dados
leem zeros. O diretório e o registro do filesystem antigo não foram convertidos
silenciosamente para esse formato. Dados do convidado não viram caminhos do host.

O backing é esparso, com cota física configurável de 4 a 256 MiB, padrão de
64 MiB. Essa cota limita os dados que podem ser materializados no volume lógico;
a capacidade lógica não constitui promessa de alocação física integral.
O controle agregado de armazenamento inclui estado, imagem, backing, gerações
ativas, snapshots e temporários. Preserva 64 KiB de controle e 256 MiB livres
na unidade host. Arquivos esparsos são cobrados pela alocação física; snapshots
e arquivos comuns pela maior medida entre tamanho lógico e físico.

No Windows, a medição de arquivos esparsos usa `GetCompressedFileSizeW`, conforme
[a documentação da Microsoft](https://learn.microsoft.com/en-us/windows/win32/fileio/obtaining-the-size-of-a-sparse-file).
A validação NTFS da implementação permanece pendente. As unidades de alocação
do host precisam dividir 64 KiB. Backings comprimidos existentes, links físicos,
links simbólicos e pontos de redirecionamento não são adotados como discos.

`resources` informa capacidade, cota, alocação física, total cobrado e CPU/RAM
solicitadas, admitidas e provisionadas. As medidas do filesystem são obtidas
por inspeção somente leitura dos metadados no processo proprietário da VM,
vinculadas à geração do checkpoint. São publicadas quando conhecidas e estáveis;
caso contrário, aparecem como `null`, com `guestSpaceKnown: false`. O disponível
é limitado novamente pela cota, backing, orçamento agregado e espaço host atual.

## Pasta independente e recuperação

`--disk-folder` escolhe uma pasta existente. O gerenciador cria nela uma subpasta
própria com identidade aleatória e marcador de propriedade. A pasta escolhida
e seus outros arquivos são preservados. Os nomes internos curtos não entram na
enumeração de VMs, mesmo quando o destino escolhido é a pasta do catálogo. Sem a opção, o catálogo mantém essas
subpastas em `.disks`, fora do diretório de estado da VM.

Com a VM parada, `disk-folder` copia a imagem persistente para uma nova subpasta
própria, verifica reservas/cotas e publica a configuração por último. A intenção
durável de migração permite recuperar uma interrupção escolhendo a localização
referenciada pela configuração confirmada. Origem e destino são verificados por
propriedade antes de qualquer limpeza. Não é permitido criar o disco dentro de
outro disco gerenciado ou do estado de uma VM. A limpeza de gerações valida
seu proprietário e recusa diretórios, links e arquivos desconhecidos. Clones recebem local e proprietário
independentes. A exclusão da VM remove apenas sua subpasta própria.

O worker usa gerações próprias nessa localização. Se morrer, a recuperação
preserva o disco ativo antes de remover a geração abandonada e retoma somente
um checkpoint completo. Checkpoint e backing são validados como a mesma geração.
Se não houver cota para publicar a imagem recuperada, o estado permanece em falha
recuperável e a geração ativa é preservada. A consulta de estado e a exclusão de
snapshots continuam disponíveis para liberar espaço; uma falha de gravação não
limpa a única cópia recuperável. O teste reproduz esse bloqueio com cota agregada
de 40 MiB, libera um snapshot, recupera o disco e retoma após ajustar o orçamento.

## Uso no Windows

Em um terminal x64 do Visual Studio 2022, na raiz da cópia atualizada:

```text
python tools\validate_phase12b.py --output build-phase12b\windows-full-20261020 --generator "Visual Studio 17 2022"
```

O diretório de saída precisa estar vazio ou ainda não existir. O script compila
Debug e Release com o compilador Microsoft e executa todos os testes registrados,
preservando logs, JUnit, hashes das fontes e o resultado de cada configuração.
Ele recusa mudanças nas fontes durante a aceitação e não encerra a fase sozinho.

Exemplo com o executável Windows compilado e uma pasta de discos já existente:

```text
guardian-vm.exe --root D:\Laboratorio create recursos caminho\guardian-kernel-resources.gk resources --ram-mib 128 --vcpus 4 --disk-folder E:\DiscosGuardian --disk-capacity-gib 2 --disk-growth-mib 64 --storage-limit-mib 1024
guardian-vm.exe --root D:\Laboratorio resources recursos
guardian-vm.exe --root D:\Laboratorio start recursos --paused
guardian-vm.exe --root D:\Laboratorio snapshot recursos inicial
guardian-vm.exe --root D:\Laboratorio resume recursos
guardian-vm.exe --root D:\Laboratorio disk-config recursos --volume-quota-mib 2048 --disk-growth-mib 128
guardian-vm.exe --root D:\Laboratorio disk-folder recursos E:\OutraPasta
```

As duas últimas operações exigem a VM parada. Alterar a cota lógica exige uma
VM sem checkpoints que dependam da configuração anterior. Reduzir a cota física
abaixo do disco recuperável é recusado. A capacidade lógica é selecionada na
criação; redimensionar um filesystem existente não está implementado.

## Evidências e fechamento

O pré-teste da integração aprovou orçamento, volume, backing esparso, boot SMP,
ciclo de vida do gerenciador e migrações com falhas injetadas. Uma execução ampla
intermediária encontrou uma configuração de fixtures de driver incompleta e
mudança de imagem durante a integração; ela não aceita a revisão final.
Esses registros são preservados, sem somar seus resultados aos de uma matriz limpa.

A primeira matriz congelada aprovou 47 testes Debug, 47 Release e 30 com
ASan/UBSan. A revisão seguinte recusa perfis de hardware que o novo contrato
não implementa, incluindo configuração e importação inconsistentes. A matriz
completa, incluindo as correções de propriedade, memória e recuperação sob cota
cheia, foi aprovada em
`build-phase12b/acceptance-20261020-full-04`, com fontes congeladas, recompilação
de imagens e executáveis, Debug/Release com KVM e ASan/UBSan do núcleo.
O resultado efetivo deve ser lido em `result.json`, junto dos XMLs de cada perfil.
A revisão cruzada de sintaxe Windows com LLVM-MinGW passou; ela não executa WHP,
não usa MSVC e não constitui aceitação Windows.

Para encerrar o escopo completo, é necessário concluir a aceitação Windows da
mesma revisão e concluir a integração dos recursos novos nos perfis de aplicações
já existentes. Não interpretar o kernel de recursos como suporte SMP automático
para aplicações PE, drivers comerciais ou Windows geral.

## Resultado final desta execução Linux

Em `acceptance-20261020-full-04`, a mesma revisão de 368 arquivos de código e testes
passou sem falhas nem skips em:

| Perfil | Resultado |
| --- | --- |
| Debug, guest real KVM | 47/47 |
| Release, guest real KVM | 47/47 |
| Núcleo ASan/UBSan | 30/30 |

Os hashes foram conferidos antes e depois de cada etapa. Imagens e executáveis
foram reconstruídos nessa pasta, sem reaproveitar o resultado Windows histórico.
O arquivo [result.json](../build-phase12b/acceptance-20261020-full-04/result.json)
consolida os comandos, tempos e XMLs. As tentativas anteriores permanecem
separadas e não entram nesses totais.

A entrega inclui `GuardianAV-12B-fontes-20261020.zip` para continuar no Windows,
`GuardianAV-12B-linux-20261020.tar.gz` com executáveis ELF Release e imagens próprias,
e `GuardianAV-12B-20261020-SHA256SUMS.txt`. Os arquivos ficam em
`/home/gabri/Projetos` e em `build-phase12b/delivery-20261020` na pasta original.
O ZIP é verificado contra os 368 hashes das fontes aceitas. Ele foi produzido
antes da transferência e conserva o registro daquele momento; o presente
relatório e o registro de transferência foram atualizados também no HD.

A 12B **não está encerrada**: é necessário executar a aceitação MSVC/WHP no Windows
e concluir a integração do SMP e filesystem novo aos perfis PE anteriores.
Preservar o escopo completo decidido pelo usuário. O novo contrato de kernel
próprio não substitui essas pendências. Não iniciar 12C.
