# Fase 7 — Volume, arquivos e registro persistente

Base ativa: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Imagem: `guardian-kernel-storage.gk`, versão 0.7.0, x64, uma vCPU, WHP.
Estado: concluída em 16/10/2026. A fase 8 não foi iniciada.

## Implementação

O convidado possui agora um volume persistente com arquivos, diretórios,
links de arquivo, permissões, compartilhamento e registro virtual. A lógica
executa no kernel próprio. O host mantém uma base somente para leitura e um
overlay separado, recebendo apenas operações de bloco com offset e tamanho
limitados. Nomes de arquivos e chaves recebidos de CPL3 não chegam às APIs de
arquivos ou registro do Windows host.

`guest/Volume.cpp` implementa o volume freestanding. `StorageKernel.cpp`
liga sua ABI às identidades e espaços de memória da fase 5 e ao dispositivo
de blocos da fase 6. `PersistentBlockStore.cpp` implementa a persistência do
dispositivo no host. As imagens anteriores continuam usando seus contratos,
inclusive o disco volátil da imagem exclusiva de I/O.

### Base e overlays

A base tem exatamente 64 KiB e é aberta com acesso de leitura. O overlay é
um arquivo exclusivo, identificado por formato, versão e fingerprint da base.
Ele armazena um journal limitado de alterações de setores. Cada registro tem
sequência, número do setor, tamanho e checksum. Ao reabrir, os registros válidos
são aplicados sobre a base. Dois overlays da mesma base permanecem independentes.

Os caminhos dos dois arquivos são opções do controlador no host. Precisam ser
absolutos, distintos e apontar a arquivos comuns. Arquivos de reparse point,
diretórios e caminhos de dispositivos como `PhysicalDrive` são recusados.
O compartilhamento de handles impede reabrir a base para escrita como overlay,
inclusive quando dois nomes apontam ao mesmo arquivo. Um overlay existente
precisa ter formato e fingerprint válidos; não é truncado para reutilização.

`--create-disk` cria a base caso esteja ausente, sem substituir uma base
existente. Um overlay novo recebe seu próprio cabeçalho. Há limite de 4.096
registros; o esgotamento retorna erro, sem sobrescrever o histórico. Compactação
do journal ainda não é implementada. Os artefatos dos testes ficam no diretório
de build e são preservados.

### Flush e recuperação

O volume serializa arquivos, metadados, descritores de segurança, links e
registro em uma imagem de 24.064 bytes, dentro de dois slots separados no disco.
O espaço inicial de 4 KiB continua reservado ao diagnóstico de blocos da fase 6.
Os slots começam em `0x1000` e `0x8800`; nenhum pedido ultrapassa os 64 KiB.

Flush grava primeiro o corpo da nova imagem no slot inativo e chama o flush do
dispositivo. Depois grava seu cabeçalho de commit, com sequência e checksum,
e chama flush novamente. A versão ativa em memória só avança após sucesso.
O dispositivo persistente usa `FlushFileBuffers` sobre o overlay autorizado.
Arquivo e registro pertencem ao mesmo commit do volume.

Ao montar, o kernel valida os dois slots e escolhe a maior sequência válida.
Uma gravação incompleta no slot inativo não invalida o slot anterior. Foram
exercitadas 47 posições de falha antes da publicação do cabeçalho. O teste
também cobre falha no flush final: o resultado não é confirmado ao chamador,
embora uma nova imagem completa já possa estar visível na reabertura.

Uma cauda parcial do journal do overlay é removida somente depois de validar
seu cabeçalho e todos os registros completos anteriores. Um registro completo
com checksum inválido faz a abertura falhar; ele não é ignorado silenciosamente.
Dentro do convidado, dois snapshots inválidos e não vazios produzem diagnóstico;
não ocorre reformatação automática de dados corrompidos. Formatação inicial
ocorre apenas quando as duas áreas de imagem estão zeradas.

