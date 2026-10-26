# 14.10B — USER32 e controles nativos

Entrega delimitada de Windows 10 x64. O novo modo `--native-user32` usa
`user32.dll` e `win32u.dll` próprios no MiniNT, um kernel próprio e protocolo v2.
Os imports usuais do SDK são atendidos por essa DLL. O auxiliar restrito cria
HWNDs e controles **BUTTON, STATIC e EDIT reais** com USER32 do Windows.
Seu conteúdo não vem de imagens rasterizadas pelo MiniNT. A apresentação 12F
e o protocolo GDI explícito da 14.10A continuam disponíveis em modos separados.

## Contrato e implementação

- Registro de classes da aplicação, WNDPROC, userdata, subclassificação de
  janelas próprias e TIMERPROC permanecem no convidado. A classe nativa da
  janela principal é fixa. Apenas os três controles permitidos são criados
  como classes nativas adicionais. Não se carrega DLL nem callback da aplicação
  no processo do host.
- Criação/destruição, filhos e janelas com proprietário, geometria, reparenting
  de filhos, estilos declarados, visibilidade, habilitação, foco, conversão de
  coordenadas, invalidação e temporizadores usam USER32 real. A modalidade
  inicial consiste em desabilitar o proprietário e criar uma janela possuída.
- BUTTON aceita pushbutton, default pushbutton, checkbox e autocheckbox;
  STATIC aceita texto alinhado; EDIT aceita uma linha. Texto é UTF-16, até 255
  unidades, com pares substitutos validados; adaptadores ANSI usam CP1252.
  `WM_SETTEXT/GETTEXT/GETTEXTLENGTH`, `BM_CLICK/GETCHECK/SETCHECK` e
  `EM_GETSEL/SETSEL` têm contratos explícitos. Ponteiros de seleção ficam no
  convidado; no IPC circula somente o resultado numérico validado.
- `WM_COMMAND` troca o HWND nativo do controle pelo ID da sessão. Mudanças de
  posição transportam campos de WINDOWPOS, que é reconstruída na memória do
  convidado. Nenhum endereço da estrutura do Windows é devolvido ao programa.
- `SendMessage` para uma WNDPROC da mesma thread executa o callback no MiniNT.
  Entre threads, mensagens escalares declaradas usam uma solicitação correlata:
  o remetente processa chamadas síncronas recebidas enquanto espera, inclusive
  chamadas aninhadas, e recebe o resultado da WNDPROC destinatária. O prazo é
  **2 segundos**. Expirar cancela a mensagem ainda enfileirada e recusa resposta
  tardia. Mensagens postadas preservam FIFO por thread; chamadas síncronas e
  notificações de operações nativas são atendidas antes do retorno da API.
- `GetMessage/PeekMessage/DispatchMessage` atendem as filas convidadas. Entrada
  nativa já é traduzida pelo loop do Windows; `TranslateMessage` não duplica
  caracteres. Timers usam IDs internos no auxiliar e retornam o ID da aplicação;
  funções de timer são executadas na thread convidada correspondente. O teste de callback
  cancela seu próprio timer, verificando também essa reentrância; timers muito
  curtos podem saturar um consumidor mais lento que o período solicitado.
- A thread de interface nativa nunca chama uma WNDPROC convidada e não espera
  seu retorno. Uma thread de transporte entrega tarefas a uma janela interna;
  essa janela também atende IPC durante loops modais de movimentação do Windows.
  A supervisão encerra um auxiliar que para de responder ao IPC.
- A visão convidada usa **96 DPI lógicos**. O auxiliar converte geometria e
  entrada para os pixels do monitor, atende mudanças nativas de DPI e devolve
  coordenadas lógicas. Isso não afirma igualdade de métricas entre versões NT,
  fontes ou topologias de múltiplos monitores.

