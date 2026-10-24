# Fase 14.1 — Contratos versionados e matriz de compatibilidade

Implementação iniciada em 23/10/2026, validação continuada em 24/10/2026.
O escopo desta subfase é definir, persistir e validar a
seleção de contratos, com inventário verificável das dependências. A 14.2 não é
iniciada por esta entrega. Resultados finais de aceitação ficam nos arquivos
`build-phase14.1/acceptance-Release-accepted/validation.json` e
`build-phase14.1/acceptance-Debug/validation.json`; somente `PASS`, sem testes
ignorados, confirma a respectiva execução.

## Implementação

`shared/guardian/NtCompatibility.h` define o contrato próprio versão 1 e três
alvos x64: Windows 10/10.0.19045 (principal), Windows 7/6.1.7601 e
Windows 11/10.0.22631. Eles são alvos de compatibilidade, não sistemas Windows
implementados integralmente. XP/Vista/8/8.1 existentes no inventário de hardware
não são aceitos como contratos de execução desta subfase; x86 depende da 14.12.

O manifesto registra perfil, arquitetura, contrato, SHA-256 de kernel,
aplicativo e runtime, módulos e inventário, política do corpus e pendências de
conformidade por domínio. Revisões e versões dos binários Windows permanecem
ausentes porque o pacote executável desta fase contém apenas o runtime próprio;
não se inventa uma revisão Windows para ele. Importação e validação de DLLs
originais continuam nas subfases seguintes.

`tools/nt_compatibility.py` produz um pacote novo, com `manifest.json`,
`inventory.json` e `binding.gnt` (176 bytes, little endian). Os hashes vinculam
os bytes usados na sessão. O runtime escolhido precisa estar integralmente
embutido no kernel selecionado; especificar outro `user32.dll` não o instala ou
substitui. SHA-256 verifica identidade/integridade, não assinatura ou confiança
no fornecedor. O pacote não inclui binários nem caminhos privados do host.

`KernelBootSession::Prepare` copia o descritor, confere kernel/aplicativo,
versão, perfil e coerência com hardware/ambiente sintético quando presentes.
Publica o contrato em `0x1F1000`, página de boot reservada, somente leitura e sem
execução. O chamador não consegue trocar a seleção alterando o objeto original
depois da preparação. Reutilizar a sessão é recusado; trocar o perfil exige nova
sessão. O contrato não modifica a identidade nem as DLLs do Windows host.

Snapshots do caminho CPU existente preservam o descritor em RAM. A restauração
confere integralmente perfil, contrato, boot, hashes e permissões; recusa outro
manifesto/runtime e a ausência da configuração exigida. Snapshots do novo
contrato PE GUI continuam explicitamente sem suporte, como na fase 12; esta
entrega não declara resolvida a recuperação completa de aplicações externas.

O executável próprio `guardian-gui-pe` aceita `--nt-binding` e `--nt-manifest`
juntos, confere também o SHA-256 do manifesto e grava cópias e resumo na pasta
da sessão. `boot.json` inclui `ntCompatibility`; execuções antigas sem seleção
explícita continuam com `null`. O caminho direto C++ pode receber descritores
de teste sem JSON; ele valida identidade e limites, não faz parsing de manifestos
JSON. Para produzir e auditar o pacote completo, usar a ferramenta Python.

## Consultas de versão e comportamento

O PE próprio consulta o contrato por uma nova operação Guardian, `0xA1`, sem
fingir implementar `RtlGetVersion`, `GetVersionEx` ou a ABI nativa do Windows.
O kernel valida integralmente o ponteiro de usuário, o tamanho e os campos.
Sem manifesto configurado, a operação retorna `Unsupported`. Os testes recebem
os três builds do convidado e observam títulos de janela distintos; o código
do aplicativo continua em CPL3.

