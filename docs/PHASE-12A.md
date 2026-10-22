# Fase 12A — Perfis de hardware e driver de referência

**Concluída para a matriz de referência em 19/10/2026.** As verificações Windows/WHP e Linux/KVM foram aprovadas com os retestes registrados abaixo.

A entrega encerra a 12A para a matriz descrita abaixo. A fase 12 continua aberta; 12B–12E e fase 13 não foram iniciadas. A entrega inicial e seus resultados permanecem em `build-phase12a/`; as evidências desta conclusão ficam em [validation.json](../build-phase12a-complete/validation.json).

## Matriz implementada

| Item | Comportamento validado | Limite |
| --- | --- | --- |
| Perfil por VM | Modelos editáveis de CPU, GPU, disco, BIOS e placa-mãe; IDs sintéticos persistentes | Modelo solicitado e capacidade efetiva são campos distintos |
| CPU | Nome de até 48 bytes no CPUID, topologia de uma vCPU e mesma identidade no firmware/registro | Preserva instruções e recursos reais; virtualização permanece visível |
| Firmware | Tabela SMBIOS 3.2 sintética, consulta RSMB por `GetSystemFirmwareTable` e pelo adaptador MiniNt | Não implementa uma BIOS PC, ACPI ou inicialização de firmware completo |
| Registro | Projeção de 15 valores, incluindo CPU, BIOS, placa-mãe e dispositivos efetivos | Valores do perfil são somente de leitura; enumeração Win32 geral continua limitada |
| GPU e disco | Compositor BGRA próprio e dispositivo de blocos de 64 KiB nos respectivos contratos | Selecionar GT 1030 ou outro modelo não implementa esse hardware/driver |
| Driver | Referência x64 do projeto, `ROOT\GUARDIAN_PROBE`, INF System, alvo declarado NT 10 ou 11 | ABI experimental de driver de software; sem WDM/PnP/KMDF/WDDM completos |
| Recursos | Driver executado com 16, 32 e 64 MiB; uma vCPU | Ampliação de RAM, núcleos e disco pertence às próximas entregas |

O alvo NT é uma declaração de compatibilidade do perfil, não uma implementação integral daquela versão do Windows. O nome escolhido para CPU não emula uma arquitetura ou conjunto de instruções diferente.

## Uma fonte de identidade

O inventário v1 em GPA `0x138000` continua sendo a fonte comum. A página é somente de leitura nas permissões físicas da VM. O kernel confere tamanho, versão, campos, topologia e nome obtido por CPUID antes de publicar o hash do inventário lido.

O mesmo conteúdo gera os valores de registro e as estruturas SMBIOS de BIOS, sistema, placa-mãe e processador. UUID e seriais vêm da identidade sintética do perfil. A referência do host lê campos de modelos, sem consultar campos de serial de disco, UUID de firmware, MAC, credenciais ou arquivos pessoais. Um campo indisponível mantém o modelo Guardian.

O programa de teste em CPL3 chama a API de firmware, consulta o registro e compara a CPU com CPUID. Ele verifica consultas de tamanho, buffer pequeno, leitura em partes e recusa de escrita. No Windows, outro teste executa syscalls reais do adaptador MiniNt para consultar registro, firmware e os campos implementados de RAM/vCPU. `MiniNt::ConfigureHardware` recebe explicitamente o inventário; os utilitários históricos sem esse adaptador não viraram automaticamente gerenciadores de perfis do catálogo.

## Importação e execução do driver

A importação `.sys` histórica permanece inerte. Pacotes INF v2 preservam INF, imagem e catálogo opcional, validam limites e derivam novamente os metadados ao abrir o pacote. A implementação verifica arquitetura, decoração NT, dependências, dispositivo, serviço e imports. Caminhos que escapam do pacote, arquivos ausentes, metadados divergentes e ações não suportadas são recusados ou recebem diagnóstico de incompatibilidade.

CAT recebe apenas inspeção limitada de envelope DER SignedData e armazenamento inerte. **Assinatura, pertencimento ao catálogo e confiança de certificados não são autenticados. Pacotes com CAT são inelegíveis para execução nesta política de laboratório.** Checksum controla integridade; não autentica nem classifica segurança.

O pacote executável da matriz é `guardian-hardware-probe.inf` com `guardian-hardware-probe.sys`, compilado das fontes do projeto. Ele importa quatro operações da API de kernel própria: criação/exclusão de dispositivo e alocação/liberação de pool com tag. A API é um módulo separado carregado no convidado; não utiliza o `ntoskrnl.exe` do Windows host.

