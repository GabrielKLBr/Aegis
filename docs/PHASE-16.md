# Fase 16 — Serviço de proteção, IPC e tempo real

A proteção passa a trabalhar no serviço Windows, com fila e política persistentes,
monitoramento após alterações de arquivos, agendamento, notificações e cofre
protegido. O trabalho continua sem o aplicativo aberto. Esta entrega não altera a
compatibilidade do MiniNT nem repete as campanhas 12F–14.x.

## Uso no aplicativo

Abra **Proteção** para consultar o estado real, iniciar varredura rápida, completa
ou de uma pasta, acompanhar a fila e cancelar uma varredura manual. **Proteger
pasta** adiciona a pasta escolhida, ativa o monitoramento e o isolamento das
**detecções confirmadas**. Pausar/retomar, proteger pastas, desligar o serviço do
antivírus, restaurar conteúdo e excluir do cofre exigem um cliente com autorização
administrativa. O aplicativo não inicia uma elevação automaticamente nessas ações;
a recusa do serviço aparece na interface. O serviço MiniNT mantém seu controle
separado. A configuração inicial não seleciona pastas nem habilita isolamento
sobre arquivos pessoais sem uma escolha explícita.

A página mostra quando a proteção está pausada, incompleta, sem sessão do usuário
ou com notificações de falha. A interface anterior de varredura continua disponível
para sessões interativas; a fila persistente e o monitor são acessados pela página
Proteção. A migração completa de interface, acessibilidade e idiomas permanece na
fase 17.

## Supervisão e autorização

O supervisor roda como LocalSystem, com lista de privilégios necessários ao IPC,
criação de processos e obtenção do token da sessão do proprietário da instalação.
Não conserva SeDebug/SeBackup/SeRestore para inspecionar arquivos arbitrariamente.
Os parsers executam com token restrito do usuário, integridade média, limites de
memória/processos e job encerrado com o supervisor. Os objetos de processo/thread
dos workers pertencem ao SYSTEM e não concedem escrita/terminação à conta comum.

O endpoint rejeita clientes remotos, processos de baixa integridade e usuários que
não sejam o proprietário instalado. O cliente verifica o PID publicado pelo SCM;
o supervisor verifica o PID do worker e usa um pipe aleatório por execução.
Operações internas 25/26 não podem ser invocadas no endpoint público. Há quatro
conexões frontais limitadas. A capacidade de execução automática é separada da
manual: uma varredura manual longa não ocupa o worker reservado ao monitor.

A autorização administrativa é conferida no token autenticado **antes** da sua
redução. O conteúdo de um JSON não concede privilégios. Configurar/excluir/restaurar
e cancelar tarefas automáticas exigem administrador; consultar estado, ler avisos,
solicitar varreduras e cancelar as próprias tarefas manuais não exigem elevação.
Arquivos de pedido são lidos com a identidade restrita do cliente, com limite de
64 KiB. O supervisor abre origens e destinos usando os direitos dessa identidade,
nunca aproveitando os direitos SYSTEM sobre caminhos enviados pelo cliente.

A instalação protege proprietário e DACL dos binários/slots, inclusive contra
mudança de DACL pela conta comum que instalou o programa. O antivírus recebe início
automático atrasado e recuperação pelo SCM após falha. A conta sem elevação pode
consultar/iniciar o antivírus, mas não pará-lo ou mudar sua configuração. Os slots
anteriores são conservados para recuperação. A atualização não desativa o Defender
nem instala um driver.

## Cofre protegido

O estado do serviço reside no ProgramData conhecido pelo Windows, em
`AegisProtection16`. Estado, política, fila e chaves não são gravados na pasta do
aplicativo ou em um diretório controlado pelo cliente. Diretórios são protegidos e
recebem proprietário SYSTEM. Diretórios/estado preexistentes pertencentes a uma
conta comum, links/reparse points e hardlinks de estado são recusados.