O contrato contém um modelo explícito de consulta sensível ao manifesto:
Win7 conserva 6.1.7601; nos alvos Win10/11, o modo sem declaração moderna usa
6.2.9200, a declaração Win8.1 usa 6.3.9600 e a declaração Win10 retorna o alvo
completo. O modo é um parâmetro do teste próprio. Interpretar o manifesto PE e
aplicar esse comportamento às APIs Windows reais pertence à 14.2–14.4.
O comportamento condicionado ao manifesto e os números NT são documentados
pela Microsoft em [GetVersionEx](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversionexa)
e [versões do sistema](https://learn.microsoft.com/en-us/windows/win32/sysinfo/operating-system-version).

Os domínios de disponibilidade de APIs, API sets, syscalls, layouts/classes,
erros, relógios, sincronização, memória, arquivos e gráficos possuem seleção de
política por alvo no manifesto, com estado `pending-native-conformance`.
Seu comportamento completo ainda será implementado nos lotes 14.2–14.12.
Não foram coletadas referências nativas dos builds exatos 7601, 19045 e 22631;
o Windows local 26300 não substitui essas referências. Não existem tabelas de
syscalls copiadas do host nem uma alegação de conformidade nativa aprovada.

## Inventário e prioridade

O inventário reaproveita o parser PE da fase 12 e acrescenta imports estáticos
por nome/ordinal, delay imports com RVA, tabelas de exports e ordinais, exports
de dados candidatos e forwarders. Resolve dependências transitivas dos módulos
fornecidos, identifica ciclos, produz ordem por dependência e prioridade por
família/subfase. API sets ficam pendentes de um schema validado por versão;
[o contrato não é necessariamente o nome de um arquivo DLL](https://learn.microsoft.com/en-us/windows/win32/apiindex/api-set-loader-operation).

Imports PE não informam se o símbolo será usado como função ou dado. A
classificação de exports usa permissões de seção e é marcada como candidata.
Imports dinâmicos não podem ser todos inferidos estaticamente: observações
explícitas opcionais contêm `source`, `module`, `symbol` e `evidence`, sem
declarar cobertura completa de GetProcAddress. Um export presente tem
comportamento **não validado**, mesmo quando seu nome resolve.

O inventário Windows desta entrega cobre 12 módulos próprios das fases 8/12,
com 155 imports resolvidos estaticamente. O Flappy fornecido pelo usuário tem
178 imports, dos quais 162 continuam sem resolução no runtime GUI atual. Ele foi
somente inspecionado; não foi executado nem promovido a aplicativo compatível.

Os limites são 128 módulos, 32 MiB por arquivo, 128 MiB agregados e 16.384
imports/exports; forwarders possuem limite de profundidade. Arquivos inválidos,
arquitetura diferente, sobreposições, nomes duplicados, links/reparse points e
combinações de perfil inconsistentes são recusados. Não há LoadLibrary ou
execução dos binários inspecionados no host. Delay imports em formato VA, extração
de recursos de versão e resolução de API sets permanecem explicitamente fora do
inventário implementado, com diagnóstico em vez de resultados fabricados.

`config/nt-compatibility/corpus-policy.json` fixa antes da campanha: no mínimo 32
programas independentes, oito categorias com quatro programas cada, pelo menos
oito programas por arquitetura x86/x64, alvo de 75% funcionais por perfil e
50% por categoria. Fixtures próprias não contam. Versões, hashes e roteiros
serão congelados antes da campanha; falhas não autorizam trocar programas para
elevar o resultado. A seleção ainda está incompleta. Essa política mede o corpus
declarado, não todo o ecossistema nem detecção de malware.

## Uso

Executar na raiz do projeto, usando o build desta fase:

```powershell
$b = 'C:\Users\gabri\.codex\tmp\aegis-phase14-1-20261023'
python tools/nt_compatibility.py create --profile win10 `
  --kernel "$b\guest\Release\guardian-kernel-pe-gui.gk" `
  --application "$b\guest\fixtures\Release\guardian-gui-application.exe" `
  --runtime "$b\guest\fixtures\Release\user32.dll" --output .\perfil-nt-novo
python tools/nt_compatibility.py verify .\perfil-nt-novo
& "$b\Release\guardian-gui-pe.exe" "$b\guest\Release\guardian-kernel-pe-gui.gk" `
  "$b\guest\fixtures\Release\guardian-gui-application.exe" .\sessao-nova `
  --nt-binding .\perfil-nt-novo\binding.gnt `
  --nt-manifest .\perfil-nt-novo\manifest.json --interactive
```

Na referência própria, digitar `G` maiúsculo, clicar na área da janela e fechar
em até 30 segundos. Usar pastas novas. O PE próprio contém chamadas do convidado:
não iniciá-lo diretamente no Windows. Para inventário estático de outro EXE,
usar `inventory`, os mesmos parâmetros `--application`/`--runtime`, módulos
adicionais em `--module` e `--output arquivo-novo.json`.

## Validação e continuidade

Os testes novos cobrem SHA-256 com vetor conhecido, políticas por perfil,
recusa de versão/revisão/contrato/hashes incoerentes, parser sintético com
ordinais/dados/forwarders/delay imports, observações dinâmicas, ciclos, persistência
canônica, adulteração, execução GUI convidada nos três perfis, restauração CPU e
recusa de snapshots incompatíveis. A integração da CLI executa somente o PE
próprio e verifica a recusa de entradas adulteradas antes de criar a sessão.

O validador `tools/validate_phase14_1.py` exige os quatro testes novos e os três
testes GUI anteriores, sem falhas/ignorados, e registra hashes de fontes e
binários antes/depois. A matriz Release inclui regressões adicionais relevantes;
a Debug cobre o contrato novo e a referência GUI. A aceitação não equivale a
novos resultados do scanner completo nem a uma nova aceitação Linux/KVM.

Na primeira matriz Release, os quatro testes novos e as regressões de GUI,
snapshot e análise passaram, mas runtime/lifetime antigos excederam o prazo
funcional de 10 s. Os relatórios preservados registram 12,7–15,7 s em estágios
distintos de I/O; lifetime passou na repetição sem alterar os binários, enquanto
runtime voltou a atingir o prazo em outro boot. Isso indica sensibilidade à
latência do host, sem comprovar sua causa exata. Os testes funcionais Windows de
PE com persistência passam a ter orçamento de 30 s por boot. O watchdog do
produto e os prazos dos testes específicos de CPU/tempo não foram alterados.
Falhas iniciais permanecem em `acceptance-Release` e
`first-regression-failures`, com a repetição em `retest-runtime-lifetime.*`;
a aceitação posterior deve ser identificada separadamente. A segunda matriz
aprovou CPU/watchdog com seus prazos originais e os três lotes de runtime, mas
encontrou a mesma condição no lote básico PE, ainda limitado a 3 s apesar de
usar persistência. O orçamento funcional Windows de 30 s se aplica, portanto,
aos quatro lotes PE; o resultado dessa segunda tentativa fica preservado em
`acceptance-Release-final`.

A cópia anterior dos arquivos modificados está em
`build-phase14.1/baseline-20261023-224422`. As mudanças locais anteriores da fase
12/12F foram preservadas. Ao concluir esta subfase, parar; a 14.2 exige novo pedido.
