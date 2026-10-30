# Fase 15 — Quarentena e gerenciamento de ameaças

Esta entrega substitui o armazenamento XOR e o fallback que truncava o original.
A quarentena funciona sem o MiniNT. As operações do aplicativo usam a engine de
antivírus e um processo auxiliar com o token restrito do usuário autenticado.
O supervisor SYSTEM não abre caminhos de arquivos enviados pelo cliente.

## Operações e integridade

O cofre por usuário fica no LocalAppData real conhecido pelo Windows, em
`Aegis/quarantine-v2`. A variável de ambiente LOCALAPPDATA não escolhe seu destino.
SQLite mantém conteúdo, metadados e histórico em transações com journal DELETE,
synchronous FULL e secure_delete. Extensões SQLite são desabilitadas no build.
Conteúdo e metadados têm criptografia autenticada AES-256-GCM com domínios e
nonces independentes. A chave é protegida pela DPAPI do usuário. O histórico
criptografado encadeia hashes e mantém uma cabeça autenticada na mesma transação.
A abertura detecta alteração, truncamento do histórico e sumiço/ressurreição de
entradas em relação às ações registradas.

Isolar mantém o arquivo aberto sem compartilhamento para escrita/exclusão,
registra identidade, SHA-256, tamanho, atributos, horários e DACL. Para uma detecção,
o hash precisa coincidir com os bytes verificados pelo scanner. Uma cópia cifrada
é confirmada e relida antes de excluir o original pelo seu handle. Não há
fallback para truncar ou sobrescrever o original. Falhas posteriores conservam
uma entrada recuperável; estados preparados ou pendentes exigem revisão e não
provocam exclusão automática ao abrir o programa.

Restaurar exige diretório escolhido e nome novo, sem sobrescrever destinos.
O conteúdo é autenticado e conferido por SHA-256, escrito num temporário privado
que nega execução, descarregado e relido. O Windows publica esse mesmo arquivo
por renomeação sem substituição. DACL, atributos suportados e horários são
restaurados; as ACEs originais ficam protegidas contra herança adicional do novo
diretório. A cópia no cofre permanece até uma exclusão explícita. O programa não
executa o arquivo restaurado. Se o histórico estiver cheio ou sua escrita falhar,
a recuperação explicitamente solicitada ainda pode produzir uma cópia verificada;
a resposta informa `complete=false`, `copyRestored=true`, `historyUpdated=false`
e preserva o cofre. Não anuncia uma operação inteiramente concluída nesse caso.

Excluir remove somente a entrada do cofre numa transação com sua ação de histórico.
Não exclui arquivos existentes na origem nem cópias já restauradas. Isso é exclusão
lógica: não garante apagamento físico em SSD, backups ou versões anteriores.

## Interface e serviço

A página Quarentena oferece atualização, isolamento manual, seleção de item,
restauração para destino novo, exclusão com confirmação e histórico paginado.
Detecções de varredura oferecem Isolar vinculado ao hash da detecção. A interface
mostra a resposta efetiva do serviço, inclusive falhas e cópias que requerem revisão.
Ações ocorrem fora da thread gráfica. Desligar a engine impede essas operações na
interface; não existe execução privilegiada alternativa na UI.

O protocolo acrescenta as operações 6–10 somente à engine de antivírus.
Solicitações de alteração usam JSON schema 2 de até 64 KiB. O diretório do cofre
é fixado pelo worker; não pode ser escolhido no pedido de serviço. Listagens têm
prévias limitadas de campos longos, identificadas por `previewTruncated`; os valores
completos permanecem cifrados. O histórico retorna até 128 ações por resposta,
com `nextBefore` para buscar páginas anteriores. Todas as páginas são verificadas
contra a cadeia completa ao abrir o cofre.

A ferramenta `guardian-quarantine.exe` permite um cofre explicitamente escolhido
para diagnóstico e testes. Exemplo com arquivos próprios e inertes:

```text
guardian-quarantine --root D:\teste\cofre --action isolate --input D:\teste\inert.bin
guardian-quarantine --root D:\teste\cofre --action list
guardian-quarantine --root D:\teste\cofre --action restore --id ID --directory D:\teste --filename recuperado.bin
guardian-quarantine --root D:\teste\cofre --action audit
guardian-quarantine --root D:\teste\cofre --action audit --before NUMERO
guardian-quarantine --root D:\teste\cofre --action erase --id ID
```

