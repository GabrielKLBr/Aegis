# Fase 12C — fidelidade do comportamento do sistema

**Atualização Windows de 23/10/2026:** a matriz atual passou em MSVC/WHP: 80 testes distintos Debug, com retestes documentados, e 80/80 Release em uma suíte completa, sem ignorados. [Correções e evidências](REVISAO-WINDOWS-FASES12B-13-20261023.md). Os registros Linux abaixo são históricos; as pendências de escopo e de comparação entre versões continuam abertas.

**Estado em 21/10/2026:** implementação e validação Linux aprovadas na revisão
`acceptance-20261021-full-01`: 50/50 Debug, 50/50 Release e 32/32 ASan/UBSan,
com 382 arquivos de código estáveis. O encerramento formal da 12C continua
pendente das referências Windows nativas e da revisão das dependências da 12B.
O projeto permanece Windows/MSVC/WHP, com desenvolvimento e testes nativos
Linux/KVM. A 12D foi autorizada posteriormente e está registrada em [PHASE-12D.md](PHASE-12D.md).

As alterações são gravadas diretamente na pasta original do HD. O estado
anterior foi preservado em `build-phase12c/baseline-20261021/original-source.zip`,
com hashes e manifesto. As compilações Linux ficam fora do NTFS para preservar
os builds Windows existentes; as evidências finais serão copiadas para o HD.

## Matriz comum e referências

`shared/guardian/ConformanceProgram.h` contém o mesmo programa de 53 casos para
tempo, exceções, sincronização, memória, handles, I/O, resultados de APIs e
coerência de arquitetura/identidade. O convidado usa a DLL runtime própria,
passa por CPL3, syscalls, escalonador e dispositivos. O executável de referência
usa as APIs reais do Windows, com seus próprios arquivos temporários, memória, threads e mutexes. Não usa programas de terceiros nem arquivos pessoais.

Cada caso tem identidade estável, domínio e resultado observado. Os valores
esperados descrevem o contrato; não são apresentados como medições do Windows.
O programa e suas entradas recebem um hash incorporado aos executáveis e às
matrizes. Observações de outra revisão, arquitetura, versão NT, casos incompletos
ou repetidos são recusadas. A captura de referência exige Windows x64 nativo;
Linux e Wine são recusados nesse caminho.

A matriz convidada cobre os perfis Win7 (`6.1.7601`), Win10 (`10.0.19045`) e
Win11 (`10.0.22631`), com 16 e 64 MiB, repetições e restauração de snapshot.
Uma versão Windows diferente é identificada pelo número real retornado por
`RtlGetVersion`; não é renomeada para cobrir outra versão. As versões sem
referência autorizada continuam pendentes, mesmo com todos os testes Linux
aprovados. Perfis x86 e NT5/NT6.0/NT6.2/NT6.3 não entram nesta matriz x64.

## Comportamentos implementados e corrigidos

- Relógios: `QueryPerformanceCounter/Frequency`, `GetTickCount/64`,
  `QueryUnbiasedInterruptTime` e `GetSystemTimeAsFileTime` derivam de uma
  publicação comum. A origem UTC vem do relógio do host no preparo da VM e
  é preservada no snapshot; os intervalos usam o relógio monotônico. Cada
  consulta atualiza a publicação por uma saída controlada ao VMM, evitando
  limitar o contador à última interrupção periódica. A frequência virtual é
  10 MHz. Nenhum relógio do host é alterado.
- `Sleep` finito bloqueia o thread até o prazo, com o mesmo relógio das consultas;
  `Sleep(0)` cede execução. Um pedido não suportado não retorna como uma espera
  concluída: gera uma exceção explícita. `Sleep(INFINITE)` continua fora do
  subconjunto suportado.
- Mutex sem nome aceita posse inicial, contabiliza recursão e libera somente
  pelo proprietário. Os casos cobrem posse, timeout zero, recursão, abandono,
  término de thread, handles inválidos e isolamento de `LastError` por TEB.