Checksums são verificações de integridade, não autenticação criptográfica.
Mesmo com checksum recalculado, metadados passam por validação de limites,
tipos, nomes canônicos, IDs e gerações, pais, profundidade, ciclos, raízes,
links, descritores e tipos de valores. A validade da imagem não autoriza
acesso a caminhos do host.

### Arquivos, diretórios e links

Há criação e abertura com disposições existentes/novo/abrir ou criar, leitura,
escrita com offset, EOF, truncamento, enumeração, renomeação e exclusão.
Expandir um arquivo por truncamento ou escrever após EOF preserva zeros no
intervalo. Limites e permissões são verificados antes de alterar bytes. Uma
escrita de zero bytes não expande o arquivo.

Handles pertencem ao processo e guardam direitos concedidos e máscaras de
compartilhamento de leitura, escrita e exclusão. Novas aberturas verificam a
compatibilidade nos dois sentidos. Um handle copiado de outro processo não
passa a ser válido. Alterar uma DACL restringe novas aberturas sem revogar
direitos já concedidos a handles existentes.

Renomeação valida o destino, colisões, compartilhamento e profundidade antes
de mudar a árvore; mover um diretório para dentro de si é recusado. Diretório
não vazio não é excluído. A exclusão de um arquivo remove seu nome, mas handles
e links existentes podem manter os dados vivos até a última referência.
Na montagem, referências voláteis de processos anteriores não são restauradas.

Links são hard links de arquivos dentro do volume, com destino por ID e geração.
Compartilham conteúdo, ACL e controle de abertura do arquivo. A exclusão do nome
original não destrói um arquivo ainda alcançável por outro link. Links de
diretórios, symlinks e reparse points não são implementados; são recusados.

O namespace de arquivos aceita somente caminhos absolutos `C:\...`, internos
ao convidado. Componentes são ASCII, com comparação sem distinção entre A–Z,
espaços internos e limite de tamanho. `..`, componentes vazios, trailing
dot/space, caminhos relativos, ADS, UNC, outros drives e namespaces de
dispositivos são recusados. Não há normalização que permita sair do volume.

### Registro, permissões e notificações

O registro possui `HKLM` e `HKCU` separado por identidade numérica do convidado.
Criar uma árvore de chaves é transacional: falha de permissão ou de capacidade
desfaz os componentes já criados. Chaves e valores têm nomes independentes,
inclusive quando compartilham o mesmo texto. O nome vazio representa o valor
padrão.

Há criação, consulta, definição e exclusão de valores, enumeração de valores
e subchaves e exclusão de chaves sem subchaves. Excluir uma chave remove seus
valores. `None`, `Binary`, `DWORD`, `QWORD`, `String`, `ExpandString` e
`MultiString` têm validação de tamanho e terminação; números usam little endian,
e strings armazenam bytes UTF-16LE. Expansão de variáveis e paridade Unicode
completa não são declaradas nesta ABI.

Notificações são assinaturas voláteis por processo, com filtro para nomes,
valores ou segurança e opção de subárvore. Um evento de conclusão do escalonador
permite esperar sem ocupar a CPU. A consulta retorna as mudanças e encerra a
assinatura; o chamador registra outra se quiser continuar. Cancelamento ou saída
do processo libera o evento e os slots. Assinaturas não são persistidas.

Arquivos, diretórios e chaves usam os tokens, grupos, DACLs canônicas e política
de integridade da fase 5. Travessia/enumeração, leitura, escrita, exclusão,
criação de filhos e leitura/alteração da DACL têm direitos próprios. Alterar
a DACL não altera proprietário, grupo ou integridade por esse caminho. O
processo recebe seu token do kernel; a chamada CPL3 não fornece uma identidade.

### ABI e integração

`shared/guardian/Storage.h` define a imagem, comandos e relatório versionados.
A syscall interna `0x80` recebe um comando de 880 bytes. O kernel valida todo
o intervalo de entrada e saída no espaço do processo antes de executar a
operação. Um destino supervisor ou não gravável é recusado antes de qualquer
alteração de arquivos ou registro. Flags, tipos, versões e campos reservados
usados pelo contrato são validados.

