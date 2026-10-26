# Segurança

Aegis é um projeto em desenvolvimento. Os limites de compatibilidade, cobertura
e aceitação Windows estão documentados em `docs/PHASE-13.md` e no roadmap.
Isolamento e telemetria não representam garantia de indetectabilidade ou de
proteção completa contra qualquer arquivo malicioso.

O modo opcional da [14.10A](docs/PHASE-14.10A.md) executa um conjunto limitado
de operações GUI por APIs do Windows em um auxiliar com token restrito,
integridade baixa e Job supervisionado. O código da aplicação permanece no
MiniNT. Esse auxiliar acrescenta superfície de ataque no host; seu perfil não
é AppContainer nem bloqueia toda leitura de arquivos do usuário. A lista de
operações, cotas, IPC e testes de falha estão no documento da fase. A execução
de operações fora desse contrato continua sendo uma falha a reportar.

Para uma falha que permita escapar do convidado, acessar dados privados ou
executar operações no host, use o [relato privado de vulnerabilidade no GitHub](https://github.com/GabrielKLBr/Aegis/security/advisories/new).
Inclua revisão afetada, plataforma e um caso mínimo inofensivo. Não publique
credenciais, dados pessoais ou amostras maliciosas em issues públicas.

Testes de segurança devem preservar os erros e a cobertura incompleta. Respostas
artificialmente vazias não devem ser usadas para fazer uma falha parecer sucesso.
Mudanças no núcleo precisam de revisão e validação; o gerador de scripts produz
regressões versionadas e não altera automaticamente essas fronteiras.
