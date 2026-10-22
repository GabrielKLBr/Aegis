# Compilação e execução nativas no Linux

Atualização: 18/10/2026. Pedido desta continuação: compilar também para Linux,
evitando Wine. O suporte Windows/MSVC/WHP permanece no projeto.

**Continuação após habilitar NX:** agora há executor KVM e recompilação das
imagens do convidado no Linux. Uso, limites e matriz: [LINUX-KVM.md](LINUX-KVM.md).

## Disponível

`guardianav` é um executável **ELF x86-64**, com ponto de entrada Linux. Usa
OpenSSL 3 para SHA-256 e interfaces POSIX para leitura. Não inicia Wine, DLLs,
executáveis analisados ou subprocessos para calcular hashes.

- Scanner recursivo por SHA-256 e substring, com o formato de `signatures.txt`
  existente e a regra interna de teste EICAR.
- Hash SHA-256 de arquivo inteiro, incluindo nomes UTF-8.
- Inspeção estática de imagens `.gk` pelo parser existente, sem inicializar VM.
- Compilação nativa dos mesmos algoritmos de memória, escalonamento, processos,
  handles/direitos, I/O, volume, runtime, loader PE e unwind utilizados no convidado.

O scanner só lê os arquivos. Uma ausência de correspondência significa que as
regras consultadas não coincidiram; não é classificação geral de segurança.
Não há quarentena automática, monitoramento de diretórios, integração com
Security Center ou interface gráfica nesta versão Linux.

## Compilar

Dependências: Linux x86-64, GCC/Clang com C++20, CMake 3.22+, make, Python 3.9+
e OpenSSL 3 com headers de desenvolvimento. Em Mint/Ubuntu, os pacotes usuais
são `g++`, `cmake`, `make`, `python3` e `libssl-dev`.

```bash
./Build-Linux.sh Release
./Build-Linux.sh Debug
./Build-Linux.sh Sanitize
```

Os binários ficam em `build-linux/<configuração>/`. `Sanitize` usa Debug com
AddressSanitizer e UndefinedBehaviorSanitizer. As árvores Windows e as evidências
anteriores não são substituídas.

Nesta máquina, CMake e os headers de OpenSSL foram preparados no cache do
usuário, em `~/.cache/guardian-native-deps`, sem instalação administrativa.
O script reconhece esse cache; em uma instalação comum usa as dependências do
sistema. Também aceita `CMAKE`, `GUARDIAN_OPENSSL_ROOT`, `GUARDIAN_BUILD_JOBS`
e `GUARDIAN_LINUX_BUILD_DIR` para instalações alternativas.

O fluxo Windows continua sendo `Build-Kernel.ps1`, com CMake 4+ e MSVC.
O CMake escolhe o fluxo pelo sistema de destino; não executa uma compilação
Windows por baixo da compilação Linux.

## Usar

```bash
./build-linux/Release/guardianav --capabilities
./build-linux/Release/guardianav scan "/caminho/da/pasta"
./build-linux/Release/guardianav scan "/caminho/arquivo" --json
./build-linux/Release/guardianav scan "/caminho/arquivo" --signatures signatures.txt
./build-linux/Release/guardianav hash "/caminho/arquivo" --json
./build-linux/Release/guardianav inspect-kernel "/caminho/imagem.gk"
```

A base padrão é procurada junto ao executável ou em `../share/guardianav/`
relativamente à instalação. Ela não depende da pasta atual do terminal.
O pacote em `dist/guardianav-linux-x86_64-20261018/` inclui a base e instruções.

| Código de saída | Resultado |
| --- | --- |
| 0 | Leitura concluída, sem correspondências; ou operação hash/inspeção concluída |
| 1 | Uma ou mais assinaturas coincidiram, sem leitura/cobertura incompleta |
| 2 | Argumentos, base de assinaturas ou configuração inválidos |
| 3 | Erro de leitura, entrada ignorada ou cobertura parcial, inclusive se houve detecção |

A busca de conteúdo cobre até 4 MiB; o SHA-256 cobre o arquivo inteiro.
Arquivo maior sem correspondência por hash é marcado como parcial. Links
simbólicos, FIFOs e outros arquivos especiais são ignorados na varredura e
recusados no hash, sem esperar dados de um pipe. Permissões insuficientes e
alterações detectadas durante a leitura produzem erro. Há limite de profundidade
de 128 níveis. Nomes com bytes de controle são escapados nos relatórios.

A base tem até 4 MiB, 10.000 regras e 4.096 bytes por linha de regra. Regras
inválidas causam erro explícito nesta implementação Linux. Os resultados são
emitidos durante a varredura, sem acumular a árvore inteira em memória.

## Validação e alcance

```bash
python3 tools/validate_linux_native.py
# Aceitação completa da etapa atual, com KVM:
python3 tools/validate_phase9.py
```