Operações do volume usam o driver de blocos da fase 6, com transferências
limitadas a 512 bytes. Mount e flush são operações síncronas serializadas em
CPL0; a fila de I/O de usuário da fase 6 continua assíncrona. A enumeração do
barramento anuncia a capacidade persistente para esta imagem. O relatório da
fase usa a porta `0xF1` e GPA fixo `0x10F000`, após os relatórios de processos
e I/O e antes do relatório de CPU.

## Aceitação e regressões

O mesmo conjunto de 784 verificações de volume executa nativamente e em CPL0
a cada boot de armazenamento. Inclui compartilhamento, ACLs, integridade,
direitos já concedidos, tombstones, hard links, truncamento, caminhos inválidos,
registro, tipos, enumeração, identidade HKCU, notificações com espera real,
quotas, rollback, snapshots interrompidos e metadados malformados. Há 128 ciclos
de criação/exclusão/saída de processo, sem handles ou notificações restantes.

As 32 verificações específicas do backend do host cobrem isolamento de overlays,
base inalterada, exclusividade, fingerprint, recuperação de cauda, corrupção,
limites e persistência real de arquivo/registro depois de reabrir o arquivo
de disco, incluindo flush interrompido.

A matriz WHP executa 20 pares de reinicialização por configuração: dez com
16 MiB e dez com 64 MiB. O primeiro boot grava um contador em arquivo e no
registro; o segundo precisa recuperar ambos com valores correspondentes antes
de alterá-los novamente. Cada par usa um overlay novo da mesma base; os hashes
da base permanecem iguais. O programa CPL3 também cria/escreve/lê um arquivo,
altera/consulta HKCU, enumera o diretório, força uma recusa de ponteiro e faz
flush. Os cenários anteriores de I/O e processos continuam na mesma imagem.

Há 18 casos externos negativos por configuração, incluindo relatório inválido,
ordem/replay/IN/OUT largo, escrita na enumeração protegida, dispositivo removido,
volume corrompido, barramento inválido e ausência de timer limitada pelo watchdog.
Um volume corrompido mantém os registros originais, sem reformatação.
Critérios: `config/phase7-acceptance.json`.

## Resultado verificado

| Verificação | Resultado |
|---|---|
| Build completo Debug / Release | aprovado / aprovado |
| CTest por configuração | 31 aprovados, zero falhas e zero ignorados |
| Núcleo do volume | 784 verificações nativas e em cada boot |
| Backend persistente do host | 32 verificações por configuração |
| Boots positivos de armazenamento | 80, em 40 pares de reinicialização |
| Boots positivos de regressão das fases 1–6 | 480 |
| Casos externos negativos | 240 (36 de armazenamento e 204 de regressão) |
| Exemplos confiáveis | sete por configuração, 14 execuções aprovadas |
| Build Debug limpo independente | aprovado, sete imagens auditadas |
| Artefatos por configuração | 60 executáveis e nove imagens |
| Fronteiras freestanding verificadas | 32 combinações de alvo/configuração |
| Evidências históricas preservadas | 288 arquivos com SHA-256 inalterado |

Os tempos dos boots de armazenamento da matriz foram 304,628–558,013 ms em
Debug e 324,984–717,217 ms em Release nesta máquina; não são garantia de prazo.
A imagem ocupa 815.104 bytes de espaço virtual em Debug e 770.048 em Release,
dentro do limite de 1 MiB do carregador. As sete imagens de produção de cada
configuração passaram as auditorias de imports/TLS/relocations/W+X. Os dois
boots adicionais da CLI por configuração também recuperaram o volume; eles
não estão somados aos 560 boots da matriz.