- Memória privada: reserva e compromisso combinados em `VirtualAlloc(NULL, n,
  MEM_RESERVE|MEM_COMMIT, ...)`, consulta, proteção e liberação real, com zero
  inicial, granularidade de 64 KiB, páginas de 4 KiB e cotas do processo.
  Um registro de propriedade impede liberar ou proteger mappings do loader,
  TEBs e stacks por essa API. Falha de compromisso desfaz a reserva. Há no
  máximo oito alocações simultâneas por processo nesse caminho.
- I/O: criação, leitura, escrita, EOF, seek, tamanho, compartilhamento, acesso
  negado, truncamento e resultados/contagens; consultar ou fechar novamente um
  handle inválido produz erro. Os arquivos do programa ficam no volume próprio do convidado.
- Exceções: instrução inválida recuperada por contexto, divisão por zero com
  execução de `finally` e exceção levantada com argumentos. O teste passa pelo
  dispatcher e unwind do convidado; a referência usa SEH nativo.

O kernel Debug completo cresceu além do limite anterior de imagem de 1 MiB.
O limite passou a 1,5 MiB, ainda abaixo da stack de bootstrap em 4 MiB, com
verificação explícita dessa separação e regressão para rejeitar imagens maiores.
As validações de PE, limites de seção, W^X e reserva física permanecem ativas.
O programa Debug também ultrapassou 128 entradas de função em `.pdata`. Loader e
unwinder compartilham agora um limite de 256, mantendo a validação integral de
metadados e um teto comum; não há truncamento silencioso da tabela.

## Diferenças e limites declarados

O tempo da VM fica congelado enquanto um snapshot está pausado ou armazenado.
A retomada preserva o tempo decorrido e a origem UTC, sem incluir o intervalo
offline. Essa é uma diferença intencional em relação à suspensão/hibernação do
Windows real. A matriz verifica o contrato da VM; não a apresenta como teste de
hibernação Windows. A quantização do timer Windows é considerada ao medir
`Sleep`; não é imposta duração máxima sob carga de agendamento.

O runtime é um subconjunto explícito. Nomes e descritores de segurança de mutex,
`Sleep(INFINITE)`, alocação em endereço solicitado, reserva/compromisso separados,
decommit, proteção guard/execução, JIT e uma página `KUSER_SHARED_DATA` em endereço Windows não são
anunciados completos. Os perfis PE continuam com uma vCPU enquanto a integração
SMP da 12B permanecer pendente. O contrato `resources` conserva sua matriz SMP
própria; ela não prova um escalonador PE multiprocessador.

As antigas demos `MiniNt` não passam a ser conformes por esta entrega. A matriz
alcança o kernel próprio e o runtime declarado acima. Interfaces provisórias de
outras demos e compatibilidade geral com Windows não são classificadas como
suporte completo.

## Validação e continuação no Windows

O resultado consolidado está em
[`result.json`](../build-phase12c/acceptance-20261021-full-01/result.json), com
comandos, tempos, hashes de entrada, arquivos JUnit e comparações. A execução foi
reconstruída em pasta nova, sem alteração dos 382 arquivos de código durante
compilação e testes. Não houve falhas, skips, Wine ou QEMU.

| Perfil nativo Linux | CTests | Matriz 12C |
| --- | --- | --- |
| Debug/KVM | 50/50 | 15 boots × 53 casos = 795 observações |
| Release/KVM | 50/50 | 15 boots × 53 casos = 795 observações |
| Núcleo ASan/UBSan | 32/32 | 8.322 verificações do novo núcleo; sem boot KVM |

São **132 execuções CTest aprovadas** e **1.590 observações da matriz convidada**.
Os três perfis NT passam com 16 e 64 MiB, duas repetições por tamanho e uma
restauração por perfil/configuração. Há ainda 12 rejeições de relógio de snapshot
inválido por configuração: frequência errada, origem UTC inválida, sequência
ímpar e apagamento dos dois campos novos. O teste de núcleo verifica rollback
de memória, cotas, propriedade dos mappings e amostragem do relógio.

