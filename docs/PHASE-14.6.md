# Fase 14.6 — registro, segurança e serviços do convidado

Esta entrega acrescenta DLLs próprias `advapi32.dll`, `sechost.dll` e
`userenv.dll` ao kernel `guardian-kernel-security.gk`. Um aplicativo externo
MSVC `/MD` persiste configurações, consulta sua identidade, verifica uma DACL e
inicia um serviço com processo, conta e vida útil separados dentro do MiniNT.
O lote é delimitado ao perfil Windows 10 x64 e às referências CRT já fixadas na
14.5. Não representa ADVAPI32, Windows ou compatibilidade binária completos.

## Implementação e autorização

- Registro: abrir/criar chaves HKCU/HKLM, valores A/W, tamanhos de consulta,
  enumeração W, exclusão, flush, DACL e notificação síncrona. Cada handle guarda
  proprietário, acesso concedido e geração do nó. Fechar um handle, usá-lo em
  outra conta/processo ou apagar/recriar a chave não redireciona acesso para um
  objeto novo. As rotas NT de registro também verificam acesso e geração neste
  kernel. Os tamanhos exigidos são devolvidos nos erros de buffer pequeno.
- Tokens: `OpenProcessToken` do próprio processo, duplicação restrita e consultas
  User, Groups, Privileges, Owner, PrimaryGroup, Type, SessionId, Elevation e
  IntegrityLevel. Os handles retêm referências reais a objetos do gerenciador
  de processos e são fechados por `CloseHandle`. Duplicar não amplia direitos.
- SIDs: contas próprias `MiniNT` e `LocalService`, grupos Administrators/Users,
  Everyone e rótulos de integridade. Construção, comparação e conversão textual
  dos SIDs delimitados; descritores absolutos/relativos e ACEs allow/deny
  canônicas. `AccessCheck` usa o token retido pelo kernel e o modelo de direitos
  das fases 5/7. Não concede privilégios ou altera a identidade do chamador.
- Perfis: diretórios privados `C:\Users\MiniNT` e `C:\Users\LocalService`, HKCU
  separado por usuário e bloco de ambiente próprio. A conta do serviço não
  herda `USERNAME`/`USERPROFILE` da conta administradora.
- SCM: conectar, criar/abrir, consultar configuração/estado, iniciar, enviar STOP
  e excluir um serviço `SERVICE_WIN32_OWN_PROCESS`, sob `NT AUTHORITY\LocalService`.
  Controle administrativo depende da identidade real da conta MiniNT. O serviço
  recebe usuário 200, grupo 20, sessão 0, integridade média e nenhum privilégio.
  Uma conta comum não pode criar, iniciar, parar ou excluir serviços por forjar
  um pedido ou copiar o número de um handle de outro processo.
- Eventos: registrar fonte, reportar uma string e ler registros W com identidade
  do autor, número, tipo, ID e horário do relógio publicado. O buffer circular
  comporta os oito eventos mais recentes da sessão.

`SECHOST` encaminha as entradas de serviços/eventos e `USERENV` as entradas de
perfil para a DLL própria ADVAPI32. O empacotamento verifica esses encaminhamentos
e inventaria os exports. Não se carregam ADVAPI32/SECHOST/USERENV do host.

O protocolo privado `0xA6` aceita apenas chamadas originadas de páginas executáveis
da ADVAPI32 própria. O kernel copia e valida o pacote completo através das páginas
do chamador. Direitos, tokens, handles, estado e identidade são verificados no
kernel. Callbacks `ServiceMain`/controle continuam em CPL3. O IPC deste lote é o
canal de controle/estado do serviço; RPC e named pipes gerais continuam fora dele.

## Persistência e falhas

O estado mutável usa o volume com journal de dois slots da fase 7. A região de
64 KiB fica reservada em GPA `0x5000000`, fora do volume de DLLs e do alocador de
páginas privadas; o host a mapeia sem execução. O programa não tem acesso direto
a ela. Flush/exportação não expõem arquivos, serviços ou registro reais do host.

`guardian-pe` mantém seus cinco argumentos anteriores e aceita um sexto argumento
opcional de entrada, `SECURITY_STATE`, somente para o kernel desta fase. Uma
execução bem-sucedida exporta `security-state.bin` na pasta nova da sessão. O
arquivo pode ser fornecido a uma execução posterior. Tamanho incorreto ou
metadados corrompidos falham; não se substitui um estado inválido por um registro
vazio e não se exporta sucesso após a recusa. O checksum trata corrupção e
consistência; não autentica um arquivo deliberadamente reescrito pelo seu dono.

Configurações do SCM ficam em
`HKLM\System\CurrentControlSet\Services\<nome>\Config`. Criar/excluir configurações
usa transação com rollback da imagem em memória e commit pelo journal. Falta de
handles é detectada antes de criar uma chave/configuração. A marca de exclusão
também é persistida. A remoção definitiva aguarda o término do processo e o
fechamento dos handles.

