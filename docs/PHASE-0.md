# Fase 0 — Arquitetura e fronteira de compilação

Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Data: 16/10/2026.

## Escopo desta entrega

Consolidar a arquitetura sobre os módulos existentes, migrar a compilação para
C++20 e estabelecer contratos verificáveis para o futuro kernel no convidado.
O alvo freestanding desta fase contém validadores de contrato e não tem ponto
de entrada, imagem inicializável, implementação de dispositivo ou carregador
de kernel. A fase 1 permanece uma entrega posterior, sujeita à solicitação do
usuário. Ao terminar a fase 0, parar.

## Decisões de arquitetura

1. WHP continua no host Windows como mecanismo de virtualização. O VMM próprio
   controla partição, RAM, vCPU e dispositivos, sem QEMU.
2. O kernel de destino executará em ring 0 no convidado. APIs Windows, alocadores
   do processo host, arquivos físicos e ponteiros host não fazem parte de sua ABI.
3. MiniNt (modelo B) permanece referência funcional para semântica e regressão.
   NtKernel (modelo A) permanece separado. Nenhum desses modelos já constitui
   o kernel convidado pretendido.
4. Preservar a organização atual e adicionar fronteiras de build; não mover
   módulos ou eliminar exemplos para aparentar uma arquitetura concluída.
5. A plataforma inicial é x64, little-endian, uma vCPU, RAM de 16 MiB a 1 GiB.
   Esse limite inicial acompanha o mapeamento de boot do VMM, não define o limite
   final do produto. SMP, cotas avançadas e perfis de hardware ficam nas fases
   previstas no roadmap.
6. Contratos compartilhados usam inteiros de largura fixa, GPAs e tamanhos;
   não contêm ponteiros, handles Windows, strings STL, bools ou objetos virtuais.
   Reservados precisam ser zero. Versões desconhecidas são recusadas.
7. A migração para kernel preservará algoritmos e testes quando possível. Os
   gerenciadores atuais que usam `std::map`, `shared_ptr`, exceções ou serviços
   Windows exigirão adaptação ao runtime próprio; copiar headers não basta.

## Inventário de módulos e dependências

| Componente existente | Estado verificável e dependências | Destino / trabalho posterior |
|---|---|---|
| `Scanner`, `SignatureDatabase`, `Sha256` | Scanner de hashes/conteúdo; arquivos Windows e CNG. | Host; ampliar detecção, formatos e conectores na fase 14. |
| `Quarantine` | Armazenamento XOR e metadados em arquivos. | Host; integridade, transações e recuperação na fase 15. |
| `RealTimeMonitor` | Monitor de diretório por API Windows. | Host; serviço, filas, perdas de eventos e IPC na fase 16. |
| `EngineFacade`, `UiApp` | Scanner em background e UI Direct2D/DirectWrite. | Host; integração WinUI 3 e idiomas na fase 17. |
| `Sandbox` | Execução restrita no host e encaminhamento ao Windows Sandbox. | Caminhos legados separados do kernel próprio; não equivalem ao novo convidado. |
| `WscIntegration` | Integração/consulta Windows Security Center. | Host; validar estado e requisitos de implantação do produto. |
| `Common`, `Util`, `ComPtr` | Helpers de tipos, console e COM/Win32. | Host; não incluir no kernel freestanding. |
| `Preflight` | Diagnóstico de disponibilidade de virtualização. | Host; preservar leitura, sem alterar segurança/boot do Windows. |
| `MicroVmMonitor` | WHP, RAM, uma vCPU, I/O, registradores e permissões. | VMM host; reutilizar e ampliar nas fases 1–12. |
| `PvConsole`, `PvDisk` | Console por porta e modelo de escrita em arquivo diferencial. | VMM; protocolos legados, sem alegar barramento/armazenamento completos. |
| `MiniNt`, `MiniNtObjects` | Syscalls e objetos atendidos no host. | Referência; adaptar partes e completar semântica no convidado. |
| `GuestHeap`, `GuestVirtualMemory`, `GuestMemory` | Alocação e validação testadas, com metadados/backends host. | Referência para gerência de memória da fase 3. |
| `GuestObjects`, `GuestScheduler`, threads e sincronização | Direitos, referências e escalonamento cooperativo testados. | Referência para fases 4–5; ainda sem preempção/SMP. |
| `NtKernel` | Modelo A sintético; gerência distinta de MiniNt. | Legado de regressão; não misturar seus handles/estado com modelo B. |
| `PeLoader`, `WinLoader`, `ProcessEnv` | Carregamento parcial; dependências de RAM host e patches de runtime. | Referência da fase 8; validar parsers e retirar respostas provisórias. |
| `VirtualFs`, `VirtualRegistry`, registro de MiniNt | Estado em memória, interfaces diferentes entre modelos. | Persistência, segurança e unificação semântica nas fases 7–8. |
| `SnapshotManager` | Cópia de RAM e subconjunto de registradores. | Fase 9; não restaura sozinho kernel, objetos, dispositivos e permissões. |
| `ReportBuilder`, `MicroSandbox` | Relatórios e fachada do modelo A. | Host; integrar telemetria e políticas da fase 13. |
| Exemplos `vm*` e dez testes existentes | Casos específicos de regressão, não aplicações Windows gerais. | Preservados; matriz inicial abaixo. |
| Serviço/IPC, SQLite, conectores, YARA, WinUI 3 | Implementação integrada ainda pendente nesta base. | Fases 14–18. |
| Kernel no convidado, drivers, rede e GUI convidados | Ainda não implementados como sistema operacional próprio. | Fases 1–12. |