Os exports efetivos estão em `guest/fixtures/NativeUser32.def`; a lista é o
contrato do lote, não uma declaração de USER32 completa. Os adaptadores seguem
os contratos documentados de [SendMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessage)
e [notificações de controles](https://learn.microsoft.com/en-us/windows/win32/controls/control-messages)
dentro dos limites declarados acima.

## Isolamento e encerramento

O processo reutiliza o lançamento supervisionado da 14.10A: token restrito,
integridade baixa, Job com um processo e 64 MiB privados, dois endpoints herdados
e ambiente contendo apenas SystemRoot/WINDIR. O manifesto fixa o SHA256 do
auxiliar ao lado de `guardian-desktop.exe`; não escolhe um executável arbitrário.
O perfil não é AppContainer e não impede toda leitura de arquivos pelo auxiliar.

O contrato permite até 32 janelas, 32 timers, 256 eventos, 32 envios pendentes e
65.536 operações por sessão. IDs são monotônicos e vinculados à sessão,
processo e thread. Operações com dono incompatível, ID expirado, campos extras,
mensagem nativa não permitida, texto inválido ou cota excedida retornam erro.
HWND, procedimentos, objetos GDI e ponteiros de estruturas do host não são IDs.

Comandos de conclusão e término são exclusivos do kernel; tentativas diretas
da aplicação são recusadas e testadas, inclusive com ponteiro de requisição
inválido. O kernel informa o término de cada thread ao auxiliar: suas janelas e timers
são removidos, e remetentes pendentes recebem erro. Ao término normal de todos
os processos, o kernel conclui o canal; uma falha convidada não é convertida em
sucesso. Queda do auxiliar, prazo da VM e transporte travado encerram o Job.
`native-user32.json` registra operações, autores convidados, erros, chamadas
nativas, recursos da última resposta e resultado do processo auxiliar.

## Validação e uso

O critério de entrega exige **147 testes Release** de regressão e **72 Debug**
de aceitação, sem falhas ou ignorados e com fontes/binários estáveis. Os quatro
testes novos cobrem contrato, processo restrito real, programas SDK dentro do
MiniNT e rejeições de empacotamento/execução. O pacote extraído repete a
aceitação gráfica anterior e os cenários novos em ambas as configurações.
Os relatórios originais acompanham a build privada.

O teste anterior de recursos também foi corrigido: a rejeição de SMP agora usa
a RAM que o próprio teste já verificou como admitida. Pedir 128 MiB fixos nessa
etapa podia produzir primeiro um erro real de orçamento de memória, mascarando
o diagnóstico de CPU que o teste pretendia verificar. O registro da primeira
regressão Release com essa falha é preservado no pacote.

O corpus novo contém dois programas independentes: controles/notificações e
mensagens entre threads, além de variantes de entrada, falha e travamento.
Testes verificam classes de controles reais, texto no EDIT do Windows, clique,
entrada por mensagens Windows direcionadas exclusivamente aos próprios controles,
retorno síncrono, reentrância, timeout sem callback tardio, payload WINDOWPOS,
IPC em loop modal nativo, cotas, autoria, duas sessões e limpeza após falhas.
O cenário entre threads injeta 200 caracteres Windows apenas na própria janela:
ele verifica mensagens da aplicação e deixa o término da thread remover a
entrada nativa pendente. Uma fila de entrada contínua não deve ser esvaziada
como condição de saída desse teste. O relatório inclui tipos de mensagens,
flags e tamanho da fila para diagnosticar atrasos e ordenação.
Não se usa injeção global de mouse/teclado nem se executa amostra maliciosa.

Compilar com MSVC/Windows SDK, Python e as referências legais já utilizadas nas
fases anteriores. Alvo agregado: `guardian_native_user32_runtime`.

```text
python -B tools/native_user32_runtime.py --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output pacote-guest
PASTA-BUILD/Release/guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-native-user32.gk pacote-guest/runtime.gpv pacote-guest/manifest.json pacote-guest/binding.gnt sessao-nova --native-user32 --seconds 8
```

Usar diretório de saída novo. CRT Microsoft, schema legal de referência, Flappy,
ISOs, discos e estados pessoais não são distribuídos na build. O ZIP contém
somente binários próprios, ferramentas, documentação e evidências selecionadas.

## Limites e próximo lote

Ainda não há USER32 universal, GDI de aplicativos gerais, fontes/imagens completas,
menus, diálogos completos, clipboard, drag-and-drop, IME, accessibility, common
controls adicionais, custom draw, owner draw ou DirectX neste modo. Não se
subclassifica o procedimento nativo de um controle pelo callback convidado.
Mensagens que exigem resposta imediata da aplicação durante o procedimento
nativo precisam de contrato próprio antes de serem habilitadas.

Controles são de uma linha, sem senha; extra styles, menus nativos e recursos
arbitrários de cursor/ícone e brushes de classe são recusados no registro.
A criação de classe aceita procedimento e nome, sem extra bytes nem class styles.
Mostrar janela não força ativação de outro aplicativo do host. Timer sem HWND e
ID automático, mudança entre top-level/child, troca de proprietário, políticas
AttachThreadInput e tradução de teclas postadas manualmente não são cobertas.
Invalidação da janela própria usa o fundo padrão do Windows; BeginPaint/HDC e
desenho personalizado ficam na **14.10C**, sem fallback silencioso para pixels.
A compatibilidade de outro perfil NT e a fidelidade em múltiplos monitores
precisam de comparação específica; executar USER32 do host não comprova ambas.

**Parar ao concluir a 14.10B. A 14.10C depende de nova solicitação.**
