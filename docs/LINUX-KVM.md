# Execução do convidado no Linux com KVM

Atualização: 18/10/2026. O NX foi habilitado na BIOS e confirmado pelo Linux.
`/dev/kvm` agora está acessível ao usuário; a API retornou versão 12.
O executor Linux foi implementado e executa os kernels próprios em CPL0/CPL3.
O produto Windows continua usando MSVC/MASM/WHP.

## Compilar e executar

O host precisa de Linux x86-64, C++20, CMake, make, Python e OpenSSL 3.
O convidado pode ser recompilado no Linux com uma instalação nativa de LLVM
que contenha `clang`, `lld-link` e `llvm-ml`. Nenhum Windows SDK ou CRT é ligado.
Nesta máquina foi preparado LLVM 23.1.3 no cache do usuário; não é necessário
instalá-lo no sistema. O compilador não é incluído no pacote de execução.

```bash
# A árvore própria preserva caches e evidências dos builds anteriores.
GUARDIAN_LINUX_BUILD_DIR="$PWD/build-linux-kvm/Release" ./Build-Linux.sh Release
python3 tools/build_guest_linux.py --config Release

./build-linux-kvm/Release/guardianav --capabilities
./build-linux-kvm/Release/guardianav boot-kernel \
  build-guest-linux/Release/guardian-kernel-complete.gk storage \
  "$PWD/resultado-kvm.json" --disk "$PWD/demonstracao.gvd" "$PWD/demonstracao.gvo" --create-disk
```

`GUARDIAN_LLVM_ROOT` ou `--llvm` selecionam outra instalação de LLVM.
`--output` seleciona a pasta do convidado. O script gera as doze imagens de
produção, duas imagens de falha, os executáveis/DLLs de referência e os headers
que incorporam seus bytes. Cada build registra a versão do compilador e os
hashes das imagens. As ferramentas executadas no host são ELF.

PE/COFF continua sendo o formato das imagens do convidado. Os PEs próprios
executam exclusivamente dentro da VM. O scanner não inicia os arquivos que lê.
Não há Wine ou QEMU em nenhum dos dois fluxos.

O mesmo executor está disponível como `guardian-kvm <imagem> <contrato> <relatório>`.
Os contratos são `basic`, `cpu`, `memory`, `threads`, `processes`, `io`, `storage` e `network`.
As imagens `loader`, `runtime`, `lifetime` e `complete` usam o contrato `storage` e exigem
base/overlay explícitos. O retorno zero exige o protocolo Hello/Diagnostic/Stop,
status zero em CPL0 e os relatórios/contadores do contrato selecionado.

Cada VM tem uma vCPU e 16–64 MiB de RAM. `--ram-mib`, `--boot-id` e
`--timeout-ms` selecionam os limites publicados na ajuda. O prazo máximo é
30 segundos; um watchdog também encerra convidados que não produzem saídas.
O relatório JSON registra backend, estado, temporizador, fault, contadores de
recursos e resultados do disco. Arquivos de base/overlay são dados próprios de
64 KiB; não são discos físicos. A base permanece somente para leitura.

## Implementação e limites

`src/linux/KvmMachine.cpp` cria a VM diretamente pela API KVM, instala RAM,
registros x64, CPUID com NX e o estado inicial de ponto flutuante. O temporizador
interrompe a execução por sinal dirigido à thread da vCPU, atende o relógio com
a vCPU parada e injeta a interrupção somente quando o convidado permite.
HLT, prazo vencido, orçamento excedido, I/O desconhecido e acessos físicos
indevidos não são convertidos em conclusão bem-sucedida.

O boot usa o mesmo `KernelBoot.cpp`, validações de protocolos e `IoBackend.cpp`
do Windows. Os trechos Windows dos quatro arquivos compartilhados alterados
foram comparados com o snapshot e preservam o conteúdo anterior. Essa inspeção
não equivale a uma nova compilação/teste no Windows.

O armazenamento POSIX mantém o formato de base/overlay anterior, com bloqueio
exclusivo, fingerprint da base, registros sequenciais, checksum e recuperação
de cauda incompleta. Arquivos especiais, links finais, base e overlay com a
mesma identidade e overlay já aberto são recusados. Escritas incompletas
interrompem novas operações; não são publicadas em memória como sucesso.

Há uma diferença material de proteção: a API pública de memória do KVM usada
aqui fornece páginas físicas somente para leitura, mas não uma permissão de
execução independente por página. O NX é aplicado pelas tabelas do convidado.
O JSON informa `secondStageExecuteProtection: false`. Portanto este backend
serve à execução e validação dos kernels próprios; a equivalência de proteção
física com o WHP não é declarada. As fontes fixas do loader continuam sem busca
em caminhos/DLLs do host. Não foi acrescentada execução de PEs arbitrários.

A adaptação do montador é textual e ocorre antes de gerar objetos: expande as
macros MASM, ajusta diretivas/rótulos e expressa os offsets numéricos de FS/GS
como endereços absolutos. A saída e a entrada de cada etapa ficam no build.
Não há remendo de bytes em código de máquina. `ClangIntrinsics.asm` fornece as
instruções declaradas pelos headers LLVM que esse compilador não expande.

