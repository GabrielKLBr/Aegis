# Fase 13 — telemetria, políticas e análise comportamental

**Atualização Windows de 23/10/2026:** a matriz atual passou em MSVC/WHP: 80 testes distintos Debug, com retestes documentados, e 80/80 Release em uma suíte completa, sem ignorados. [Correções e evidências](REVISAO-WINDOWS-FASES12B-13-20261023.md). Os registros Linux abaixo são históricos; as pendências de escopo e de comparação entre versões continuam abertas.

**Estado em 22/10/2026:** implementação concluída e aceita na matriz Linux/KVM
nativa: **59 Debug, 59 Release e 35 ASan/UBSan**, sem falhas ou ignorados.
O produto permanece Windows/MSVC/WHP; não foi usado Wine ou QEMU.
A aceitação nativa Windows desta revisão e as pendências anteriores continuam
abertas. O resultado mantém `phaseComplete: false` e `prerequisitesComplete: false`
para esses requisitos. A fase 14 não foi iniciada.

A base anterior está preservada em `build-phase13/baseline-20261022/`, com
arquivo de fontes, inventário SHA-256 e verificação dos bytes. Código e registros
ficam na pasta original do HD; builds ficam fora do NTFS.

O escopo é registrar operações reais e tentativas bloqueadas do convidado,
com autoria e motivo, correlação versionada, política permitir/negar/simular,
relatórios JSON/HTML e diferenças de estado. Limites e perda de eventos devem
ficar explícitos. Eventos recebidos da VM serão validados como dados não confiáveis.

## Implementação e fronteira

O protocolo `GuardianTelemetry/1` transporta eventos limitados, com versão,
boot, sequência, operação e correlação entre intenção e resultado. Há PID e
thread do processo observado, além do processo/thread autor da chamada. Isso
permite distinguir o encerramento de um filho solicitado pelo processo pai.
Os registros incluem decisão, motivo, status e, para o volume, tamanhos e
hashes FNV-64 anteriores/posteriores; não copiam o conteúdo dos arquivos.

O host valida envelopes, enums, limites, texto ASCII terminado, sequência e
identidade da operação pendente. Textos não são comandos, caminhos recebidos
da VM não são abertos no host e o HTML escapa seus dados, sem JavaScript ou
recursos externos. Eventos são declarações recebidas do kernel convidado;
a validação de formato não prova sua veracidade se esse kernel for comprometido.
Programas em CPL3 continuam sujeitos às permissões e à validação das syscalls.

O novo kernel reúne na mesma linha do tempo os nove domínios: processos,
threads, módulos, arquivos, registro, serviços, drivers, rede e memória. O eco
UDP próprio é solicitado pela aplicação via syscall, com PID/thread reais,
e usa o dispositivo de IP virtual. Ele funciona offline (recusa) ou no modo
simulado (eco interno verificado); não recebe endereços arbitrários e não abre
conexões externas. Drivers de I/O reais do convidado aparecem no mesmo fluxo.
O driver PE importado mantém seu contrato de boot separado e também foi ligado
ao protocolo de coleta, sem declarar que execute dentro do perfil de processos.

A coleta da aplicação começa após a preparação da base sintética e os testes
internos iniciais. O preparador repõe somente seus três documentos próprios
e remove suas versões `.locked` anteriores; isso é uma base de teste explícita,
sem importar dados do host. A cobertura completa indica as operações
instrumentadas desse intervalo, sem afirmar cobertura de toda instrução ou
API do sistema.

O novo kernel `guardian-kernel-analysis.gk` executa uma aplicação própria em
CPL3, com arquivos sintéticos de referência, e verifica a limpeza real dos
recursos ao terminar. Ele tem critérios próprios de conclusão; não preenche
os resultados dos antigos probes de CPL3. Esses probes continuam nos kernels
`complete` e `processes`, validados pela regressão completa. O contador de
processos, threads, handles e objetos deve voltar a zero, sem perda de páginas.

O serviço `GuardianLabService/1` é um filho real, com estado, autoria, consulta,
início e parada; é local à vida do processo criador e seu executável pertence
ao catálogo próprio. Não representa o SCM do Windows nem promete executar
serviços de terceiros. A escrita de configuração `HKCU\Lab\Run` é observada
como configuração de inicialização; não se infere que um autorun Windows
inexistente tenha sido executado.