Os caminhos `VmSpoof*` e personalidades existentes são exemplos legados; esta
fase não amplia alterações de identidade nem usa seus resultados como medida
de realismo ou de isolamento.

## Fronteiras e alvos de compilação

| Alvo | Conteúdo |
|---|---|
| `guardian_host` | CLI `av` e UI `avui`, ligados às bibliotecas Windows. |
| `guardian_vmm` | Ferramentas WHP `vm*` e `microsandbox`; continuam no host. |
| `guardian_reference` | Sete exemplos de referência e testes, incluindo contratos quando habilitados. |
| `guardian_abi` | Headers de layout e validação sem dependência de APIs Windows. |
| `guardian_guest_contract` | Objeto x64 com os validadores compilados no subconjunto de kernel. |
| `guardian_guest_audit` / `guardian_guest` | Auditoria COFF desse objeto e agrupamento do convidado. |

Os grupos não criam novos processos ou serviços em execução. O objeto convidado
é ligado ao teste host para verificar os mesmos validadores compilados com suas
restrições; esse teste não constitui execução do kernel em uma VM.

Requisitos atuais: CMake 4.0+, Visual Studio 2022 C++ x64, Windows SDK e Python
3.9+ para a auditoria. Não há dependências baixadas pelo configure. C++20 foi
habilitado nos alvos nativos; módulos nomeados C++ não são usados. O alvo
convidado requer MSVC x64; builds host alternativos podem desligá-lo por
`GUARDIAN_BUILD_GUEST_CONTRACT=OFF`, deixando de validar esta fronteira.

Para o convidado: sem RTTI, unwinding C++, bibliotecas padrão automáticas,
instrumentação de runtime ou Just My Code. As opções restritivas se aplicam ao
alvo convidado; host mantém runtime, verificações Debug e proteção de pilha.
O build verifica o COFF real: máquina x64, ausência de símbolos externos não
resolvidos e de diretivas que solicitem bibliotecas ou símbolos externos.
Isso demonstra independência desse objeto; não prova ausência de bugs, segurança
de um futuro kernel ou compatibilidade com drivers Windows.

## Contrato de boot v1.0

Definições executáveis: `shared/guardian/Abi.h` e `Validate.h`.

- Formato inicial da futura imagem: PE32+ x64 freestanding, base preferida
  `0x200000`, sem imports Windows, inicializadores dinâmicos ou runtime externo.
  A fase 1 construirá a imagem e validará seções, entrada e endereços antes de
  carregá-la; essa implementação não faz parte da fase 0.
- Entrada em long mode/ring 0, paginação identity-mapped, interrupções e direction
  flag desabilitadas. A referência atual do VMM mantém OSXSAVE e SCE desabilitados;
  o kernel só mudará esses estados quando implementar seus caminhos de exceção
  e syscall. O contrato inicial não anuncia AVX/XSAVE.
