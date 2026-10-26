# Fase 14.10A — protocolo e auxiliar de GUI nativa

Este lote implementa a primeira passagem completa de **comandos de GUI do
MiniNT para APIs reais do Windows host**. O programa mínimo cria uma janela,
pinta duas regiões com `FillRect` e verifica suas cores com `GetPixel`.
O Windows executa o desenho; o MiniNT não fornece uma janela rasterizada.
O canal gráfico legado pode registrar um desktop vazio para suas verificações
de compatibilidade; ele não desenha nem fornece o conteúdo da janela nativa.

O aplicativo usa a DLL própria `guardian-native-gui.dll` e sua chamada tipada.
Isto ainda não adapta os imports USER32/GDI de aplicações existentes: janelas,
controles e mensagens correspondem à 14.10B; desenho mais amplo à 14.10C; a
aceitação gráfica externa, incluindo Flappy neste backend, à 14.10D. O modo
de pixels da [12F](PHASE-12F.md) permanece disponível e separado.

## Contrato e execução

`guardian-kernel-native-gui.gk` reutiliza a base da 12F/14.9 e anuncia
`GuardianNativeGui/1/guest`. Uma chamada CPL3 só é aceita pelo serviço `0xAA`
quando parte da seção executável da DLL gateway declarada. O kernel copia e
valida a estrutura, associa PID/TID reais e envia um pacote pelo canal F8,
operação byte OUT 4. A enumeração fica no deslocamento 256 da página gráfica
existente e continua protegida contra escrita pelo convidado.

O protocolo fixa tamanho, versão, boot, sessão aleatória, sequência, autoria,
comando e resposta. Não contém endereços, callbacks ou handles Windows. A tabela
de objetos tipados pertence à sessão e exige o PID/TID criador. IDs não são
reutilizados durante a sessão. Uma referência numérica a um objeto externo ou
de outro tipo não concede acesso a ele.

O conjunto deste lote é: criar janela, criar brush RGB, obter DC dessa janela,
preencher retângulo, consultar um pixel de sua superfície, liberar DC, apagar
brush, destruir janela e concluir. `Hello` é exclusivo da inicialização do IPC,
sem serviço equivalente para o programa convidado. Tipos ou parâmetros inválidos
retornam erro; corrupção de sessão/seqüência/envelope encerra o canal.

O auxiliar cria uma DIB e um DC **no host** para conservar o desenho durante
repaint, usa `FillRect` de GDI e apresenta por `BitBlt`. A consulta de cor usa
`GetPixel` nessa superfície própria. Não recebe arrays de pixels do MiniNT.
A janela recebe o prefixo `[MiniNT native 14.10A]`.

## Processo auxiliar e IPC

O gerenciador inicia `guardian-native-gui-host.exe` pelo caminho absoluto ao
lado de seu próprio executável. O manifesto seleciona `win32-gdi-helper`, fixa
a versão do protocolo e o SHA-256 desse auxiliar; arquivo trocado exige novo
pacote/vínculo. A leitura do JSON recusa chaves duplicadas, sintaxe inválida,
profundidade excessiva e valores de tipo incompatível.

A criação usa token restrito derivado do usuário atual, grupo Administradores
em deny-only, privilégios removidos salvo `SeChangeNotifyPrivilege` e nível de
integridade baixo. A lista de SIDs restritores permite os acessos normais de
leitura/inicialização do usuário e de recursos Windows necessários à interface.
O auxiliar verifica seu token e a associação ao Job antes de aceitar comandos.
Este perfil não é AppContainer nem uma declaração de bloqueio de toda leitura
de arquivos do usuário. O contrato não oferece operações de arquivo/rede/COM,
carregamento de DLL escolhida pelo convidado ou execução de código convidado
no auxiliar.

Somente duas pontas de pipes anônimos são herdadas, por uma lista explícita de
handles. Stdout/stderr compartilham a mesma ponta de resposta. O ambiente contém
somente `SystemRoot` e `WINDIR`, sem variáveis pessoais ou credenciais do processo
gerenciador. O processo começa suspenso e entra no Job antes de executar.

O Job limita memória privada a **64 MiB** e processos ativos a **um**, encerra
seus processos ao fechar e aplica restrições de UI a clipboard, handles externos,
átomos globais, desktops e alterações de configurações do sistema. Não há fallback
para um auxiliar sem essas restrições quando a criação falha.

Há uma única solicitação em trânsito. Cada pacote cabe no buffer do pipe; uma
ponta de leitura mantida somente pelo pai verifica que o pedido anterior foi
consumido antes da próxima escrita. Respostas extras, desconexão, alteração de
envelope e contadores inválidos são recusados. A inicialização tem limite de dez
segundos; cada resposta, dois segundos. Ao encerrar, o pai fecha o canal, espera
até 500 ms e, se necessário, termina o Job e aguarda até dois segundos.