Referência da interface: [documentação oficial da API KVM](https://docs.kernel.org/virt/kvm/api.html).

## Aceitação reproduzível

```bash
python3 tools/validate_phase9.py
```

O verificador cria uma pasta nova, recompila o host e o convidado, exige KVM
disponível e registra falha se faltar alguma etapa. Não ignora testes:

| Matriz | Critério |
| --- | --- |
| Debug e Release | 23 CTests por configuração |
| AddressSanitizer/UndefinedBehaviorSanitizer | 23 CTests, incluindo boot real e host de disco |
| Kernels próprios | 11 imagens × 2 configurações × 3 tamanhos de RAM × 2 boots = 132 boots |
| Falhas de kernel e double fault/IST | 4 execuções recusadas como sucesso, com relatório fatal válido |
| Estado persistente | Segundo boot usa o mesmo overlay, recupera a sequência anterior e mantém a base intacta |
| Aplicações PE | Marcadores de DLL/TLS, 31 workers, filhos, rollback, saída explícita e captura real da pilha |
| Executor KVM | 33 verificações, incluindo argumentos, sessão, boot adulterado, prazo e erros de dispositivo |
| Armazenamento do host | 32 verificações de persistência, isolamento, corrupção, bloqueio e recuperação |

As evidências ficam em `build-linux-kvm/validation-<data>/validation.json`, com
logs, JUnit, hashes das fontes/artefatos e relatórios individuais. Sentinelas no
PATH detectam chamadas a Wine, programas Windows ou QEMU durante a validação.
O manifesto só aprova a matriz se as fontes permanecerem iguais até o fim.

## Estado da fase 8

A implementação restante foi concluída para a matriz de consoles próprios:
exceções em CPL3, continuação, unwind não local e DLLs dinâmicas. A auditoria
por aplicação, as recusas e os limites estão em
[PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md).
A aceitação nativa Windows desta revisão continua pendente. A fase 8 permanece
aberta para essa aceitação e a fase 9 não foi iniciada.

Resultado histórico da fase 8: **132 boots, quatro recusas esperadas e 60 execuções CTest**,
sem falhas ou testes ignorados. Evidência:
`build-linux-kvm/validation-20261018-050639-287554/validation.json`.
O pacote atualizado é `dist/guardianav-phase8-linux-x86_64-20261018.tar.gz`.
Inclui os executores ELF, as 13 imagens (11 de produção e duas de falha),
assinaturas, documentação e manifestos. Os pacotes históricos permanecem.

Resultado anterior do backend, preservado: 120 boots e 57 execuções CTest em
`build-linux-kvm/validation-20261018-041930-839301/validation.json`.
O pacote histórico `dist/guardianav-linux-kvm-x86_64-20261018.tar.gz` foi extraído,
conferido por SHA-256 e executou dois boots adicionais de `lifetime`.

## Continuação da fase 9

Snapshots completos e gerenciamento pelo executável `guardian-vm` foram
acrescentados. O verificador atual é `tools/validate_phase9.py`; o comando e as
contagens da entrega anterior continuam identificados como evidência histórica.
As provas novas cobrem registradores/XSAVE/interrupções, 16/32/64 MiB, retomadas,
reinícios de disco, VMs simultâneas, falhas e isolamento. Ver [PHASE-9.md](PHASE-9.md).

A fase 9 passou em 23 CTests por configuração (69 execuções), 132 boots de
regressão, 81 retomadas/boots de snapshots e 24 boots pelo gerenciador. Evidência:
`build-phase9/validation-20261018-103055-f78507d1/validation.json`.
A aceitação nativa Windows permanece pendente.

## Fase 10 — Rede do convidado

Perfil em [PHASE-10.md](PHASE-10.md). Para teste direto, depois dos builds:

```bash
./build-linux/Release/guardian-kvm build-guest-linux/Release/guardian-kernel-network.gk \
  network resultado-rede.json --network simulated --capture "$PWD/capturas" --timeout-ms 20000
```

`network` começa sem rede. `guardian-vm network` configura serviços simulados,
laboratórios e relay com destinos explícitos. A aceitação corrente é
`python3 tools/validate_phase10.py`; scripts/resultados das fases 8/9 são históricos.

Aceitação final da fase 10: 81 execuções CTest e 132 boots de regressão
aprovados em Debug/Release/Sanitize, sem Wine/QEMU. Evidências e limites:
[PHASE-10.md](PHASE-10.md). A aceitação nativa Windows permanece pendente.

## Fase 11 — Aplicações gráficas próprias

A imagem `guardian-kernel-graphics.gk` adiciona um contrato distinto, `graphics`,
com uma aplicação CPL3 própria e duas ferramentas/janelas. O build gera 13 imagens de produção e dois controles
fatais. `guardian-gui IMAGE OUTPUT [--interactive]` apresenta a saída por
software; o gerenciador admite `create ID IMAGE graphics`, snapshots e clones
do roteiro determinístico. O modo interativo é direto, sem snapshots.

A aceitação atual é `python3 tools/validate_phase11.py`; os validadores antigos
são históricos. Veja [PHASE-11.md](PHASE-11.md) para suporte, bibliotecas,
permissões e a separação entre validação Linux/KVM e Windows/MSVC/WHP.

Aceitação final da fase 11: 96 CTests, 132 boots de regressão e 45
boots/retomadas gráficos aprovados, com nove pausas gráficas e ASan/UBSan.
Evidência `build-phase11/validation-20261018-191149-4baab81b/validation.json`.
O gate nativo Windows permanece pendente.