As matrizes completas de observações estão em
[`Debug`](../build-phase12c/acceptance-20261021-full-01/host-Debug/conformance-Debug/matrix.json)
e [`Release`](../build-phase12c/acceptance-20261021-full-01/host-Release/conformance-Release/matrix.json).
As comparações registram explicitamente `referenceCompared: false` e as versões
Windows ainda ausentes: `6.1.7601`, `10.0.19045` e `10.0.22631`. Uma lista de
divergências vazia nesse estado não significa comparação Windows aprovada.

O hash do programa comum é:

```text
258fb7ba10d3dfadaf3d7f3c4a9707e328313e8091b0cc9b56a053a25758d1bd
```

A revisão cruzada de sintaxe dos oito arquivos host/referência Windows passou
com LLVM-MinGW, sem executar binários Windows. O registro identifica os macros
usados apenas para revisar metadados de build. Ela não constitui compilação
MSVC nem aceitação WHP. O histórico de desenvolvimento permanece separado da
aceitação final e não entra nesses totais.

Em terminal x64 do Visual Studio 2022, na pasta original do projeto, usando uma
pasta de saída nova:

```powershell
python tools\validate_phase12c.py --output build-phase12c\windows-full-20261021 --generator "Visual Studio 17 2022"
```

Esse fluxo compila com MSVC, executa os convidados em WHP e captura a referência
nativa da versão Windows efetivamente usada. Para comparar referências coletadas
em outras instalações autorizadas, acrescentar `--reference caminho\reference.json`
uma vez por versão exata. O hash do programa precisa coincidir.

## Entrega na pasta original

Os fontes e estes Markdown estão na pasta original do HD. O backup anterior
permanece em `build-phase12c/baseline-20261021`. As evidências finais selecionadas
foram copiadas para `build-phase12c/acceptance-20261021-full-01`, preservando os
builds e registros das fases anteriores. A compilação Linux completa continua em
`/home/gabri/Projetos/GuardianAV-12C-build-20261021`.

Em `build-phase12c/delivery-20261021`:

- [`GuardianAV-12C-fontes-20261021.zip`](../build-phase12c/delivery-20261021/GuardianAV-12C-fontes-20261021.zip):
  código, documentação e evidências selecionadas para continuar no Windows.
- [`GuardianAV-12C-linux-20261021.tar.gz`](../build-phase12c/delivery-20261021/GuardianAV-12C-linux-20261021.tar.gz):
  seis executáveis ELF Release e imagens próprias do convidado. O programa
  `bin/guardian-conformance` repete a matriz com a imagem de conformidade.
- [`SHA256SUMS.txt`](../build-phase12c/delivery-20261021/SHA256SUMS.txt) e
  [`delivery.json`](../build-phase12c/delivery-20261021/delivery.json): identificação
  e conferência dos pacotes.

O ZIP é conferido contra todos os 382 hashes do código aceito. Os executáveis
ELF do pacote correspondem aos binários Release validados. As imagens do
convidado são dados para o VMM; seu bit de execução é removido no pacote Linux.
O diff desde o backup está em `build-phase12c/review/changes.diff`.

O encerramento formal da 12C exige revisar a dependência 12B e completar as
referências nativas das versões declaradas. Aprovação no Linux, revisão cruzada
de sintaxe e comparação com valores esperados não substituem essas referências.

## Referências de contrato

O teste considera a quantização e a retomada descritas em
[Sleep](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep),
a frequência/contagem separadas de
[QueryPerformanceCounter](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter),
a posse inicial e a recursão de
[CreateMutexA](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexa),
e a cobertura das páginas intersectadas pelo intervalo em
[VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect).
Essas especificações orientam os casos; referências executadas continuam sendo
evidências separadas.
