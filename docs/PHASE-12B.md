> Continuação de 20/10/2026: o novo contrato próprio `resources` implementa SMP,
> volume escalável e pasta independente. Os limites antigos descritos abaixo
> continuam referentes aos perfis anteriores. A 12B permanece aberta; consulte
> [implementação, limites e validação atual](PHASE-12B-RECURSOS.md).

# Subfase 12B — recursos e cotas

Estado: implementação em validação. A subfase não está concluída para todo o
escopo do roadmap. SMP e volumes grandes continuam pendentes; a 12C não começou.

Esta entrega amplia a RAM da matriz própria de 16–64 MiB para 16–1024 MiB,
condicionada à admissão real do host. O modo máximo escolhe a maior quantidade
admissível dentro desse limite. O padrão reserva pelo menos 512 MiB ou 25% da
memória do sistema. `--host-reserve-mib` permite uma reserva explícita de pelo
menos 512 MiB; zero seleciona o padrão automático. A admissão também contabiliza
oito vezes a RAM de cada VM e 128 MiB de margem por worker.
Essa conta conservadora cobre RAM viva, captura, codificação e restauração.
Uma reserva por processo, comum aos catálogos do mesmo usuário, limita o conjunto
a quatro workers e preserva uma CPU lógica nos hosts com mais de uma CPU.
Quando catálogos escolhem reservas diferentes, prevalece a maior reserva
solicitada entre as sessões ativas. Uma sessão não reduz a reserva de outra.
Não há afinidade exclusiva nem promessa de latência para o sistema operacional.

O kernel continua com uma vCPU real. Solicitar mais de uma produz diagnóstico
de SMP sem suporte. A interface apresenta solicitado, admitido e provisionado;
uma VM parada não anuncia RAM ou vCPU provisionadas. Aumentar somente CPUID ou
o inventário não implementaria o funcionamento de núcleos adicionais.

O disco de blocos mantém 64 KiB e o volume de referência mantém 32 unidades de
alocação de 512 bytes, com três unidades iniciais para as raízes. Cada arquivo,
diretório, chave ou valor ocupa uma unidade; arquivos e valores continuam
limitados a 512 bytes. A cota do volume pode variar de 1536 a 16384 bytes,
em unidades de 512. Criar uma nova entrada sem espaço retorna `STATUS_DISK_FULL`.
Excluir um arquivo aberto só libera sua unidade quando o último handle fecha.
Sobrescrever dados de uma unidade já alocada continua permitido.

`QuerySpace` e `GetDiskFreeSpaceExA/W` consultam a mesma contagem usada pelas
alocações. As APIs de referência aceitam a raiz `C:\` ou a unidade corrente;
caminhos e unidades adicionais continuam fora da matriz. Capacidade do volume,
espaço livre e disponível à cota são valores separados. O disponível também
chega a zero quando o backing não comporta uma nova transação completa.

O journal recebe um limite de crescimento próprio, respeitando a unidade de
alocação do host, o teto finito de 4096 registros e uma reserva de 256 MiB na
unidade do host. Falta de cota atravessa o transporte de I/O com status específico.
O limite agregado de armazenamento da VM inclui base, journal, cópias ativas,
imagens, drivers, auditorias, snapshots e arquivos temporários de substituição.
O padrão é 4 GiB; a configuração aceita 1 MiB a 64 GiB. A cobrança considera o
maior entre os comprimentos lógicos somados e a alocação física somada. A
interface exibe ambos. Uma margem de 64 KiB permite mensagens de controle e
encerramento quando a cota de dados se esgota.

Salvar um arquivo exige espaço para o anterior e para o temporário. Rejeitar
uma operação por cota preserva o estado anterior. Excluir snapshots recupera
armazenamento agregado; elevar uma cota de backing, com a VM parada, permite
retomar gravações. Reduzir um limite abaixo do uso atual é rejeitado.

`--root` escolhe a pasta do catálogo e de seus discos. Catálogos diferentes
podem ficar em unidades diferentes e compartilham a reserva de RAM/CPU do usuário.
Uma pasta de disco independente da pasta de estado de cada VM ainda está pendente.
O modelo de disco do perfil da 12A continua sendo apenas referência: o dispositivo
não expõe nem acessa o disco físico ou os arquivos pessoais do host.

Exemplos da interface de linha de comando:

```text
guardian-vm --root D:\Laboratorio create teste caminho\guardian-kernel-storage.gk storage --ram-mib 128 --vcpus 1 --volume-quota-kib 8 --overlay-limit-kib 2048 --storage-limit-mib 512 --host-reserve-mib 1024
guardian-vm --root D:\Laboratorio resources teste
guardian-vm --root D:\Laboratorio resource-config teste --maximum --host-reserve-mib 1024
guardian-vm --root D:\Laboratorio storage-config teste --volume-quota-kib 8 --overlay-limit-kib 2048 --storage-limit-mib 1024
```

Configurações antigas continuam legíveis. Kernels importados anteriormente
mantêm o comportamento original; a declaração de capacidade no kernel impede
selecionar uma cota de volume personalizada em uma imagem sem a implementação.
Checkpoints antigos desse contrato sem cota mantêm sua compatibilidade original.
Em imagens novas, o limite de volume e a proteção da página de configuração
ficam vinculados ao checkpoint; restaurar com outro limite é rejeitado.

## Validação

Resultados finais e hashes serão registrados após a aceitação nativa. O snapshot
anterior à edição está indicado em `build-phase12b/baseline.txt`. Falhas de
compilação e resultados preliminares são preservados nessa pasta.

Os testes de gerenciamento escolhem explicitamente 1 GiB de reserva operacional,
mantendo o multiplicador de oito e a margem por worker. A reserva automática
de 25% recusou sessões durante a pressão de memória observada neste Windows;
essa recusa também está preservada nos resultados preliminares. Os testes
determinísticos cobrem as duas políticas, o mínimo de 512 MiB e a preservação
da maior reserva ativa. O kernel básico verifica acesso real à última página
de RAM disponível, além das verificações anteriores de layout e inicialização.

## Trabalho restante da 12B

- SMP real no VMM e no kernel, com topologia, inicialização e sincronização
  coerentes; depois admitir múltiplas vCPUs nas aplicações suportadas.
- Volume escalável, arquivos maiores e crescimento configurável em GiB;
  a cota física de armazenamento não aumenta a capacidade do filesystem atual.
- Pasta de disco independente por VM, incluindo migração, recuperação e clone.
- Referência opcional de capacidade/fabricante do disco do host, sem fazer o
  volume anunciar espaço que ainda não consegue utilizar.
- Ampliar a matriz de RAM e estresse conforme essas implementações avancem.

A parada desta entrega deve manter a 12B aberta, sem avançar à 12C ou declarar
que os recursos pendentes já funcionam.
