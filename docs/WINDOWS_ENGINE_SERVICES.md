# Engines do Aegis como serviços do Windows

Entrega independente dos frameworks da fase 14.11, solicitada em 28/10/2026.
O trabalho dos frameworks permanece em andamento e não integra esta entrega.

## Uso pelo app

Abra `avui.exe` na pasta completa do pacote e entre em **Configurações**.
**Instalar serviços** solicita a autorização de administrador do Windows e
registra `AegisAntivirus` e `AegisSandbox`. Depois, cada engine tem seu próprio
botão **Ligar/Desligar** e estado consultado no gerenciador de serviços real.
A instalação inicial deixa ambos desligados, com inicialização manual.
Ao fechar o app, os serviços que estavam ligados continuam disponíveis;
análises vinculadas à janela são canceladas ao fechá-la. Isto não acrescenta
monitoramento em tempo real ou análise automática de todo o computador.

O serviço do antivírus recebe análises de arquivos/pastas e entrega progresso,
detecções e cobertura incompleta. O serviço do sandbox recebe PEs no backend
MiniNT/WHP simples e, na build com runtime gráfico, bundles preparados da 14.10G.
**Sessão com GUI** abre uma pasta contendo `runtime.gpv`, `manifest.json` e
`binding.gnt`, produzidos pelas ferramentas existentes de `audio_runtime.py`.
O kernel e o helper usados são os da instalação protegida; o bundle precisa
estar vinculado à mesma revisão. As janelas e o áudio continuam na sessão do usuário.

**Analisar um arquivo** usa o backend WHP simples já utilizado pelo scanner,
cuja compatibilidade de importações é limitada. A seleção de EXEs arbitrários
não prepara automaticamente o runtime gráfico. A unificação desse fluxo,
os instaladores e os frameworks completos continuam nas respectivas etapas
do roadmap. As ferramentas de desenvolvimento avulsas conservam seus comandos
próprios; os botões controlam os serviços e as análises encaminhadas por eles.

## Limites e isolamento

- São dois processos registrados no SCM como `SERVICE_WIN32_OWN_PROCESS`, sem
  dependência obrigatória entre as engines. Há uma análise por engine por vez.
- O supervisor usa LocalSystem para gerenciar tokens e sessões. Ele não abre
  o caminho da amostra e não executa esse arquivo como programa do host.
  A análise ocorre em um executável confiável com token restrito do solicitante,
  privilégios removidos, integridade média e a sessão interativa original.
- A instalação copia uma lista fixa de arquivos para uma pasta versionada em
  `Program Files\AegisEngineServices`, com escrita reservada a administradores
  e SYSTEM. Uma atualização valida a identidade dos registros já existentes,
  preserva o estado ligado/desligado e tenta restaurar a configuração anterior
  caso uma etapa falhe. Não substitui serviços de outros programas.
- O usuário que instalou pode consultar, iniciar e parar os próprios serviços;
  não recebe permissão para mudar executável, conta ou ACL deles. Isto permite
  os controles pedidos e não implementa proteção contra adulteração por outro
  processo da mesma conta. A autorização de administrador é necessária para
  instalar, atualizar ou remover os registros.
- Pipes aceitam apenas conexões locais, com ACL explícita, validação do PID do
  serviço, autenticação do usuário e validação do PID do worker. O protocolo
  tem versão, operações fixas, caminho de até 32.767 unidades UTF-16 e resposta
  de até 2 MiB. Não há comando para executar um binário arbitrário no host.
- Cada solicitação possui Job Object com encerramento dos filhos e cotas de
  processos/memória. Desligar a engine, cancelar a análise, perder o cliente ou
  atingir o prazo encerra o trabalho. Sandbox: até 60 segundos; árvore de
  arquivos: até uma hora. Resultados incompletos não equivalem a arquivo seguro.
- Relatórios de sessões gráficas ficam no perfil do usuário. A remoção dos
  serviços preserva pastas versionadas e relatórios para recuperação.

Serviços ficam na sessão 0; a separação entre supervisor e processo do usuário
segue o modelo de [interação indireta recomendado pela Microsoft](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services).

## Compilação e verificação

No Windows/MSVC, use `GUARDIAN_GUI_COMPATIBILITY_ONLY=OFF` para o app e os
serviços. O backend simples requer `GUARDIAN_BUILD_GUEST_CONTRACT=ON`.
O encaminhamento gráfico também requer os targets de compatibilidade até áudio
(por exemplo `GUARDIAN_BUILD_PE_GUI=ON`, `GUARDIAN_BUILD_EXTERNAL_PE=ON` e
suas dependências habilitadas).
Os targets são `guardian-engine-service`, `avui`, `engine_services_tests` e
`guardian-service-wait-sample`. A build mínima do scanner continua disponível
sem exigir o corpus de compatibilidade gráfica completo.

`tools/package_engine_services.py` monta a pasta de instalação Release,
com lista de arquivos e SHA-256, usando os redistribuíveis x64 licenciados do
Visual Studio. Binários pertencem somente ao repositório privado de builds.

O teste `engine_services_protocol` cobre validação e limites, Unicode,
transferências parciais e cancelamento de leitura pendente. O teste focado
`test/EngineServicesIntegrationTests.py` usa os serviços já instalados, amostras
próprias e uma conta sem elevação para verificar independência, YARA, erros
de cobertura, permissões, WHP e cancelamento dos processos. Com `--bundle`,
verifica também o encaminhamento de uma sessão gráfica preparada.
Somente esta mudança e suas dependências entram na validação desta entrega.

Comandos auxiliares do executável dos serviços: `--setup`, `--remove`,
`--status`, `--start antivirus|sandbox`, `--stop antivirus|sandbox`,
`--scan CAMINHO`, `--sandbox PE` e `--bundle PASTA`.

## Resultado da validação

Aceitação Windows da revisão `r3`, em 28/10/2026:

- Builds Release com runtime gráfico e mínima sem o corpus gráfico: compiladas.
- Protocolo em Release, Debug e na build mínima: passou, incluindo ACL, identidade
  do servidor, limites, Unicode, leitura cancelada e resposta final com leitor lento.
- Dez cenários nos serviços instalados: passaram. Cobrem engines desligadas,
  arquivos Unicode, detecção YARA, cobertura incompleta, configuração e executáveis
  protegidos, requisição malformada, execução WHP, parada independente, cancelamento
  por desconexão e sessão gráfica/áudio preparada da 14.10G na sessão do usuário.
- Fachada usada pelo app: passou em resultados, limpeza entre análises, cancelamento
  e falha explícita quando o serviço está desligado, sem execução local alternativa.
- Política de token: filtrado, sem administrador e com somente o privilégio de
  travessia restante. A instalação ocorreu com UAC; os testes de uso não foram elevados.

Foram corrigidos os direitos de leitura de atributos do pipe e o descarte da
resposta final quando o servidor desconectava antes da leitura pelo cliente.
O encerramento agora espera o cliente por um prazo limitado e pode ser cancelado
ao desligar o serviço, sem bloquear indefinidamente o supervisor.

A auditoria de publicação continua verificando todos os caminhos de todos os
commits. Conteúdos idênticos são conferidos uma vez por auditoria, com checagens
separadas para PNG e texto. Quatro testes focados passaram, incluindo rejeição de
artefato apagado do histórico e de conteúdo renomeado para um caminho indevido.
As 45 builds anteriores foram verificadas para preservação antes de acrescentar
o pacote desta entrega. Baterias de execução das fases anteriores não foram repetidas.