O cofre conserva AES-256-GCM, verificação dos bytes/SHA-256 antes de remover o
original, exclusão pelo handle e restauração sem substituir arquivos. Sua chave
agora usa a DPAPI do SYSTEM. Origem e temporário de restauração são abertos com o
token restrito do usuário; o banco e a chave são acessados somente como SYSTEM.
O proprietário original e suas ACEs continuam preservados. A cópia cifrada
permanece até exclusão explícita autorizada. Uma restauração para um diretório
administrativo não aproveita a elevação do cliente: os direitos restritos precisam
permitir a escrita.

A GUI e `av clean`/listagem usam esse broker. `guardian-quarantine.exe` permanece
uma ferramenta de diagnóstico de cofre por usuário; não é um atalho de acesso à
chave SYSTEM. Cofres anteriores da fase 15 não são removidos nem convertidos
silenciosamente. Pode-se recuperá-los com a ferramenta da fase 15 e depois isolar
explicitamente no cofre novo. Migração, exportação e recuperação de credenciais
fazem parte do ciclo de atualização da fase 18.

Isso protege o armazenamento e as operações administrativas contra um cliente
comum da mesma conta. Não é PPL, proteção de kernel ou resistência a administrador,
SYSTEM, credenciais/canais administrativos já autorizados ou controle do Windows.
Não impede adulteração de arquivos de origem que o próprio usuário pode escrever.

## Monitor, fila, cobertura e agendamento

O monitor usa ReadDirectoryChangesW assíncrono, observa criação, modificação,
renomeação e diretórios e consolida alterações por aproximadamente um segundo.
Os buffers têm 64 KiB. Notificações vazias, malformadas ou filas excedidas geram
**coverage-gap** e uma varredura de recuperação; nunca são interpretadas como
ambiente limpo. O monitor é armado antes da varredura inicial. Reiniciar o serviço
arma o monitor de novo e solicita nova verificação do escopo configurado. A identidade da pasta é conferida periodicamente; mover ou substituir
a raiz observada provoca reabertura e varredura de recuperação.

SQLite mantém política, tarefas e eventos com journal DELETE e synchronous FULL.
Tarefas interrompidas em execução voltam à fila com marca de recuperação, sem
prometer continuidade de um snapshot de filesystem: a enumeração é refeita.
Configuração e cancelamento de tarefas automáticas antigas mudam na mesma
transação. Marcador/estado ausente ou vazio e configurações ausentes não provocam
reinicialização silenciosa de um banco já estabelecido.

A fila admite até 128 tarefas pendentes, das quais no máximo 16 manuais. O histórico
mantém até cerca de 512 tarefas terminadas; o estado retorna as 64 mais recentes.
Há consolidação de pedidos automáticos ainda na fila, sem tratar uma varredura já
em andamento como cobertura de uma lacuna posterior. A fila de alterações
consolidadas é limitada a 1024 caminhos. Limites/falhas ficam visíveis e geram
recuperação de escopo. Excesso de carga pode atrasar a análise; não há garantia de
latência contra um produtor ilimitado de alterações.

A varredura rápida usa quickRoots, com fallback para roots e pastas conhecidas do
usuário. A completa planeja volumes locais fixos, usando somente arquivos legíveis
pelo token do usuário. A personalizada usa até 16 caminhos. ACLs negadas, quota,
reparse points e limites de tempo são diferenciados de uma análise concluída.
Exclusões são caminhos/descendentes normalizados, comparados sem depender da
localidade linguística; não são padrões glob ou extensões.

Os parsers protegidos ignoram configuração/cache de regras controlados pelo
usuário: usam regras incorporadas e assinaturas do slot instalado, offline e sem
executar o arquivo analisado. O marcador sintético de teste é aceito como pequeno
arquivo sentinela, não como uma assinatura de código-fonte que apenas cite o
texto do marcador. A validação dessas regras não certifica eficácia antimalware;
a campanha de eficácia/falsos positivos permanece na fase 19.