O contrato `hardware` inicia `guardian-kernel-hardware.gk`. O loader do convidado mapeia e protege os dois PEs, resolve imports e chama o `DriverEntry` real do `.sys`. O driver cria recursos, realiza quatro escritas e quatro leituras DWORD no dispositivo virtual e executa sua rotina de descarregamento. O convidado verifica resultados e liberação dos recursos; o host testemunha as operações do dispositivo e recusa uma alegação funcional sem essas operações. Esse teste é prova de funcionamento da referência, não atestado de confiabilidade de um driver arbitrário.

Os estados têm significados separados:

- **Importado:** pacote copiado e auditado; nenhum código foi executado.
- **Configurado:** pacote/API selecionados em `installed.gdi`; ainda sem prova de inicialização.
- **Instalado:** imagem carregada e imports vinculados neste boot experimental em memória.
- **Inicializado:** `DriverEntry` retornou sucesso.
- **Funcional:** operações previstas pelo contrato concluíram com prova do dispositivo.
- **Descarregado:** rotina de unload liberou os recursos e o loader removeu as imagens.

Não há instalador SCM/PnP completo ou registro persistente de serviços Windows nesse contrato. O relatório de `drivers` conserva a observação do boot indicado e mostra o descarregamento separadamente. A referência com `DriverEntry` falho comprova que importação/carregamento não viram sucesso de inicialização. Nenhum driver importado é instalado ou executado no host.

## Snapshots e clones

Snapshots vinculam identidade, CPUID, inventário, opções, imagens de driver/API, proteções de memória e estado do dispositivo. O teste pausa depois de uma escrita, com resposta ainda pendente, restaura em candidatos novos e conclui as operações e o unload. Alterações das imagens, descritor, perfil e permissões são recusadas.

Clones preservam a identidade para manter coerência com seu checkpoint, copiam o pacote selecionado e precisam produzir sua própria observação de execução. Edição de hardware ou instalação de outro pacote exige VM parada, sem checkpoints ou snapshots nomeados. Snapshots dependem do backend, perfil, imagens e formato de CPU compatíveis; não há promessa de migração de checkpoints de revisões intermediárias. Os arquivos anteriores não são apagados automaticamente.

## Uso da referência

Na pasta dos binários da configuração escolhida, mantendo `guardian-driver-api.gkd` ao lado do gerenciador:

```powershell
guardian-vm --root D:\VMs create teste caminho\guardian-kernel-hardware.gk hardware
guardian-vm --root D:\VMs hardware-config teste --nt win11 --cpu "CPU de referencia" --board "Placa virtual"
guardian-vm --root D:\VMs driver-import teste referencia caminho\drivers\guardian-hardware-probe.inf win11 board
guardian-vm --root D:\VMs driver-install teste referencia
guardian-vm --root D:\VMs start teste
guardian-vm --root D:\VMs status teste
guardian-vm --root D:\VMs drivers teste
```

`driver-install` prepara o pacote para o próximo boot. Os estados de execução aparecem depois que o convidado efetivamente executa o teste. O mesmo fluxo existe no gerenciador ELF do Linux.

## Validação

| Plataforma/configuração | Resultado final distinto | Evidência |
| --- | --- | --- |
| Windows Debug | 59 testes validados, incluindo retestes | Suíte completa e retestes preservados |
| Windows Release | 59 testes validados, incluindo retestes | Suíte completa e retestes preservados |
| Linux Debug | 39 testes validados, incluindo retestes | Convidados LLVM nativos e host GCC/KVM |
| Linux Release | 39 testes validados, incluindo retestes | Convidados LLVM nativos e host GCC/KVM |
| Linux AddressSanitizer/UndefinedBehaviorSanitizer | 27/27 aprovados | Núcleos, parsers, rede e os sete testes de hardware |
| Artefatos do convidado | 16 imagens por configuração em cada toolchain | Manifestos e hashes no relatório |
| Processos de validação restantes | Nenhum | `process-cleanup.json` |

As suítes completas e os retestes são preservados separadamente. Não se atribui uma suíte limpa de 59/59 ou 39/39 a tentativas que tiveram falhas: o relatório estruturado associa cada resultado final à sua execução. No Linux Debug, passaram 37/39 antes dos reparos de rede; os casos de rede foram repetidos. Na Release, passaram 37/39 antes da correção de carregamento da API no cenário negativo do driver; esses dois casos foram repetidos em Debug e Release. A passagem com sanitizers foi limpa, 27/27.