## Limites do lote

| Recurso | Limite/estado |
| --- | --- |
| Janelas | Quatro por sessão |
| Objetos, incluindo janelas/brushes/DCs | 64 por sessão |
| Comandos | 1.024 por sessão |
| Cliente da janela | 16×16 até 1.280×720 |
| Posição inicial | −4.096 até 4.096 por eixo |
| Título convidado | Até 63 caracteres ASCII |
| Cores | RGB de 24 bits |
| Referência convidada | Windows 10 x64 da matriz existente |
| USER32 geral, controles e callbacks de mensagens | Pendentes da 14.10B |
| Fontes/texto/imagens gerais e perfis NT adicionais | Pendentes da 14.10C/matriz |
| DirectX e áudio | Não iniciados |

O fechamento da janela e a entrada nativa ainda não são encaminhados à aplicação
por este contrato. O loop modal de mover uma janela pode exceder o prazo de uma
operação e terminar a sessão. Esses comportamentos pertencem ao lote USER32.
Aplicações que saem com objetos vivos falham na conclusão; o supervisor recolhe
o auxiliar. Não se anuncia compatibilidade GUI geral nem equivalência de GDI
entre versões NT pela presença desse backend.

## Uso

Fornecer os CRT/schema legais da referência já usada nas fases anteriores:

```powershell
python -B tools/native_gui_runtime.py --build BUILD --config Release --reference CRT --schema-reference SCHEMA --output app-native
BUILD/Release/guardian-desktop.exe BUILD/guest/Release/guardian-kernel-native-gui.gk app-native/runtime.gpv app-native/manifest.json app-native/binding.gnt sessao-native --native-gui --seconds 8
```

`--native-gui` deve corresponder ao backend do manifesto. Não se combina com
`--integrated`, `--interactive`, `--script` ou `--window-script`. Uma pasta de
saída existente é recusada e seus relatórios permanecem intactos.

`native-gui.json` registra eventos por sequência/PID/TID/operação/resultado,
restrições, saída do auxiliar e os últimos recursos conhecidos. `nativeCalls`
contém, nesta ordem, contagens de criação de janela, brush, GetDC, FillRect,
GetPixel, ReleaseDC, DeleteObject e destruição de janela. A conclusão normal
exige zero objetos e saída zero do auxiliar. Depois de uma queda, os últimos
contadores podem conter recursos vivos: a saída do processo e o teste de ausência
de suas janelas comprovam o recolhimento, sem inventar uma resposta de destruição.

## Aceitação e continuidade

Os testes novos são `native_gui_core`, `native_gui_broker`,
`native_gui_integration` e `native_gui_runtime_tools`. Incluem envelopes inválidos,
tipos/autoria/IDs expirados, cotas, manifesto, versões, duas sessões com DCs reais,
cores obtidas por GDI, falha parcial de inicialização, auxiliar morto/suspenso,
modos incompatíveis, vínculo SHA e preservação de relatórios anteriores.
O teste do auxiliar também captura somente o cliente de seu próprio HWND com
`PrintWindow` e confere a cor desenhada, além da consulta interna com `GetPixel`.

São cinco cenários MiniNT por configuração: duas execuções normais com sessões
distintas, queda do convidado, watchdog e morte do auxiliar. A aceitação exige
**143 testes de regressão Release e 68 de aceitação Debug**, sem falhas/ignorados,
com fontes, binários e entradas estáveis. O pacote extraído é conferido por hash
e executado nas duas configurações; seus totais de cenários também incluem
rejeições de estados inválidos herdadas das fases anteriores.

A primeira aceitação Debug encontrou cinco falhas herdadas de rede: o
laboratório escolhia uma porta TCP e presumia disponibilidade do mesmo número
em UDP, embora o Windows mantenha reservas distintas por protocolo. O laboratório
passa a reservar o par antes de iniciar threads, com tentativas limitadas e
fechamento dos sockets rejeitados. Testes exercitam reserva específica, colisão,
esgotamento e propagação de outros erros. Não se alteram firewall ou reservas
do sistema. O relatório da tentativa original é preservado junto da entrega.

O pacote não inclui DLLs Microsoft de entrada, Flappy, ISOs, credenciais ou
estados de VM. Debug do host exige o runtime de depuração do Visual Studio.
Linux não foi revalidado. A entrega termina na **14.10A**; a **14.10B exige nova
solicitação**.

Referências: [tokens restritos](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken),
[processo com token](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw),
[herança de handles](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes)
e [DCs do GDI](https://learn.microsoft.com/en-us/windows/win32/gdi/device-contexts).