Agendamentos são periódicos, em segundos UTC, entre 60 segundos e sete dias, até
16 entradas. O avanço de nextRun e a inclusão do trabalho são transacionais.
Após indisponibilidade, executa-se uma ocorrência pendente, sem criar uma tempestade
de ocorrências antigas. A sessão do proprietário é obtida pelo Windows, sem UI
aberta; sem sessão utilizável, as tarefas aguardam e o estado informa isso.

Notificações persistem em até 4096 eventos, com cursor e indicação de lacuna por
retenção. Detecções/lacunas/falhas também tentam publicar um evento Application do
Windows, registrando o sucesso dessa publicação. O evento nativo usa o nome do
arquivo; o caminho completo continua na consulta autenticada do proprietário.
A aplicação consulta esses avisos e o estado real, sem estados positivos inventados.

## Limites e escopo da validação

O monitor é **posterior à alteração**. Não há minifilter nem bloqueio preventivo de
execução: um processo pode iniciar antes da análise. Um driver só deve ser adicionado
quando houver necessidade e implantação/assinatura suportadas. O parser está limitado
a 32 MiB por arquivo e ao orçamento de inspeção; o cofre conserva seu limite de
64 MiB. A política admite até 100000 arquivos e 86400 segundos por varredura; o
padrão é 10000 arquivos/1800 segundos. Atingir limite deixa resultado incompleto.
A base de estado usa no máximo 32768 páginas SQLite; falta de armazenamento é erro,
não autorização para descartar dados. Permanecem os limites de caminhos, DACL e
formatos de origem documentados na fase 15.

Os testes usam arquivos novos e inertes. Validam permissões administrativas reais,
IPC, arquivo administrativo ilegível para o cliente, isolamento/restauração pelo
broker, cópia retida, conflitos, exclusões, notificações, mudanças de nome, varreduras,
concorrência, agenda e recuperação de trabalho após terminar o processo do serviço.
A integração é executada com os serviços instalados em Release e Debug. As configurações Release e Debug passaram **58/58** casos de integração cada;
há mais **25/25** casos nativos por configuração. [Resultados por caso](PHASE-16-RESULTS.json).
Os casos nativos para decoder de notificações e separação do protocolo. Buffers
malformados/overflow são vetores sintéticos; o encerramento do serviço e as mudanças
NTFS são reais. Não se realizou reboot/logoff do PC, corte físico de energia,
certificação de carga ilimitada, eficácia com malware ou bateria completa de cliques
da GUI. A GUI é compilada e seus contratos de serviço são exercitados.

## Reprodução e ferramentas

```text
guardian-engine-service --protection-status
guardian-engine-service --protection-config policy.json
guardian-engine-service --protection-scan scan.json
guardian-engine-service --protection-cancel cancel.json
guardian-engine-service --protection-events cursor.json
```

Use [o exemplo de política](../config/protection-policy.example.json) com schema 1.
Pedidos de varredura usam mode quick/full/custom e paths quando personalizados;
cancelamento usa id; notificações usam after. Agenda é uma lista de
`{id, mode, paths, intervalSeconds, nextRun}`. Pedidos do cofre conservam schema 2.

`tools/validate_protection.py` prepara fixtures e executa as etapas sem elevação;
`tools/validate_protection_admin.py` executa explicitamente as etapas administrativas
em terminal já autorizado. As ferramentas não tentam elevar sua própria execução.
A ordem é prepare, basic, configure (admin), monitor, release (admin), schedule
(admin), recovery-submit, crash (admin), recovery-check, restart (admin), cleanup
(admin), final. Cada etapa usa a mesma pasta nova e --config Release/Debug.
A política anterior é restaurada e apenas entradas da fixture são excluídas.
`tools/package_protection.py` exige ambas as provas atuais, conserva fontes e
binários próprios e exclui bancos, chaves, pedidos locais, referências Microsoft,
estado de usuários e catálogos locais de execução. O pacote extraído recebe nova
conferência de hashes e execução dos contratos desta fase.

Referências oficiais: [notificações de diretório](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw),
[token da sessão](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken),
[acesso com identidade do usuário](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-impersonateloggedonuser),
[segurança dos serviços](https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights).
