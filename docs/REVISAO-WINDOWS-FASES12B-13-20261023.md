# Revisão Windows das fases 12B a 13 — 23/10/2026

**Estado:** matriz nativa Windows/MSVC/WHP validada: **80 testes distintos em
Debug**, com os retestes descritos abaixo, e **80/80 em uma suíte completa
Release**, sem ignorados. Nenhuma fase posterior foi iniciada. As pendências
de escopo e de comparação entre versões permanecem abertas.

Foram revisados os fontes da continuação da 12B, 12C, 12D, 12E e 13 na pasta
`D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. A base Git é `63f02b7`;
o arquivo dos fontes da base Git anterior às correções está em
`build-windows-review-20261022/baseline-sources.zip`.
O ambiente usa Windows build `10.0.26300`, MSVC `19.44.35228` e WHP nativo.

## Correções

- Ligação e includes: os dois testes de unwind recebem `guardian_abi` e seus
  cabeçalhos compartilhados. O teste host de armazenamento inclui `Files.cpp`,
  exigido pelo novo cálculo de uso agregado.
- Compilação MSVC: nomes locais que ocultavam variáveis foram corrigidos no
  programa de conformidade e na criação de serviços. O kernel de análise
  exclui da compilação os probes antigos que ficavam após seu retorno.
- Os erros textuais da sessão de recursos são convertidos para UTF-8 no
  Windows, sem reduzir caracteres largos a bytes.
- Os testes incluem seus cabeçalhos necessários, usam valores de bytes com
  tipo explícito e copiam seus textos literais com verificação de tamanho.
  As verificações `/W4 /WX` foram preservadas.
- O controle global de recursos aguarda por até cinco segundos pela transação
  de admissão quando outro catálogo a utiliza. O novo teste reproduziu antes
  da correção `Cannot acquire VM lease`; depois passou, sem alterar a
  configuração consultada. As leases exclusivas das VMs continuam exclusivas.
- O teste de disco esparso aloca dinamicamente seus buffers de trabalho de
  64 KiB. O Windows confirmou o estouro anterior com a exceção `0xc00000fd`
  no evento de aplicação 1000. O reteste conserva as verificações de conteúdo,
  persistência, clone, descarte, cota e propriedade.
- O teste funcional PE de runtime/ciclo de vida usa dez segundos por boot no
  Windows, como o teste PE completo. O prazo antigo de três segundos cancelou
  um boot durante a sequência de I/O após o programa PE concluir. Os outros
  16 boots desse teste tiveram mediana de aproximadamente 1,4 segundo.
  O ajuste fica restrito a esse teste funcional com gravações duráveis no host;
  as verificações específicas de relógio, watchdog e travamento foram mantidas.
  Permanecem 40 boots por contrato, nas duas quantidades de RAM e com os mesmos
  critérios de conteúdo, persistência, memória e encerramento.

## Evidências e método

| Execução | Resultado |
|---|---|
| Debug, primeira suíte completa | 78/80; timeout PE e estouro de pilha preservados |
| Debug, cinco testes afetados pelos ajustes finais | 5/5 |
| Debug, cobertura consolidada | 80 distintos aprovados; zero ignorados |
| Release, suíte completa | 80/80; zero falhas e zero ignorados |

A cobertura Debug reúne 75 resultados da primeira execução e cinco retestes.
Ela não representa uma única suíte integral sem falhas. Entre a primeira
execução e o reteste mudaram somente `test/GuestPeBootTests.cpp` e
`test/SparseDiskTests.cpp`; os cinco testes dependentes desses arquivos foram
reexecutados. Os fontes do produto permaneceram idênticos nesse intervalo.

Os registros ficam em `build-windows-review-20261022/`:

- `attempt-01/`: primeira compilação limpa e seus erros originais.
- `admission-before.log` e `admission-before.xml`: reprodução da concorrência.
- `stack-overflow-event.xml`: diagnóstico original do Windows.
- `debug-01/`: suíte completa inicial, XML e hashes de entrada.
- `debug-retest-03/`: cinco retestes e relatórios dos contratos.
- `release-01/`: compilação limpa Release, suíte e hashes de entrada.
- `windows-debug-consolidated.xml`: resultados Debug com a origem de cada teste.
- `validation.json`: contagem consolidada, hashes estáveis e limites da aceitação.
- `artifact-hashes.json`, `source-inputs-final.json` e `changes.patch`: artefatos,
  entradas compiladas e diferenças dos arquivos já versionados.
- `process-cleanup.json`: nenhum worker de teste remanescente.

O Debug foi executado na unidade D. A compilação e os dados de teste Release
usam `C:\Users\gabri\.codex\tmp\aegis-windows-review-20261022-release`;
os fontes e registros permanecem na pasta original do projeto.

## Cobertura observada nas duas configurações

- 12B: RAM real de 128 MiB, checkpoints, cotas, disco esparso, migração com
  caminhos acentuados e recuperação após encerrar forçadamente um worker.
  O kernel próprio `resources` passou com uma a quatro CPUs e restauração.
- 12C: 15 execuções, com 795 observações do contrato; captura de referência
  nativa da build `10.0.26300`.
- 12D: 438 verificações Debug e 395 Release no ciclo de sessões,
  incluindo mais de 31 segundos de
  execução acumulada, suspensão, retomada e interação automatizada.
- 12E: 12 perfis com restauração e recusa de política alterada; seis pares de
  cenários no gerenciador, com 613 verificações Debug e 616 Release
  de persistência e privacidade.
- 13: 1.233 verificações do núcleo e 864 nos boots em cada configuração;
  no ciclo do gerenciador foram 1.192 Debug e 1.175 Release.
  A cobertura inclui nove domínios na linha do tempo, políticas de
  permitir/negar/simular, exportação, perda de
  eventos e restauração com a política vinculada.

As contagens do gerenciador incluem consultas de estado e podem variar com o
andamento da execução. Os 80 testes nomeados e seus critérios foram preservados.

## Pendências preservadas

A referência da build instalada não substitui as referências exatas de
`6.1.7601`, `10.0.19045` e `10.0.22631`, ainda ausentes na 12C.
O comparador informa `referenceCompared: false`; uma lista de diferenças vazia
nesse estado não encerra a comparação entre versões.

A integração de SMP e do volume escalável aos perfis PE anteriores da 12B
continua aberta. A compatibilidade geral com programas e drivers Windows
permanece limitada aos contratos documentados. Estes testes não declaram
conclusão integral das fases nem indetectabilidade do sandbox.

O Linux não foi reexecutado após estas correções Windows; os resultados Linux
anteriores permanecem como registros históricos das respectivas revisões.
