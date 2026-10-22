# Handles e objetos do MiniNt — 16/10/2026

Esta etapa continua a base em `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
O núcleo continua atendendo syscalls NT no host e executando o convidado pelo
WHP existente, sem QEMU. O foco foi tornar o estado dos objetos e seus direitos
verificáveis antes de ampliar a compatibilidade.

**Etapa seguinte:** [KERNEL-THREADS.md](KERNEL-THREADS.md) acrescenta threads,
escalonamento cooperativo e esperas bloqueantes em eventos/threads. Os resultados
de seis testes abaixo registram esta etapa de objetos; o script atual executa
oito testes. As limitações de espera simples foram ampliadas conforme a nova
documentação, enquanto APCs, arquivos e espera múltipla continuam pendentes.

## Problemas corrigidos

Arquivos, seções, eventos e chaves usavam contadores separados. Seus intervalos
podiam se sobrepor, de modo que um mesmo número identificasse tipos diferentes.
Além disso, seções guardavam somente o número do handle do arquivo: fechar o
arquivo antes de mapear a seção podia fazer o mapeamento perder sua origem.

`GuestObjects.h` agora fornece uma tabela única por `MiniNt`, com tipo, direitos
e atributos próprios de cada handle. Os objetos usam referências compartilhadas;
fechar um handle retira aquela entrada, e o objeto continua vivo enquanto houver
outra referência. Valores de handles do convidado nunca são usados como handles
do Windows hospedeiro.

## Comportamento atual

| Recurso | Implementação |
|---|---|
| Tipos | Arquivo, seção, evento e chave de registro em uma tabela comum. |
| Identificadores | Múltiplos de quatro, sem colisões entre tipos. Fechamentos não reutilizam números nesta instância, evitando reviver handles antigos. |
| Direitos | Direitos genéricos são convertidos para máscaras específicas do tipo; operações verificam os direitos necessários. Não há concessão silenciosa de `MAXIMUM_ALLOWED`. |
| Duplicação | `NtDuplicateObject` compartilha o mesmo objeto, preserva ou reduz direitos e suporta `DUPLICATE_SAME_ACCESS` e `DUPLICATE_SAME_ATTRIBUTES`. |
| Fechamento | `NtClose` recusa handles inválidos, fechamento duplicado e handles protegidos contra fechamento. |
| Informações | `NtQueryObject` atende informações básicas de 56 bytes e flags do handle de 2 bytes; `NtSetInformationObject` altera herança e proteção contra fechamento. |
| Limites | Até 4.096 handles abertos por instância; esgotamento retorna erro sem publicar um novo handle. |

A duplicação é restrita ao processo corrente, com `HANDLE(-1)` nos dois
argumentos de processo. Elevar os direitos do handle de origem é recusado.
`DUPLICATE_CLOSE_SOURCE` é recusado nesta etapa, sem efeitos no handle de origem;
sua semântica especial em caminhos de erro ainda precisa ser implementada.

As verificações de tipo distinguem handle inexistente de handle de outro tipo.
As flags pertencem à entrada do handle: proteger um handle contra fechamento não
altera seus aliases. Herança é armazenada e consultável; criar processos filhos
e herdar a tabela ainda não foi implementado.

## Arquivos, seções e views

`MiniNtObjects.cpp` atende os syscalls de objetos. Arquivos do convidado
continuam dentro do `VirtualFs` em memória. O objeto de arquivo possui um handle
interno do `VirtualFs`, fechado automaticamente ao desaparecer sua última
referência. Duplicados compartilham a posição do arquivo; aberturas independentes
têm posições independentes.

`NtReadFile` e `NtWriteFile` verificam direitos, tipo, buffers e o
`IO_STATUS_BLOCK`. A leitura informa EOF; uma escrita recusada não altera os
dados nem avança a posição. As operações implementadas são síncronas e usam a
posição corrente: offsets explícitos, APCs, eventos de conclusão e operações
assíncronas são recusados. Há limite de 16 MiB por chamada e de 64 MiB para o
crescimento por escrita de um arquivo neste caminho.

A seção mantém uma referência ao objeto do arquivo; cada view mantém uma
referência à seção. Portanto, é possível fechar todos os handles do arquivo,
mapear a seção, fechar todos os handles da seção e continuar lendo a view.
Desmapear a última view libera a última referência, quando não há outros
usuários. O teste verifica também o fechamento efetivo do handle interno do
`VirtualFs`, usando `OpenHandleCount()`.

Mapeamentos de dados verificam os direitos do handle de seção. A proteção
máxima da view considera tanto a seção quanto os direitos usados para mapeá-la;
um handle limitado à leitura não pode ganhar escrita por uma chamada posterior
a `NtProtectVirtualMemory`.

O modelo de dados continua copiando os bytes para cada view. Coerência entre
views, persistência no arquivo e copy-on-write ainda estão pendentes. Imagens
`SEC_IMAGE` mantêm a política anterior de imagem inteira RWX, sem permissões
individuais por seção PE; o parser PE antigo também continua fora de uma
auditoria integral.

## Eventos e registro

Aliases de um evento compartilham seu estado. Eventos de sincronização
(`auto-reset`) consomem o sinal em uma espera satisfeita; eventos de notificação
(`manual-reset`) permanecem sinalizados até o reset. Sinalizar e resetar exigem
`EVENT_MODIFY_STATE`; esperar exige `SYNCHRONIZE`. A saída de estado anterior
usa exatamente quatro bytes.
`NtClearEvent` segue seu ABI de um argumento; o registrador do segundo argumento
não é interpretado como ponteiro.

Uma consulta com timeout zero sobre evento não sinalizado retorna
`STATUS_TIMEOUT`. Esperas que exigiriam bloquear a thread retornam
`STATUS_NOT_SUPPORTED`; não é mais fabricado sucesso. Entrega de APCs e o
escalonador continuam pendentes.
Esperar em arquivos ainda é uma operação não suportada neste modelo.

Chaves também usam a tabela única. Consultar valores exige `KEY_QUERY_VALUE`,
e alterá-los exige `KEY_SET_VALUE`. Handles duplicados preservam a identidade
da chave. A leitura de nomes e os buffers desse caminho agora são verificados
contra a RAM e as permissões do convidado. O namespace virtual anterior ainda
é permissivo ao abrir chaves sem valores; não representa toda a semântica do
registro do Windows.
`NtCreateKey` distingue uma nova chave de uma chave existente na saída
`Disposition` e exige `KEY_CREATE_SUB_KEY` para criar relativamente a um handle
de chave pai.

## Testes e compilação

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

O script continua usando `build-codex` e agora executa seis testes. Os quatro
testes de heap/memória virtual continuam incluídos. Os dois novos são:

- `microvm_objects`: direitos, duplicação, flags por handle, falhas por limite,
  fechamento e destruição na última referência; cria 1.000 handles de tipos
  alternados para verificar colisões e a invalidação de handles antigos.
- `microvm_objects_guest`: programas gerados pelo próprio teste executam em
  ring 3 pelo dispatcher real de `MiniNt`. Verificam aliases, direitos, posição
  de arquivo, eventos, proteção contra fechamento, vida útil de seções/views,
  limites de proteção, registro e canários dos campos de saída.

Os testes de convidado compartilham `MicroVmTestGuest.h`, extraído do teste de
memória virtual existente. Usam watchdog, RAM virtual e pequenos programas
gerados localmente. Retornam 77 se o WHP não estiver disponível, situação que o
CTest registra como ignorada. Não usam amostras de malware.

## Validação executada

Windows x64 com WHP disponível, MSVC 19.44 e Windows SDK 10.0.26100.0:

| Verificação | Debug | Release |
|---|---|---|
| Seis testes do CTest | 6 passaram, nenhum ignorado | 6 passaram, nenhum ignorado |
| `vmntdll` | Passou | Passou |
| `vmntfile` | Passou | Passou |
| `vmntsect` | Passou | Passou |
| `vmntimage`, incluindo execução e relocação da DLL de teste | Passou | Passou |
| `vmntquery`, incluindo evento sinalizado | Passou | Passou |
| `vmntreg` | Passou | Passou |
| `vmwininit`, com o heap próprio | Passou | Passou |
| Compilação de todos os alvos do projeto | Não executada | Passou, 37 executáveis |

Os testes de convidado realmente executaram no WHP. A compilação dos alvos do
script em Debug terminou sem avisos. O build completo Release mantém avisos
de conversão em código anterior do modelo A e de outros módulos.

Evidências em `build-codex`:

- `objects-release-tests.log`: os seis testes em Release.
- `objects-debug-script.log`: compilação e seis testes em Debug.
- `objects-regressions.json`: saída completa dos sete exemplos em cada configuração.
- `objects-full-release-build.log`: compilação completa Release.
- `objects-change-manifest.json`: arquivos alterados, hashes e localização dos backups.

Os nove arquivos existentes modificados nesta etapa foram preservados antes das
alterações em `snapshots/codex-before-objects-20261016-000830/`. Os builds e
snapshots anteriores permanecem disponíveis.

## Limites e continuação

Os direitos desta etapa são controles de cada handle dentro do modelo. Tokens,
ACLs, impersonação, privilégios, objetos nomeados de evento/seção e tabelas de
outros processos ainda não foram implementados. Compartilhamento de arquivos
entre aberturas (`ShareAccess` diferente de zero) é recusado neste caminho.
Aberturas de diretórios, atributos adicionais e parte das opções nativas também
continuam fora do subconjunto suportado.

`NtKernel` (modelo A) permanece separado. A integração de snapshots ainda deve
incluir metadados de objetos, posição de arquivos, regiões e permissões WHP;
restaurar apenas RAM e registradores não restaura o estado do `MiniNt`.
Esta etapa não fornece um kernel inicializável independente nem compatibilidade
integral com Windows ou isolamento de malware validado.

O próximo bloco é o escalonamento de threads e esperas, substituindo respostas
provisórias por transições de estado e eventos de conclusão reais.

## Referências de ABI e semântica

- [ZwClose e referências pendentes](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwclose)
- [ZwDuplicateObject](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwduplicateobject)
- [NtQueryObject](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject)
- [ZwCreateSection](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatesection)
- [ZwCreateKey](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatekey)
- [Declarações de flags e estruturas do Object Manager no phnt](https://github.com/winsiderss/phnt/blob/master/ntobapi.h)
- [Declarações de eventos no phnt](https://github.com/winsiderss/phnt/blob/master/ntexapi.h)