## Compilar e executar

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-storage.gk
New-Item -ItemType Directory -Path .\build-codex\meu-volume -Force
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-storage.gk --storage --disk .\build-codex\meu-volume\base.gvd --overlay .\build-codex\meu-volume\sessao.gvo --create-disk --ram-mib 64 --repeat 2 --report .\build-codex\meu-volume\boots.json
```

`--storage` exige base e overlay explícitos. Reutilizar o overlay recupera seus
commits; escolher outro overlay da mesma base cria um ambiente independente.
Esses arquivos são o formato próprio deste protótipo, não ISO/VHDX/NTFS.
WHP é usado diretamente, sem QEMU ou mudanças nas configurações do Windows.
Não foram executadas amostras de malware.

## Evidências e recuperação

Os resultados novos usam exclusivamente o prefixo `phase7-`, inclusive as
regressões das fases 1–6. Os 288 arquivos históricos têm seus hashes preservados.
O backup dos quinze arquivos anteriores alterados está em
`snapshots/codex-before-phase7-20261016-210839/`, junto do inventário histórico.
Há 14 arquivos novos de implementação, testes, configuração e documentação.

Os logs `phase7-full-*-script.log` e `phase7-*-tests-details.log` registram build
e CTest. `phase7-boots-*` e `phase7-regression-phase1-*` até
`phase7-regression-phase6-*` guardam matrizes e negativos. Os diretórios
`phase7-*-volumes-*` e `phase7-boots-*-volumes/` guardam os discos dos testes.
`phase7-reference-programs.json`, `phase7-smoke.json` e `phase7-cli-*` registram
exemplos e CLI. Auditorias e build limpo estão em `phase7-image-audit.json`,
`phase7-build-boundaries.json`, logs `phase7-clean-*` e
`build-codex-phase7-clean/`.

`phase7-change-manifest.json` inclui hashes de fontes, backups, executáveis,
imagens, bases, overlays e evidências. Revalidar sem executar programas:
`python tools/validate_phase7.py --snapshot snapshots/codex-before-phase7-20261016-210839`.
As tentativas intermediárias foram preservadas. O uso excessivo de pilha em
Debug foi corrigido evitando grandes imagens temporárias e múltiplas cópias
temporárias de comandos. A guard page e o stack probe real permanecem ativos.
LNK4281 é o aviso esperado da base fixa do kernel em 2 MiB.

## Limites e parada

| Recurso | Limite desta etapa |
|---|---:|
| Disco virtual | 64 KiB |
| Nós, incluindo raízes, arquivos, chaves e valores | 32 |
| Conteúdo de um arquivo/valor | 512 bytes |
| Handles globais / por processo | 32 / 8 |
| Assinaturas de registro | 8 |
| Caminho / componente, incluindo NUL | 128 / 32 bytes ASCII |
| Profundidade | 8 componentes |
| Registros por overlay | 4.096 |
| Imagem por snapshot / slots | 24.064 bytes / 2 |

O formato é próprio e limitado. Não implementa NTFS/FAT, hives Windows, ACLs NT
completas, streams, links de diretório, montagem de volumes externos, memória
mapeada de arquivos, expansão automática do disco ou compactação do overlay.
Flush persiste o volume inteiro; não há um cache de páginas de arquivos.
Os limites tornam a superfície verificável antes da ampliação de capacidade
e compatibilidade. O produto e o kernel Windows completo continuam em
desenvolvimento, sem promessa de invisibilidade do sandbox.

Ao concluir a fase 7, parar. A fase 8 só começa após nova solicitação.

## Referências de semântica

- [Microsoft: FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
- [Microsoft: flush de dados em buffer](https://learn.microsoft.com/en-us/windows/win32/fileio/flushing-system-buffered-i-o-data-to-disk)
- [Microsoft: tipos de valores de registro](https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types)
- [Microsoft: notificações de registro](https://learn.microsoft.com/pt-br/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue)

Essas referências orientam a semântica. Os limites e diferenças da ABI própria
estão descritos acima; não se declara paridade geral com APIs Windows.