- Convenção x64: RCX aponta para `BootInfo`; pilha de 16 bytes com shadow space e
  alinhamento de entrada correspondente à ABI. A entrada não deve retornar;
  a fase 1 tratará retorno inesperado como falha observável.
- `BootInfo`: 160 bytes, alinhamento 8. Contém versão, máquina, tamanho real da
  RAM, imagem/entrada, pilha, tabelas, canal, uma vCPU e ID de sessão não secreto.
- Todas as áreas declaradas devem caber na RAM, sem overflow ou sobreposição.
  A primeira MiB fica reservada ao bootstrap, incluindo as tabelas existentes
  em `0x70000..0x73FFF`. Dados de boot e demais buffers começam após essa região.
- Mapa de memória: até 128 entradas de 32 bytes, ordenadas e contíguas, cobrindo
  toda a RAM. Classifica reservado, disponível, dados de boot, imagem e pilha.
  A validação confere a propriedade das áreas; não substitui a proteção MMU/WHP.
- Layout de referência dos testes: boot `0x100000`, mapa `0x101000`, dispositivos
  `0x102000`, canal `0x103000`, imagem `0x200000`, pilha `0x400000`.
  Esses endereços são uma configuração de referência, não ponteiros host.

O transporte deve ler/copiar o header e as tabelas somente após validar seus
intervalos na RAM real. Os validadores recebem estruturas já acessíveis ao
chamador; não tornam um ponteiro C++ arbitrário seguro para dereferenciar.

## Dispositivos e mensagens v1.0

Tabela de dispositivos: até oito descritores de 32 bytes. O contrato inicial
exige console de diagnóstico por porta `0xE9` e canal de controle por `0xEA`,
sem interrupção de dispositivo. Seus tipos e versões são explícitos e não
reutilizam implicitamente o protocolo do disco legado `0x70/0x71`.

A implementação futura do canal usa um buffer GPA fixado no boot, de 4096 bytes.
Uma notificação não poderá fazer o host seguir endereço arbitrário fornecido
pelo convidado. O host deverá copiar a mensagem enquanto a vCPU está parada,
validar o envelope e depois interpretar o payload em armazenamento próprio.
O doorbell é uma escrita de oito bits em `0xEA`; o valor do byte não é comando
nem endereço e é ignorado. O tipo da operação vem do envelope no buffer fixo.

Header de mensagem: 40 bytes, magic/versão, operação, tamanhos, sequência e ID de
boot. Flags e campos reservados precisam ser zero. O pacote deve ter tamanho
exato; outra sessão, repetição, versão desconhecida ou payload excessivo são
recusados. O ID de boot e a sequência evitam confusão de sessão; não constituem
autenticação criptográfica de um convidado não confiável.

Operações iniciais somente convidado → host:

| Operação | Payload e uso previsto |
|---|---|
| `Hello` | 16 bytes: arquitetura, vCPUs e capacidades implementadas. |
| `Diagnostic` | 1–1024 bytes de diagnóstico; não executar como comandos ou formato. |
| `StopResult` | 8 bytes: resultado do autoteste e campo reservado zero. |

O estado do transporte da fase 1 deverá exigir Hello inicial, sequência
monotônica e parada terminal, validar os campos de payload e limitar volume de
diagnóstico, tempo e número de saídas. Os validadores desta fase conferem
envelope/layout; não implementam o transporte ou toda essa máquina de estados.
Nenhuma mensagem concede acesso geral a APIs privilegiadas do host.

## Matriz inicial e medidas de aceitação

Configuração versionada: `config/phase0-acceptance.json`. Ambiente de referência
observado: Windows 11 Pro x64 `10.0.26300`, MSVC 19.44, SDK 10.0.26100.0.
Outras builds e Windows 10 ainda precisam de execução na respectiva máquina.
O uso do ntdll local valida um subconjunto dessa referência; não valida perfis
XP–11. ISO Windows não é necessária para construir o kernel próprio.

Referências já existentes: `vmntdll`, `vmntfile`, `vmntsect`, `vmntimage`,
`vmntquery`, `vmntreg`, `vmwininit`; saída esperada zero, timeout de 20 segundos
por execução. Os testes WHP usam programas pequenos gerados no projeto e
watchdog. Uma execução ignorada por falta de WHP não conta como validação real.

