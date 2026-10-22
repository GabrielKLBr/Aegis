# Fase 6 — I/O, dispositivos e drivers próprios

Base ativa: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Imagem: `guardian-kernel-io.gk`, versão 0.6.0, x64, uma vCPU, WHP.
Estado: concluída em 16/10/2026. A fase 7 não foi iniciada.

## Implementação

O kernel agora registra, inicia, atende requisições e descarrega três drivers
internos: console, canal de mensagens e disco em blocos. O gerenciador de I/O,
os callbacks dos drivers, a fila, o cancelamento e a conclusão executam em
CPL0 no convidado. O host implementa os dispositivos virtuais em memória e
valida o protocolo. Nenhum driver é instalado no Windows host.

`guest/IoManager.cpp` contém o núcleo freestanding. `IoKernel.cpp` liga esse
núcleo aos processos, handles e direitos da fase 5, ao timer e às esperas do
escalonador. A imagem de I/O reutiliza a execução de processos da fase 5 e
adiciona operações reais de I/O por syscall ao programa de usuário do pai.
As outras imagens continuam usando seus contratos anteriores.

### Barramento e fronteira host/convidado

`shared/guardian/Io.h` define um barramento versionado com três descritores:
identificador, classe, versão, tamanho máximo, capacidade, operações e
reservados. O kernel valida todos os campos e a cobertura do mapa de memória.
A enumeração é somente leitura no WHP e permanece supervisora nas tabelas do
convidado. Trata-se de uma extensão própria, separada da tabela inicial de
console/controle do contrato de boot.

| Área fixa | Endereço físico | Uso |
|---|---|---|
| Pacote | `0x10C000` | pedido e resposta de até 584 bytes |
| Relatório | `0x10D000` | resultado da execução |
| Barramento | `0x10E000` | enumeração imutável de 152 bytes |
| Porta | `0xF0` | OUT de um byte: transação ou publicação do relatório |

Pedidos carregam versão, tamanho, sessão de boot, sequência estritamente
crescente, ID da requisição, dispositivo, operação, offset e comprimento.
São copiados do endereço fixo antes de validar e executar. O backend não
recebe ponteiros do host nem caminhos escolhidos pelo usuário. Respostas
repetem a identidade do pedido, informam status e quantidade transferida;
o driver verifica esses campos antes de aceitar dados ou concluir a operação.

IN, OUT largo, comandos desconhecidos, transação fora do estado de execução,
sessão errada, replay e campos reservados produzem diagnóstico. A enumeração,
pacotes e relatório usam áreas distintas. O relatório só é aceito uma vez,
depois do relatório de processos e antes do relatório final de CPU, com o
timer desarmado. Pedidos malformados não avançam a sequência nem alteram os
dispositivos. Falhas operacionais válidas retornam status limitado.

### Carregamento e descarregamento

Cada driver possui contrato interno com versão, tamanho, arquitetura, rota
e callbacks de início, parada e despacho. Versão/arquitetura/tamanho/flags
incompatíveis ou callbacks ausentes são recusados com `Incompatible`.
Início malsucedido não publica uma instância ativa. IDs de instância têm
geração para recusar referências antigas após recarregamento.

O início valida o barramento e ativa a rota do driver. A parada verifica a
instância ativa e desativa a rota. Descarregar é recusado enquanto há handles,
referências externas ou pedidos ainda não recolhidos. Falha de dispositivo
ou resposta inválida coloca a instância em falha; pedidos posteriores não
continuam enviando operações por essa rota. Descarregar e recarregar cria
uma instância nova, depois da recuperação dos recursos pendentes.

Nesta fase, carregar significa registrar e iniciar drivers compilados dentro
da própria imagem. Não existe ainda um carregador de arquivos `.sys`, ABI WDM,
PnP completo, DMA, passagem de hardware ou drivers Windows de terceiros.
O loader geral pertence à fase 8; a compatibilidade ampliada, à fase 12.

### Requisições assíncronas e cancelamento

Enviar uma requisição valida o contrato, aplica quotas e reserva um objeto
de conclusão antes de publicar o pedido na fila. A chamada retorna `Pending`;
o backend ainda não foi acionado. Escritas fazem cópia dos dados de entrada.
Alterar o buffer do usuário após o envio não muda os bytes já submetidos.

O timer existente chama o serviço de I/O em CPL0. Os pedidos são retirados em
ordem FIFO, despachados pelos drivers e concluídos exatamente uma vez. Cada
requisição tem um evento manual próprio. Uma thread pode aguardar esse evento
sem ocupar a CPU; a conclusão a torna pronta novamente. Recolher o resultado
antes de concluir retorna `Pending`.