O SCM mantém uma referência ao objeto de processo do serviço. Fechar o SCM ou
encerrar o criador não mata o serviço. Uma falha do processo é observada pelo
objeto retido mesmo após sua coleta; o estado passa a STOPPED/erro 1067. Saída do
processo limpa seus tokens, chaves, fontes de eventos e notificações pendentes.

## Validação reproduzível

Cinco testes acrescentados ao CTest:

1. `security_contract`: layouts confrontados com o SDK Windows.
2. `security_core`: contas, DACLs, handles cruzados/inválidos, gerações, esgotamento
   de handles, falha de alocação, lifetime do serviço, limpeza e recuperação do
   journal após cada um de 53 pontos de falha de escrita/flush.
3. `security_windows_reference`: referências de buffer, token, SID, DACL e consulta
   ao SCM do Windows; instala zero serviços no host. A chave temporária pertence
   exclusivamente ao teste, em HKCU.
4. `security_runtime_tools`: exports, forwarders, inventário de imports, vínculo
   com kernel correto e recusa de volume alterado.
5. `security_runtime_integration`: aplicativo com CRT original no convidado,
   notificação real de registro entre threads, isolamento LocalService, evento,
   estado reaberto em outra sessão, continuidade após a saída do criador e
   recuperação após instrução inválida em serviço. Também recusa estados de
   tamanho incorreto e corrompidos. Confere ausência de objetos/handles/referências
   vivos ao final.

`tools/validate_phase14_6.py` exige a matriz de 109 testes configurados, executa a
regressão completa Release e os 34 testes de aceitação em Release/Debug, e fixa
SHA-256 dos fontes, executáveis e referências antes/depois de cada execução.
Resultados, logs e XML ficam em `build-phase14.6/`; nenhum teste omitido conta
como aprovação. `tools/package_phase14_6.py` exige essas provas estáveis. O smoke
do pacote repete quatro cenários e duas recusas em cada configuração.

## Limites que continuam abertos

- Três processos, quatro threads por processo, três registros de serviço, 48
  handles deste protocolo, oito watches e oito eventos. Mantêm-se os limites
  nativos de memória/handles e 16 arquivos no volume imutável.
- O armazenamento de teste tem 32 nós e 512 bytes por arquivo/valor, total de
  dados de 16 KiB. O arquivo de estado possui 64 KiB por incluir metadados e o
  journal. Não é uma hive Windows nem um disco Windows completo.
- Nomes/caminhos administrativos são ASCII, até 127 caracteres de caminho e
  31 por componente; strings W de valores usam UTF-16. A variante A aceita
  somente conteúdo ASCII neste lote. Não há WOW64, links de registro, hives
  externas ou registro remoto. `RegNotifyChangeKeyValue` assíncrono continua
  recusado. Notificações usam as permissões read/list do modelo de armazenamento.
- As ACLs usam o modelo compacto canônico das fases 5/7; não cobrem todos os
  direitos Windows, ACEs de objeto/callback/herança, SACLs, troca de proprietário,
  auditoria completa ou privilégios arbitrários. Operações fora desse modelo
  falham; o corpus não estabelece equivalência geral de ACLs do NT.
- Tokens duplicados sustentam consultas/AccessCheck; não implementam impersonação
  de thread, logon, alteração de privilégios, domínios ou criação arbitrária de
  contas. O argumento nulo dos helpers de perfil representa a conta atual.
- SCM aceita início manual, processo exclusivo, uma conta LocalService e STOP.
  Não há LocalSystem, credenciais/senhas, autostart, dependências, argumentos de
  início, pausa, recuperação automática ou instalação de drivers. Um serviço
  travado não é tratado como parado por retornar sucesso artificialmente.
- O event log é um anel em memória desta sessão, sem catálogo de mensagens,
  categorias, dados binários ou durabilidade entre reinicializações.
- Apenas o perfil Win10 desta matriz foi aceito. A política de perfis da 14.1 e
  as referências/versionamentos da 14.3–14.5 permanecem em vigor; outros perfis
  e programas arbitrários não ganham compatibilidade por essa aprovação.
- GUI externa ampla, WinRT, DirectX, rede geral, DLLs arbitrárias e SMP de
  aplicação continuam sujeitos às fases correspondentes. Esta entrega para
  antes da 14.7 e não altera as pendências da 12F.

## Referências primárias

- [Contas de serviços](https://learn.microsoft.com/en-us/windows/win32/services/service-user-accounts).
- [Estados de serviço](https://learn.microsoft.com/en-us/windows/win32/services/service-status-transitions).
- [Direitos do SCM e dos serviços](https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights).
- [Classes de informação de token](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_information_class).
- [Notificação de registro](https://learn.microsoft.com/pt-br/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue).

Os fontes são publicados em `GabrielKLBr/Aegis`; os componentes compilados e suas
provas usam somente `GabrielKLBr/Aegis-builds`. DLLs de entrada Microsoft, ISOs,
volumes GPV e dados pessoais não integram o pacote publicado.