Novas referências planejadas: kernel com autoteste (fase 1), cenários de
preempção (4), aplicação console própria (8), consistência de snapshots (9) e
aplicação GUI própria (11). O JSON marca explicitamente que ainda não existem.
Não usar sucesso dos exemplos de syscall como prova de que essas aplicações
futuras já funcionam.

Primeiro gate da fase 1: 20 boots sequenciais por tamanho de RAM de teste
(16/64 MiB), timeout de 3 segundos, Hello e parada explícita, além de entradas
inválidas recusadas. Registrar tempo de parede, saídas de vCPU, RAM, memória
privada do host e resultado. Timeout é limite funcional, não promessa de
desempenho. Os outros workloads têm medidas definidas no JSON; limites finais
de desempenho serão fixados a partir de baselines medidos quando existirem.

Perfis de identidade, recursos, relógio e uso terão configuração centralizada
e versões. Identificadores do host não entram no contrato de boot. O orçamento
de recursos e o anúncio de capacidades precisam refletir implementação real;
essas políticas de admissão serão implementadas nas subfases correspondentes.

## Reproduzir a validação

```powershell
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
# Somente fronteira freestanding e testes novos:
cmake --build build-codex --config Debug --target guardian_guest guest_contract_tests
ctest --test-dir build-codex -C Debug -R '^guest_' --output-on-failure
```

Os três testes novos verificam o contrato usando o objeto freestanding, auditam
seu COFF e exercitam recusas do auditor com objetos truncados ou dependentes de
runtime. Os dez testes anteriores continuam na suíte.

## Resultado e encerramento da fase

**Fase 0 concluída em 16/10/2026. Fase 1 não iniciada.**

| Verificação | Debug | Release |
|---|---:|---:|
| Compilação completa em C++20 | 42 executáveis | 42 executáveis |
| Testes CTest | 13/13 | 13/13 |
| Testes ignorados | 0 | 0 |
| Exemplos de referência | 7/7 | 7/7 |
| CLI sem argumentos: ajuda e saída zero | Passou | Passou |
| Objeto freestanding: símbolos externos não resolvidos | 0 | 0 |
| Objeto freestanding: bibliotecas padrão solicitadas | 0 | 0 |
| Build e auditoria do contrato em diretório novo | Passou | Passou |

A suíte CTest levou aproximadamente 2,85 segundos em Debug e 2,41 em Release
nesta execução. São observações desta máquina, não limites de desempenho do
produto. Os testes WHP executaram de fato.

Os novos alvos de contrato foram compilados com avisos tratados como erros.
O build completo registra avisos C4244 nos alvos legados que usam o modelo A:
conversões de enum para inteiro e de `wchar_t` para `char`. Esses caminhos não
foram reescritos nesta fase; a limitação de conversão de texto permanece
registrada para a evolução da implementação. Não há erros nos builds finais.

Evidências em `build-codex/`:

- `phase0-full-release-script.log` e `phase0-full-debug-script.log`.
- `phase0-release-tests-details.log` e `phase0-debug-tests-details.log`.
- `phase0-regressions.json` e `phase0-host-smoke.json`.
- `phase0-clean-configure.log`, `phase0-clean-debug-build.log` e
  `phase0-clean-release-build.log`.
- `phase0-build-boundaries.json`: opções geradas para host e convidado.
- `phase0-change-manifest.json`: hashes, backup, ambiente e validação.

O diretório independente `build-codex-phase0-clean` foi mantido para inspeção.
Arquivos anteriores desta fase: `snapshots/codex-before-phase0-20261016-153611/`.

Conforme a regra de execução, o trabalho para aqui. A próxima implementação é a
imagem do kernel e seu boot na fase 1, somente após nova solicitação do usuário.

## Referências técnicas

- [ABI x64 — Microsoft](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [Omissão de bibliotecas padrão com /Zl](https://learn.microsoft.com/en-us/cpp/build/reference/zl-omit-default-library-name)
- [Subconjunto de compilação /kernel](https://learn.microsoft.com/en-us/cpp/build/reference/kernel-create-kernel-mode-binary)
- [Propriedade MSVC_RUNTIME_CHECKS — CMake](https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_CHECKS.html)
