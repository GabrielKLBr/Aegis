# Fase 14.2 — Carregador PE externo e volume de runtime

Implementação Windows/MSVC/WHP de 24/10/2026. Esta entrega amplia o carregador
existente; não inicia a 14.3. O contrato executável continua sendo Guardian,
com DLLs de teste próprias. Compatibilidade ampla com binários Windows e a
execução do Flappy não estão declaradas.

## Implementação e limites

O novo `guardian-kernel-external-pe.gk` recebe um volume externo somente leitura
(`runtime.gpv`). EXE, DLLs e manifests não precisam ser incorporados ao kernel.
`guardian-pe` confere SHA-256 de kernel, EXE, volume e manifesto contra o vínculo
de 176 bytes da 14.1 antes de preparar a execução. O volume e seus metadados
ficam reservados no mapa de memória e protegidos contra escrita/execução. O
parsing, as relocations, os imports e a execução do código acontecem no convidado
em CPL3; não há execução do arquivo fornecido como processo do host.

`GUARDIAN_SCALABLE_PE` seleciona estes limites no novo kernel, nos testes e na
DLL de runtime correspondente. Os kernels anteriores conservam seus limites e
o tamanho de seus comandos. Não misturar DLLs Guardian de contratos diferentes.

| Recurso | Contrato externo 14.2 |
|---|---|
| Arquivo PE e imagem mapeada | Até 8 MiB cada |
| Catálogo de arquivos | 16 entradas, incluindo manifests |
| Módulos PE | Até 16, sujeitos à memória disponível |
| Volume de entrada | Até 64 MiB |
| Páginas privadas por processo | Até 16.384 páginas / 64 MiB |
| Arena virtual / regiões | 256 MiB / 256 regiões |
| Seções por imagem | 32 |
| Relocations / exports / funções de unwind | 8.192 / 8.192 / 4.096 |
| TLS | Até 4 KiB por módulo, TEB de 128 KiB por thread |
| Callbacks de inicialização | Até 144 por lote |
| Alias de API / identidades de assembly | 16 / 8 |
| Contextos de ativação / profundidade por thread | 8 / 8 |
| Manifest / árvore de recursos | 4 KiB / orçamento de 256 diretórios |
| Referências de recursos | 32 por processo |

Os limites são verificações de recusa, não promessa de que todos os máximos
cabem simultaneamente. A aplicação de referência tem um payload de 2 MiB,
imagem maior que 128 KiB, mais de quatro módulos e TLS de 2 KiB. A CLI reserva
256 MiB de RAM e mantém o limite de execução de 30 segundos do contrato de boot.

## Busca, imports e ciclo de vida

O catálogo aceita somente caminhos canônicos sob `C:\App`,
`C:\Windows\System32` e `C:\Windows\WinSxS`. Recusa caminhos do host, travessia,
streams alternativos, entradas duplicadas e regiões sobrepostas. A ordem do
contrato é: caminho convidado explícito; alias de API registrado; diretório de
assembly ativo; diretório do EXE; System32. `guardian-runtime-api.dll` e
`user32.dll` são reservados ao System32 convidado. Não há busca por PATH, pasta
atual ou diretórios do Windows host. A busca ignora maiúsculas/minúsculas e
acrescenta `.dll` quando o nome solicitado não tem extensão.

O mapa de API sets é explícito no pacote de cada perfil NT e vinculado pelo
SHA-256. Não é um parser dos schemas nativos ApiSetNamespace v2/v6. Os três
perfis da 14.1 continuam disponíveis; alterar o perfil exige outro pacote e
vínculo. DLLs e contratos novos são responsabilidade das próximas subfases.

Há imports por nome e ordinal, exports de dados, forwarders por nome/ordinal,
recursos e delay imports. Os delay imports RVA são resolvidos antecipadamente
antes de selar as seções; não há promessa de lazy binding, hooks de delay-load
ou recuperação do IAT por `__FUnloadDelayLoadedDLL2`. Forwarders não usados não
carregam seu destino; `GetProcAddress` pode carregar e inicializar dependências
novas. Ciclos de resolução e grafos de DLLs cíclicos são recusados.

`LoadLibraryA`, `FreeLibrary` e `GetProcAddress` usam o loader lock existente.
Dependências iniciam antes dos dependentes; callbacks de TLS antecedem o
DllMain correspondente. A liberação percorre a ordem inversa, respeita
referências e mantém dependências de módulos ainda alcançáveis. Falhas no
DllMain e no backend desfazem mapeamentos, referências e TLS. A referência
Windows confirma também o PROCESS_DETACH do DllMain que recusou PROCESS_ATTACH.
As seções são seladas como leitura, leitura/escrita ou leitura/execução; W+X
é recusado. Gerações de módulos impedem reutilização de handles de recursos
depois de descarregar e recarregar a mesma base.