## Política e regras

A política v1 se aplica às operações que alteram estado. `deny` impede a operação
antes do efeito e devolve erro; leituras e limpeza de recursos permanecem
possíveis. `simulate` de arquivos/registro executa os efeitos no volume sintético
privado da VM: leitura posterior, datas, permissões, clone e snapshot refletem
o estado produzido. Ela não descarta alterações automaticamente; usar VM
`--disposable` para esse ciclo. Simulação em outros domínios é recusada com
motivo explícito. Não há operação nova que execute comandos ou efeitos no host.

As regras v1 registram evidências numeradas e motivos:

- Configuração de inicialização escrita: suspeita; não prova execução posterior.
- Serviço próprio criado e iniciado: comportamento confirmado, ligado ao mesmo
  serviço/processo; isso não identifica malware.
- Três arquivos distintos reescritos e renomeados para `.locked`, pelo mesmo
  processo em até dez segundos: padrão confirmado. Repetir um só caminho,
  misturar processos ou exceder a janela não satisfaz a regra.
- Solicitação de memória gravável/executável: suspeita, com resultado real.
  O runtime atual recusa essa proteção; a recusa não é descrita como sucesso.
- Bloqueio pela política: tentativa suspeita, preservando o motivo e o erro.
- Coleta completa sem padrão: legítimo no subconjunto observado, sem atestar
  segurança geral. Falha, coleta ainda em curso, entrada rejeitada ou perda de
  eventos: cobertura inconclusiva.

O limite padrão é 4.096 eventos, configurável entre 16 e 8.192. Ao atingir o
limite, a perda fica contada e novas mutações são recusadas; o relatório fica
inconclusivo. O snapshot conserva registros, sequência, operações pendentes e
política. Restaurar com política/identidade incompatível é recusado. O limite
reservado a metadados de snapshots passa de 1 para 5 MiB para comportar a coleta
máxima, preservando os demais limites. Há até 64 boots de histórico por VM,
sob a cota de armazenamento existente; falhas de gravação tornam a VM falha.

## Uso

```text
guardian-vm --root CATALOGO create amostra guardian-kernel-analysis.gk storage
guardian-vm --root CATALOGO analysis-policy amostra
guardian-vm --root CATALOGO network amostra simulated
guardian-vm --root CATALOGO analysis-policy amostra file deny
guardian-vm --root CATALOGO start amostra
guardian-vm --root CATALOGO status amostra
guardian-vm --root CATALOGO analysis amostra
guardian-vm --root CATALOGO analysis-export amostra DIRETORIO
```

Trocar `deny` por `allow` ou `simulate`. A política só muda com a VM parada;
checkpoints antigos preservam sua política e não podem ser retomados sob uma
configuração diferente. Para a aplicação própria de referência, acrescentar
`--scenario benign` ou `--scenario patterns` à configuração. `--event-limit N`
permite testar a saturação. Os relatórios são JSON, HTML e diferenças de estado,
com histórico em `analysis-history/` dentro da VM.

## Validação e entrega

A única aceitação desta revisão é `acceptance-20261022-full-03`:

| Configuração | CTests aprovados | Falhas / ignorados |
|---|---:|---:|
| Debug | 59 | 0 / 0 |
| Release | 59 | 0 / 0 |
| ASan/UBSan | 35 | 0 / 0 |
| Total | 153 | 0 / 0 |

O núcleo passou em 1233 verificações Debug, 1233 Release e
1233 com sanitizers. Inclui entradas não confiáveis, HTML escapado,
perda/capacidade, correlação negativa por arquivo/processo/tempo, restauração
com identidade e política, 8.192 eventos serializados, política no volume real
e metadados de valores de registro com nomes em maiúsculas/minúsculas.

Os boots próprios passaram em 864 verificações Debug e 864
Release. Exercitam permitir, negar, simular, uso legítimo e saturação; comparam
o conteúdo real dos arquivos, retomam snapshot, recusam política incompatível,
conferem os nove domínios na mesma coleta, rede simulada e driver PE no contrato
separado. Kernels básicos sem coletor recusam a configuração de política.

