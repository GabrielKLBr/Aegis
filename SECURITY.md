# Segurança

Aegis é um projeto em desenvolvimento. Os limites de compatibilidade, cobertura
e aceitação Windows estão documentados em `docs/PHASE-13.md` e no roadmap.
Isolamento e telemetria não representam garantia de indetectabilidade ou de
proteção completa contra qualquer arquivo malicioso.

Para uma falha que permita escapar do convidado, acessar dados privados ou
executar operações no host, use o [relato privado de vulnerabilidade no GitHub](https://github.com/GabrielKLBr/Aegis/security/advisories/new).
Inclua revisão afetada, plataforma e um caso mínimo inofensivo. Não publique
credenciais, dados pessoais ou amostras maliciosas em issues públicas.

Testes de segurança devem preservar os erros e a cobertura incompleta. Respostas
artificialmente vazias não devem ser usadas para fazer uma falha parecer sucesso.
Mudanças no núcleo precisam de revisão e validação; o gerador de scripts produz
regressões versionadas e não altera automaticamente essas fronteiras.