Cancelar um pedido ainda na fila conclui com `Cancelled` e não executa sua
operação no dispositivo. Um cancelamento tardio retorna `TooLate`, preservando
o resultado existente. O teste real aceita a corrida legítima com conclusão e
repete o cancelamento de forma limitada até exercitar o caminho cancelado.
Ao sair um processo, seus pedidos são cancelados/recolhidos e suas referências
são liberadas. IDs de pedido com geração e a identidade do processo impedem
consultar, cancelar ou recolher pedidos de outro processo ou de uma geração
anterior.

Fila e drivers operam na única vCPU, com interrupções mascaradas dentro do
kernel. Despacho e conclusão do backend são serializados; não há cancelamento
concorrente dentro do callback, SMP, DPC preemptível ou interrupção dedicada de
dispositivo nesta etapa. Um pedido em despacho não pode ser reutilizado.

### Permissões e vida útil dos buffers

Os dispositivos são objetos da tabela de handles da fase 5. Abrir passa pela
DACL e integridade. Leitura exige `ReadMemory`, escrita/flush exige
`WriteMemory` e controle de descarregamento exige `Modify`. Um handle de
leitura não autoriza escrita. As identidades continuam sendo internas ao
convidado; não são credenciais do host.

Cada requisição mantém uma referência ao objeto do dispositivo até ser
recolhida ou removida na saída do processo. Fechar o handle não invalida o
pedido já submetido. Isso é uma regra explícita da ABI própria, sem promessa
de reproduzir a semântica de arquivos/IRPs do Windows. Eventos de conclusão,
slots e referências são liberados na recuperação.

Leituras permanecem no buffer interno até a aplicação recolher o resultado.
O destino inteiro é validado para escrita nesse momento, respeitando o
espaço de memória atual. Um destino inválido é recusado sem copiar parcialmente
nem consumir o resultado; a aplicação pode tentar novamente com destino válido.
Buffers do kernel e páginas supervisoras não são aceitos como buffers CPL3.

### Dispositivos implementados

- Console: escritas ASCII, com validação integral e orçamento total de 4 KiB.
  Uma escrita com byte inválido não produz saída parcial. A saída é capturada
  pelo dispositivo e incluída no relatório de boot.
- Canal: FIFO de até oito mensagens, cada uma com até 512 bytes. Escrita
  enfileira; leitura retorna a próxima mensagem. Buffer pequeno preserva a
  mensagem; canal vazio/cheio retorna `Busy`. Não há encaminhamento para rede,
  shell, arquivos ou serviços do Windows.
- Blocos: dispositivo de RAM com 64 KiB e setores de 512 bytes. Leitura e
  escrita transferem setores reais do array isolado; limites e alinhamento
  são validados dos dois lados. Flush é um ponto de ordenação da fila em
  memória. Não representa persistência em arquivo ou gravação no disco físico.

## Aceitação e regressões

`IoChecks.cpp` executa o mesmo núcleo de 630 verificações nativamente e em
CPL0 em cada boot de I/O. Cobre contratos incompatíveis, falha no início,
descarregamento ocupado, cópia de entrada, espera/conclusão, cancelamento,
quota por dono/global, FIFO, ID antigo, dono incorreto, resposta inválida,
quarentena da instância, esgotamento de eventos e 128 ciclos com recuperação.
`GuestIoTransportTests.cpp` acrescenta 95 verificações da fronteira do host:
framing, sequência, limites, replay, atomicidade de console, capacidade e
preservação de mensagens, dados de bloco e falha de dispositivo.

Em cada boot real, o programa CPL3:

1. Abre o disco, confirma que descarregamento com handle aberto é recusado,
   envia uma escrita e fecha o handle antes da conclusão.
2. Altera o buffer original; depois de aguardar a conclusão, lê o setor por
   outro handle e confere que os dados submetidos foram preservados.
3. Confirma que escrita por handle de leitura, offset fora do disco e destino
   supervisor são recusados. Recolhe a leitura usando um destino válido.
4. Envia e recebe uma mensagem de oito bytes, escreve `IO OK\n` no console e
   cancela uma escrita de bloco ainda na fila.
5. Continua o cenário da fase 5: criação do filho restrito, herança controlada,
   cópias autorizadas e fault contido no processo adversário.

Ao final, os três drivers foram parados e descarregados; não restam pedidos,
pins, handles ou objetos de processo. Restam apenas os 21 frames do espaço de
kernel/ociosidade. Bytes contados pelos drivers devem coincidir com o backend.

A matriz de I/O exige 20 boots com 16 MiB e 20 com 64 MiB por configuração,
mais 22 casos externos negativos: 12 envelopes inválidos, cinco relatórios
inválidos, ordem incorreta, IN/OUT largo, dispositivo removido e timer ausente
com watchdog. A falha injetada no disco produz encerramento diagnosticado,
sem escrita de bloco, fault de kernel ou corrupção do host.
Critérios: `config/phase6-acceptance.json`.