## Recursos e manifests

O parser valida a árvore PE de recursos em três níveis e oferece consulta por
IDs numéricos. O runtime próprio fornece `FindResourceA/ExA`, `LoadResource`,
`LockResource` e `SizeofResource`. Nomes Unicode, MUI e a política completa de
fallback por idioma não estão implementados; idioma omitido escolhe a primeira
entrada numérica disponível. O diretório de certificados é tratado como
metadado delimitado do arquivo, sem validar Authenticode.

Manifests embutidos do EXE e manifests externos registrados no volume aceitam
um subconjunto XML ASCII/UTF-8 de assemblyIdentity, dependency/dependentAssembly
e compatibility/application/supportedOS. DTD, entidades, tags desconhecidas e
atributos duplicados são recusados. A seleção de assembly exige nome, versão
de quatro componentes, amd64 e win32. Contextos de ativação mantêm pilhas por
thread, cookies LIFO e referências até a última desativação. A consulta própria
de versão pode derivar o modo do supportedOS do EXE; isso não implementa
GetVersionEx nem uma PEB/TEB nativa completa.

Não há resolução completa de políticas WinSxS, assinaturas, publisher policy,
COM/classes/janelas ou UAC/trustInfo. Esses requisitos são recusados, sem simular
sucesso. O volume é um formato imutável Guardian de entrada, não NTFS nem a
integração de arquivos mutáveis do perfil `resources`. ARM, x86/WoW64, managed
PE, DLLs sem relocations quando deslocadas e diretórios PE ainda não atendidos
são recusados. Criar processos filhos e restaurar snapshots neste novo caminho
permanecem sem suporte explícito; threads próprias foram exercitadas. A GUI
anterior permanece coberta pelos testes da fase 12, sem avanço da 12F.

## Reprodução e evidências

Configurar no Windows com `BUILD_TESTING=ON`, `GUARDIAN_GUI_COMPATIBILITY_ONLY=ON`,
`GUARDIAN_BUILD_PE_GUI=ON` e `GUARDIAN_BUILD_EXTERNAL_PE=ON`, usando Visual Studio
2022 x64. Compilar `guardian-pe`, `guardian_external_application`,
`guardian_external_widget2`, `external_pe_loader_tests`,
`external_pe_native_tests` e os alvos de regressão desejados.

`python tools/pe_volume.py sample --build BUILD --config Release --kernel KERNEL --output NOVO_PACOTE --profile win10`
produz a referência. `create --spec SPEC.json --kernel KERNEL --output NOVO_PACOTE`
aceita o mesmo formato de especificação usado por `sample`. `verify PACOTE`
confere a estrutura, hashes por arquivo, manifesto e vínculo.

`guardian-pe KERNEL PACOTE/runtime.gpv PACOTE/manifest.json PACOTE/binding.gnt NOVA_SESSAO`
executa e grava `boot.json`, a cópia do manifesto e os relatórios gráficos.
O inventário estático dos PEs é salvo em `inventory.json`; não afirma execução
nem cobertura de imports dinâmicos não observados.

`tools/validate_phase14_2.py --build BUILD --config Release --output NOVA_ACEITACAO`
executa 15 testes obrigatórios, recusa qualquer skip e fixa SHA-256 dos fontes
e binários antes/depois. Repetir com Debug. Os testes incluem comparação no
Windows real somente das DLLs próprias, execução do PE externo nos três perfis,
TLS de worker, ordinal/dados/forwarders, inicialização tardia, DllMain recusado,
delay imports, recursos, seleção de duas versões de assembly, cookies inválidos,
recusa de pacote adulterado antes de criar a sessão e injeção de falhas no
carregador. Um erro ou limite não pode deixar módulos ou processos vivos.

Evidências locais: `build-phase14.2/acceptance-Release/validation.json`,
`build-phase14.2/acceptance-Debug/validation.json` e
`build-phase14.2/acceptance-Release.xml` para a regressão completa. Apenas arquivos
com `status: PASS`, fontes/binários estáveis e nenhum teste ignorado confirmam
a respectiva aceitação. Builds ficam fora do repositório público.

Referências: [formato PE](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format),
[busca de DLL](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order),
[DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain) e
[contextos de ativação](https://learn.microsoft.com/en-us/windows/win32/sbscs/activation-contexts).
