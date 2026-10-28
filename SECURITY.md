# Segurança

Aegis é um projeto em desenvolvimento. Os limites de compatibilidade, cobertura
e aceitação Windows estão documentados em `docs/PHASE-13.md` e no roadmap.
Isolamento e telemetria não representam garantia de indetectabilidade ou de
proteção completa contra qualquer arquivo malicioso.

Os modos opcionais da [14.10A](docs/PHASE-14.10A.md) , da
[14.10B](docs/PHASE-14.10B.md), da [14.10C](docs/PHASE-14.10C.md) da [14.10D](docs/PHASE-14.10D.md) e da [14.10E](docs/PHASE-14.10E.md) executam conjuntos limitados
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

Na 14.10D, seletores de arquivo usam somente o namespace convidado; não há
navegador Shell do host. Fontes/ícones e mensagens de controles usam contratos
limitados, IDs próprios, retenção dos objetos selecionados e cotas. A política
de geometria é virtualizada e não comprova equivalência completa de USER32.

Na 14.10E, curvas, paths, regiões, transformações, glifos e descritores usam
contratos limitados e autoria de objetos. Ponteiros/padding de descritores do
host não são serializados. Cursores afetam somente os clientes da sessão.
O conjunto maior de operações GDI exige as mesmas verificações de isolamento,
cotas, falhas parciais e limpeza; não representa garantia de ausência de escape.

Na [14.10F](docs/PHASE-14.10F.md), o auxiliar também executa Direct2D,
DirectWrite, codecs internos WIC e D3D11 WARP por comandos limitados. Não
recebe ponteiros, módulos de shader/driver, caminhos do host ou interfaces
COM do convidado. Há cotas de imagens/texturas e erros separados de API e
transporte. Esses componentes aumentam a superfície de ataque; os testes
de compatibilidade não comprovam contenção para executar malware real.