## Resultado verificado

| Verificação | Resultado |
|---|---|
| Build completo Debug / Release | aprovado / aprovado |
| CTest por configuração | 28 aprovados, zero falhas e zero ignorados |
| Núcleo de I/O | 630 verificações nativas e em cada boot |
| Fronteira do host | 95 verificações por configuração |
| Boots positivos de I/O | 80 |
| Boots positivos de regressão das fases 1–5 | 400 |
| Casos externos negativos | 204 (44 de I/O, 160 de regressão) |
| Exemplos confiáveis do projeto | sete por configuração, 14 execuções aprovadas |
| Build Debug limpo independente | aprovado, seis imagens auditadas |
| Artefatos do build completo | 57 executáveis e oito imagens por configuração |
| Fronteiras freestanding verificadas | 28 combinações de alvo/configuração |
| Evidências históricas preservadas | 237 arquivos com SHA-256 inalterado |

Os boots de I/O da matriz levaram 164,976–185,445 ms em Debug e
133,972–155,998 ms em Release nesta máquina; esses números não são garantia de
prazo. A imagem ocupa 552.960 bytes de espaço virtual em Debug e 528.384 em
Release, dentro do limite de 1 MiB do carregador. As seis imagens de produção
de cada configuração passaram as auditorias de imports/TLS/relocations/W+X.
Os dois boots adicionais da CLI por configuração não estão somados aos 480
boots da matriz.

## Compilar e executar

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-io.gk
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-io.gk --io --ram-mib 64 --repeat 2 --report .\build-codex\phase6-cli-Release.json
```

`--io` seleciona a extensão de processos com os dispositivos da fase 6.
Contratos incompatíveis são recusados antes de criar a VM. O WHP é usado
diretamente, sem QEMU e sem mudanças nas configurações de boot ou segurança
do Windows. Somente programas próprios e os sete exemplos confiáveis do
projeto são executados na validação.

## Evidências e recuperação

Resultados novos usam o prefixo `phase6-`, inclusive as regressões. O inventário
de 237 arquivos históricos das fases anteriores fica no backup
`snapshots/codex-before-phase6-20261016-204825/`, junto dos doze arquivos
anteriores alterados. Há 14 arquivos novos de implementação, testes,
configuração e documentação.

Em `build-codex/`, os logs `phase6-full-*-script.log` e
`phase6-*-tests-details.log` registram compilação e CTest. `phase6-boots-*` e
`phase6-regression-phase1-*` até `phase6-regression-phase5-*` guardam a matriz e
seus negativos. `phase6-reference-programs.json`, `phase6-smoke.json` e
`phase6-cli-*` registram exemplos e CLI. `phase6-image-audit.json` e
`phase6-build-boundaries.json` verificam imagens e ausência de runtime host no
convidado. O build limpo fica em `build-codex-phase6-clean/` e seus logs têm
o prefixo `phase6-clean-`.

`phase6-change-manifest.json` registra SHA-256 de fontes, backups, executáveis,
imagens e evidências. Revalidar sem executar programas:
`python tools/validate_phase6.py --snapshot snapshots/codex-before-phase6-20261016-204825`.
Tentativas intermediárias foram preservadas, incluindo a correção inicial da
leitura do comando de OUT: apenas o byte transferido é usado, seguindo o
contrato da porta. LNK4281 continua sendo o aviso esperado da base fixa de
2 MiB exigida pelo carregador.

## Limites e parada

Até três drivers internos, oito pedidos globais e quatro por processo,
512 bytes por transferência e uma vCPU. IDs e contadores são limitados;
gerações esgotadas não são recicladas silenciosamente. O armazenamento desta
etapa é volátil e não há sistema de arquivos ou registro persistente novo.

As limitações da fase 5 continuam válidas. Esta entrega não declara
compatibilidade geral com drivers ou aplicações Windows, nem conclusão do
antivírus ou invisibilidade do sandbox. Persistência, overlays, arquivos e
registro serão tratados na fase 7.

Ao concluir a fase 6, parar. A fase 7 só começa após nova solicitação.

## Referências de semântica

- [Microsoft: filas de requisições](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/queuing-and-dequeuing-irps)
- [Microsoft: conclusão de requisições](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/when-to-complete-an-irp)
- [Microsoft: sincronização de cancelamento](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/synchronizing-irp-cancellation)
- [Microsoft: condições de descarregamento](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/unload-routine-environment)

São referências para decisões de projeto; o contrato implementado é a ABI
própria descrita neste documento, não uma implementação completa de WDM.