As suítes completas Windows e os retestes também permanecem separados. A primeira Debug passou 58/59: o adaptador MiniNt ainda permitia abrir uma chave vazia de dispositivo removido; o perfil configurado passou a recusar chaves ausentes. A primeira Release passou 58/59: catálogo excedeu 60 segundos durante execução concorrente com outras tarefas. O reteste isolado concluiu em 22 segundos; a passagem final também aprovou catálogo. Nenhuma asserção foi removida. Os limites globais do gerenciador permaneceram inalterados. O cenário de duas VMs de laboratório usa explicitamente o orçamento já disponível de 30 segundos, devido à captura durável concorrente; os testes de prazo curto da CPU permanecem ativos.

A comparação das 32 imagens MSVC confirmou seções executáveis idênticas após o ajuste exclusivo de leitura de endereço para LLVM. Posteriormente, as duas imagens do contrato hardware foram atualizadas e revalidadas para carregar a API explicitamente, incluindo drivers sem imports após otimização. Os retestes finais Windows cobrem os dez testes MiniNt, os sete de hardware, snapshots, rede e catálogo; testes adicionais executam o driver nos alvos NT 10/11 e nos limites de RAM.

No Linux, a validação inicial com imagens MSVC revelou dependência de HLT no teste de I/O, preparação não repetível de um diretório de teste e falhas de tempo sob concorrência. O teste passou a encerrar explicitamente após as três transferências, a preparação ganhou diretório por execução e a inicialização concorrente de rede ganhou barreira. O diagnóstico adicional identificou o bloqueio do switch de laboratório expirando durante publicações duráveis. A espera de aquisição passou de 100 tentativas curtas para uma janela monotônica de dois segundos, com diagnóstico por VM. Um teste segura a trava por 250 ms e comprova recuperação; outro mantém a trava e confere recusa limitada. A recompilação nativa encontrou também declaração de intrinsic do LLVM 18 que truncava CR2; a leitura agora tem ABI explicitamente de 64 bits. O teste real de CPU voltou a conferir falha em endereço de 4 GiB. A compilação otimizada do driver negativo removeu seus imports; a API de kernel agora é carregada explicitamente antes do driver, garantindo que o teste observe o retorno real de DriverEntry, em vez de uma falha anterior de resolução de módulo.

As imagens finais Linux são recompiladas no próprio Linux com LLVM; os executáveis do host são ELF compilados com GCC. Os formatos PE/COFF do convidado não dependem de Wine, QEMU, SDK Windows ou execução de programas Windows no host Linux. KVM mantém a limitação já documentada: NX depende das tabelas do convidado; não há proteção de execução independente de segundo estágio nessa API.

Comandos Windows para uma árvore própria:

```powershell
cmake -S . -B build-12a -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DGUARDIAN_BUILD_GUEST_CONTRACT=ON
cmake --build build-12a --config Debug --parallel 4
cmake --build build-12a --config Debug --target guardian_guest
ctest --test-dir build-12a -C Debug --output-on-failure
# Repetir build, auditoria e testes com Release, sem testes concorrentes.
```

Para recompilar convidado e host no Linux:

```sh
python3 tools/build_guest_linux.py --llvm /usr/lib/llvm-18 --config Debug --output /caminho/absoluto/guest-debug
cmake -S . -B build-12a-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DGUARDIAN_TEST_KVM=ON -DGUARDIAN_KVM_GUEST_DIR=/caminho/absoluto/guest-debug
cmake --build build-12a-debug --parallel 4
ctest --test-dir build-12a-debug --output-on-failure
```

Dependências usadas: CMake, Ninja, compilador C++20, Python 3, OpenSSL, SDL2 e LLVM com `clang`, `lld-link` e `llvm-ml`. O acesso a KVM/NX já deve estar disponível. Debug e Release usam diretórios próprios. O relatório estruturado contém hashes das fontes/binários e resultados por teste; [changes.patch](../build-phase12a-complete/changes.patch) permite revisar a entrega a partir do snapshot anterior à 12A.

## Limites e parada

Drivers comerciais, acesso direto a dispositivos físicos, GPU NVIDIA/AMD/Intel, WDDM, KMDF, PnP completo e autenticação CAT não são declarados suportados. As 167 importações ausentes do Flappy não foram implementadas neste lote, e o programa não recebeu declaração de compatibilidade.

A 12A é uma matriz de referência testada para continuar o desenvolvimento. Compatibilidade Windows geral, recursos maiores e avaliação adicional de realismo continuam nas entregas futuras. A execução é encerrada nesta subfase, antes da 12B.