O verificador exige Linux x86-64, compila Debug, Release e Sanitize e exige
**20 CTests por configuração** (algoritmos, scanner e disco do host; o teste
real KVM é acrescentado pelo verificador separado), sem falhas ou ignorados. Confere que os
executáveis são ELF x86-64, registra bibliotecas nativas e hashes das fontes
e artefatos e exige capacidades compatíveis com este subconjunto.

Resultado histórico anterior ao backend KVM: os 17 CTests passaram em cada uma das três configurações
(51 execuções, sem falhas ou ignorados). O teste da CLI aprovou 89 verificações;
o parser PE aprovou 1.353; a busca de tratadores manteve as 3.059 verificações.
O pacote foi extraído em pasta temporária e executou capacidades e varredura
com a base localizada junto ao executável.

Evidência: `build-linux/validation-20261018-032855-683697/validation.json`, com
logs por configuração, hashes e identificação dos executáveis ELF. Os originais
anteriores ao fluxo Linux estão em
`snapshots/codex-before-linux-native-20261018/`.

Os testes de scanner comparam SHA-256 com `hashlib`, cobrem fronteiras de bloco,
arquivos grandes, assinaturas de conteúdo/hash, caminhos com espaços/Unicode,
recursão, entradas inacessíveis, links, FIFOs, JSON e preservação dos bytes.
São criadas sentinelas de `wine`, `wine64`, `cmd.exe` e `powershell.exe` no PATH
do teste para detectar uma eventual chamada indevida.

Três imagens próprias congeladas em `test/fixtures/native/` são usadas apenas
como dados nos testes de parsers. Foram copiadas dos outputs MSVC anteriores
e têm origem e SHA-256 no manifesto local. Nenhum código PE é executado nesses
testes de parser. A matriz KVM separada recompila as imagens e executa os PEs
próprios dentro do convidado.

Os testes de unwind informam expressamente que o oráculo Windows não rodou.
Testes de algoritmos e parsers não substituem a execução do kernel em CPL0/CPL3.
Após a habilitação de NX na BIOS, o backend KVM foi implementado e executa
os kernels próprios. `--capabilities` distingue o suporte compilado
(`vmExecution: true`) da disponibilidade local (`vmAvailable`). As diferenças
de proteção física e a validação estão em [LINUX-KVM.md](LINUX-KVM.md).

A implementação restante da fase 8 foi concluída para a matriz de consoles
próprios: exceções em CPL3, unwind não local e DLLs dinâmicas. A validação
Linux/KVM passou com 20 CTests em cada configuração (incluindo o teste KVM),
132 boots reais e quatro recusas esperadas. A aceitação nativa Windows desta
revisão continua pendente; a fase 9 não foi iniciada. Evidências e limites em
[PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md).

## Continuação da fase 9

O executor `guardian-vm` acrescenta snapshots completos, gerenciamento,
clonagem e recuperação. A suíte sem VM tem agora 20 CTests; a matriz com KVM
tem 23 por configuração. Estado atual, comandos e limites em
[PHASE-9.md](PHASE-9.md). A fase 10 não foi iniciada.

A matriz completa da fase 9 passou no Linux: 69 execuções CTest, incluindo
ASan/UBSan e execução real, mais a regressão das 11 imagens anteriores.
Evidência: `build-phase9/validation-20261018-103055-f78507d1/validation.json`.

## Continuação da fase 10

Política, parser IPv4/IPv6, serviços simulados, laboratório e UDP por relay
têm fontes comuns para Windows e Linux. `Build-Linux.sh` sem KVM executa 22
CTests e produz 22 executáveis ELF, incluindo os novos testes compilados.
A matriz com KVM habilitado tem 27 CTests e é executada por
`tools/validate_phase10.py`. Resultados e comandos em [PHASE-10.md](PHASE-10.md).

Aceitação final da fase 10: 81 execuções CTest e 132 boots de regressão
aprovados em Debug/Release/Sanitize, sem Wine/QEMU. Evidências e limites:
[PHASE-10.md](PHASE-10.md). A aceitação nativa Windows permanece pendente.

## Continuação da fase 11

O fluxo atual acrescenta `guardian-gui`: visualizador nativo de aplicações
próprias dentro do convidado. Não é a interface completa do antivírus.
Com SDL2 de desenvolvimento disponível, são 24 CTests sem KVM e 32 com KVM;
o inventário sem KVM contém 24 executáveis ELF. O gate atual é
`python3 tools/validate_phase11.py`. A aceitação passou: 96 execuções CTest e 132 boots de regressão em
Debug/Release/Sanitize; gate Windows pendente.

A máquina de desenvolvimento usa headers SDL2 extraídos no cache, sem alterar
a instalação do sistema. Em uma instalação Linux normal, `libsdl2-dev` habilita
a mesma compilação; o pacote usa a biblioteca SDL2 nativa do sistema.
Instruções e limites: [PHASE-11.md](PHASE-11.md). Não há Wine/QEMU.