O gerenciador/oracle independente passou em 1038 verificações Debug
e 1041 Release, incluindo VMs reais, correlação a partir dos eventos,
exportação, persistência dos relatórios, clone, rollback, perda visível e
arquivo pessoal de controle intacto. As contagens incluem consultas de progresso
e podem variar com a duração das VMs. O canário pessoal ficou fora da RAM e
dos dados convidados nos canais exercitados; não se trata de prova geral de
que qualquer implementação possível esteja livre de todo vazamento.

Os 418 arquivos de código permaneceram iguais durante a matriz. O SHA-256 de
`source-inputs.json` é `fd9432b0d4413788289b53ea05a5060ac13bd77de2b8710e2465c9e19203af60`.
A checagem cruzada final de sintaxe passou em 14 unidades Windows sobre o mesmo
código. Ela não executa `.exe`, não usa MSVC/WHP e não substitui a aceitação
nativa Windows.

Os testes antigos de snapshot, gerenciador, conformidade e sessões continuam
na matriz; seus critérios e timeouts foram preservados. A repetição dirigida
das cinco regressões foi aprovada antes da execução completa final.

Na pasta original do HD:

- Evidências selecionadas: `build-phase13/acceptance-20261022-full-03/`.
- Revisão e checagem Windows: `build-phase13/review/`.
- Fontes: `build-phase13/delivery-20261022/GuardianAV-13-fontes-20261022.zip`.
- Linux: `build-phase13/delivery-20261022/GuardianAV-13-linux-20261022.tar.gz`,
  com 12 ferramentas ELF, 21 kernels, quatro programas próprios como dados e
  exemplo de relatório JSON/HTML da execução Release aceita.
- Integridade e teste da entrega: `SHA256SUMS.txt`, `delivery.json` e
  `package-verification.json` no diretório de entrega. A checagem adicional
  extrai o pacote, executa o núcleo e a VM entregue, verifica a simulação,
  os nove domínios e a exportação real.

## Correções encontradas na validação

A primeira matriz foi substituída durante Debug, antes da aceitação: um
marcador anunciava coleta em kernels que não iniciavam o coletor. O marcador
passou a existir apenas nos caminhos que o inicializam. Os resultados parciais
não foram contados como aceitação.

A matriz `full-02` terminou com cinco regressões Debug que tinham a mesma
causa: a auditoria de texto inválido antecipava acesso negado, alterando o erro
original de uma criação de processo com comando sem terminador. O coletor
passou a marcar o rótulo inválido e preservar a validação da API; somente uma
política configurada pode negar antecipadamente. O caso também roda em CPL3
na aplicação própria. A execução falha foi preservada e não aceita esta revisão.

O teste dirigido também atingiu o limite antigo de 10.000 saídas do hipervisor.
Perfis com coletor passam a 100.000 por trecho normal, mantendo o watchdog de
tempo e o limite de eventos; os demais conservam o limite anterior. Não houve
redução dos testes ou ampliação de seus timeouts para obter a aprovação.

Também foram verificados os hashes e tamanhos do registro usando os mesmos
critérios de normalização de nomes do volume, preservando a coerência entre
o evento e o estado real.

## Reprodução

```text
python tools/validate_phase13.py --output DIRETORIO_NOVO --llvm LLVM_NATIVO --openssl-root OPENSSL_NATIVO
```

No Windows, executar em um terminal x64 do Visual Studio, sem os caminhos Linux.
O validador conserva os testes e as pendências anteriores. Políticas que impedem
o carregamento ou operações necessárias ao programa podem impedir sua conclusão;
essa falha e a cobertura inconclusiva ficam explícitas.

Durante a execução, a cobertura permanece em curso/inconclusiva. Para o resultado
final, aguardar a VM parada e `finished: true`; `complete: false` continua
indicando perda, rejeição ou falha, mesmo após o fim.

Esta entrega encerra a implementação e a aceitação Linux da fase 13, mantendo
os requisitos Windows e anteriores abertos. A fase 14 depende de nova solicitação.