O serviço oferece `--q-list`, `--q-audit [pagina.json]`, `--q-isolate pedido.json`,
`--q-restore pedido.json` e `--q-erase pedido.json`. Pedidos incluem `schema: 2` e,
respectivamente, `source/sha256/threat/reason`, `id/directory/filename`, `id`.
A paginação do histórico usa `before`. O cliente de console antigo também usa o
novo cofre e recusa isolamento de detecção sem hash.

## Limites e modelo de proteção

- Até 64 MiB por arquivo, 256 entradas, 8192 ações e 262144 páginas SQLite de
  4096 bytes (1 GiB, além do journal temporário). Limites recusam novos isolamentos
  antes de remover o original. Histórico cheio não é descartado automaticamente;
  a recuperação explícita continua disponível com aviso de histórico incompleto.
- Caminhos locais absolutos com menos de 240 caracteres; volumes locais fixos ou
  removíveis. UNC, dispositivos, traversal, ADS, junctions/reparse points,
  hardlinks, EFS, arquivos comprimidos/esparsos/offline são recusados.
- Apenas arquivos pertencentes ao usuário atual com DACL concreta. Não há
  restauração de SACL, outro proprietário, alternate streams ou privilégios de
  backup. Arquivos bloqueados ou sem autorização de exclusão permanecem na origem.
- ACLs limitam acesso ao usuário e SYSTEM. A DPAPI é vinculada ao perfil do usuário
  e normalmente ao computador. Perder o perfil/chave pode impedir recuperação.
  Não se reinicializa uma chave ausente sobre um banco existente; banco ausente,
  vazio ou sem cabeça de histórico também não é reinicializado com chave antiga.
- O cofre não é uma fronteira contra um processo que já controle a mesma conta:
  esse processo pode usar as credenciais DPAPI e permissões do usuário. A proteção
  contra adulteração por malware com a mesma identidade depende de um broker/cofre
  protegido do serviço, a aprofundar na fase 16. Criptografia e cadeia de auditoria
  não são apresentadas como resistência a esse adversário ou a rollback completo
  de uma cópia antiga consistente do banco e da chave.
- Temporários privados de uma restauração abruptamente interrompida podem ficar
  no destino, sem execução permitida; não há varredura automática que apague
  arquivos do usuário. Arquivos legados XOR não são importados nem removidos.
- As provas incluem encerramento abrupto de processos, não corte físico de energia.
  Durabilidade depende de NTFS/SQLite/armazenamento cumprirem o flush. Não há
  certificação de proteção antimalware ou de recuperação em qualquer falha física.

## Validação e reprodução

A campanha usa arquivos novos e inertes, operações NTFS reais e um driver de
injeção de falhas separado. A ferramenta de produção não aceita esses comandos.
O driver de teste não é instalado como componente de serviço.

```text
python tools/validate_quarantine.py --build BUILD --config Release --output PASTA_NOVA
python tools/validate_quarantine.py --build BUILD --config Debug --output OUTRA_PASTA_NOVA
python tools/validate_quarantine_services.py --build BUILD --output NOVA_PASTA_SERVICOS
python tools/package_quarantine.py --build BUILD --release PROVA_RELEASE --debug PROVA_DEBUG --services PROVA_SERVICOS --output NOVO_PACOTE
```

São validados bytes, SHA, horários, DACL, ACL do cofre, nomes Unicode, arquivo vazio,
conflitos, troca após detecção, bloqueio, somente leitura, links/streams, quotas,
SQLITE_FULL, interrupções em isolamento/restauração/exclusão, tags GCM, chave
perdida, inventário versus histórico e recuperação com histórico indisponível.
A integração usa o serviço instalado, scanner com marcador inerte, token restrito,
isolamento, listagem, restauração, conflito, exclusão, histórico e engine desligada.
As configurações Release e Debug aprovam **57/57** casos cada; a integração do
serviço instalado aprova **13/13** casos. A interface é compilada em Release/Debug e seu contrato de dados é exercitado
pelo cliente do serviço; não equivale a teste manual completo de cliques da GUI.
A aceitação é repetida a partir do pacote extraído, cujos bytes são conferidos.
Não são repetidas as baterias de compatibilidade 12F–14.x.
Os [resultados por caso](PHASE-15-RESULTS.json) registram a aceitação desta fase.

Chaves DPAPI, bancos de quarentena, arquivos de usuário, referências Microsoft e
catálogos locais de execução não entram no pacote. Fontes vão ao repositório
público; os binários e suas provas ficam no repositório privado de builds.

Referências: [DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata),
[renomeação sem substituição](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info),
[SQLite synchronous](https://www.sqlite.org/pragma.html#pragma_synchronous).
