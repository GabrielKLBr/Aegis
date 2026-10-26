# Fase 12F — janelas independentes do MiniNT no Windows

Esta entrega acrescenta um modo opcional de apresentação para o corpus
**Windows 10 x64** já aceito nas fases 14.7–14.9. Cada janela de nível superior,
incluindo diálogos pertencentes a outra janela, ganha um HWND independente no
desktop do host. Controles e demais janelas filhas são desenhados dentro de sua
janela principal. O Flappy original e os aplicativos próprios da matriz são
os critérios de aceitação; não há promessa de compatibilidade gráfica geral.

O EXE, suas DLLs e seus callbacks continuam no MiniNT. O componente do Windows
cria suas próprias janelas, apresenta imagens e devolve eventos tipados. Não
executa WNDPROCs, callbacks ou endereços de funções fornecidos pelo convidado.
Não há encaminhamento arbitrário de mensagens USER32/GDI.

**Distinção de escopo:** esta 12F apresenta imagens rasterizadas pelo MiniNT.
Executar as operações de criação de controles e desenho pelas APIs reais do
Windows host é o requisito adicional planejado em **14.10A–14.10D** do
[roadmap](ROADMAP-COMPLETO.md#subfase-1410--gui-nativa-no-host-directx-mídia-e-áudio).
A [14.10A](PHASE-14.10A.md) acrescenta a primeira ponte de comandos nativos;
14.10B–14.10D continuam pendentes. Os testes desta 12F não comprovam essa
renderização nativa. EXE, DLLs e callbacks continuarão no convidado também
nessa evolução.

## Ponte e isolamento

`guardian-kernel-integrated.gk` herda o kernel da 14.9 e acrescenta o protocolo
`GuardianWindowBridge/1`. O modo é selecionado por `--integrated`; a enumeração
negocia essa capacidade separadamente. Kernels anteriores recusam esse modo.
O modo do visualizador único permanece disponível com `--interactive`.

O host atribui um identificador aleatório de sessão a cada inicialização. O
protocolo binário contém esse identificador, boot, versão, tamanho e sequência.
Usa o canal F8 existente com uma operação separada. Quadros têm início,
metadados, uma imagem para cada janela principal visível e commit. A apresentação
só recebe um quadro depois de validar todas as imagens, dimensões e relações.
Duplicidades, lacunas, replay, proprietário de outro processo, ciclos, IDs
ressuscitados e mudanças de PID/TID de um ID são recusados.

Cada imagem é desenhada separadamente pelo compositor convidado. Assim, uma
janela coberta por outra conserva seu conteúdo, inclusive fora da região visível
do desktop virtual. Não se recortam janelas a partir da imagem já achatada do
desktop. O caminho anterior de composição e seus hashes são preservados.

HWNDs reais ficam exclusivamente no apresentador. Mesmo um número convidado
igual ao valor de um HWND externo é tratado como ID opaco e recebe uma nova
janela própria. Relações de proprietário são traduzidas somente entre janelas
principais da mesma sessão. Proprietário que seja uma janela filha está fora
deste contrato e é recusado.

O título tem prefixo `[MiniNT]`. Popups sem barra de título recebem uma borda
azul de identificação, desenhada apenas na apresentação. Estilos nativos são
selecionados de uma lista limitada. A ordenação e a prioridade `topmost` do
convidado são aplicadas entre as janelas da sessão; não tornam a aplicação
globalmente superior às outras aplicações do host.

## Entrada, foco, tamanho e DPI

O apresentador possui uma thread de interface separada da execução da VM.
Arrastar/redimensionar uma janela no loop modal do Windows não bloqueia a thread
que executa o MiniNT. Uma caixa de quadros pendentes mantém o estado mais recente
e contabiliza quadros substituídos. Isso limita memória; janelas transitórias
que aparecem e desaparecem entre apresentações podem não chegar a ser exibidas.

Teclas físicas, texto Unicode, mouse, foco, blur, fechamento e geometria têm
formatos próprios. Cada evento identifica sua sessão e janela. O kernel resolve
o controle filho destinatário dentro daquela janela, preserva o scan code e
executa os callbacks no convidado. Blur atrasado de uma janela não limpa o foco
de outra. Eventos para janelas apagadas ou desabilitadas são descartados ou
recusados e contabilizados. Filas e limites excedidos encerram a execução com
erro observável.

Foco do convidado só é aplicado à interface nativa quando a janela atualmente
ativa do Windows já pertence àquela sessão. O apresentador não chama
`SetForegroundWindow` nem injeta teclado/mouse em outras aplicações. Não há
hooks globais. Texto comprometido é separado das teclas físicas, evitando uma
segunda tradução de caracteres pelo host.

O espaço lógico do convidado permanece em **96 DPI**. A thread de apresentação
usa percepção de DPI por monitor e converte coordenadas/tamanhos para **96–240
DPI** no host. `WM_DPICHANGED` ajusta a apresentação; movimento e tamanho do
cliente retornam ao MiniNT em coordenadas lógicas. Isto é virtualização de DPI,
não implementação de toda a API Windows de DPI por monitor no convidado.
O contexto de DPI alterado é apenas o da thread, sem mudar configurações do PC.

Fechar a janela nativa envia uma solicitação ao programa convidado. A janela
permanece até o programa a destruir, inclusive quando ele abre um diálogo de
confirmação. Modalidade mantém o proprietário nativo desabilitado e o restaura
quando o convidado o habilita. Minimização/restauração são operações de
apresentação; não se declara suporte completo a todos os estados Win32.

## Cotas e limites

| Recurso | Limite |
| --- | --- |
| Janelas convidadas, incluindo filhos | 32 por sessão |
| Cliente de uma janela principal | 1.280 × 720 pixels lógicos |
| Imagens de um quadro | 16 MiB somados |
| Quadros e comandos do protocolo | 4.096 quadros e 131.072 comandos por sessão |
| Taxa de apresentação | 512 transações de quadro e 128 MiB de imagens por segundo |
| Entrada | Fila nativa de 2.048 eventos e 4.096 eventos entregues por sessão |
| Identidades de janela ao longo da sessão | 4.096, sem reutilização após exclusão |
| Roteiro de testes | 2.048 ações, até 30 segundos |
| Capturas de diagnóstico | Até 16 imagens de cliente por execução |

Movimentos repetidos e geometria pendente podem ser combinados. Taxas de quadros
são limites de transações, incluindo flushes, e não uma garantia de FPS. A
memória de apresentação nativa também depende da escala de DPI, dentro do
intervalo admitido. Não há GPU passthrough, DirectX, composição transparente
completa, IME completo, clipboard, arrastar arquivos ou acesso ao Explorer.
As limitações de USER32/GDI/controles das fases 14.7 e 14.8 continuam valendo.

O CLI conserva seu orçamento de **1–30 segundos** por execução. Captura e
restauração de snapshots de RAM dessa família gráfica continuam recusadas.
O disco virtual de 64 KiB pode ser exportado e fornecido a uma nova sessão por
`--state`; ela recebe um novo namespace e novas janelas nativas. Não se
ressuscitam HWNDs ou recursos do processo anterior.

## Uso

O exemplo prepara o aplicativo próprio. Referências CRT/schema são entradas
locais legais e não acompanham as builds:

```powershell
python -B tools/integrated_runtime.py --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output runtime-12f
.\PASTA-BUILD\Release\guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-integrated.gk runtime-12f/runtime.gpv runtime-12f/manifest.json runtime-12f/binding.gnt sessao-12f --integrated --seconds 30
```

O aplicativo de teste próprio exige A, texto, clique, movimento/tamanho e M para
abrir seu diálogo; o roteiro de aceitação automatiza essa interação. Para o
Flappy, acrescente `--application CAMINHO-FLAPPY --asset CAMINHO-SETTINGS
--asset CAMINHO-ICON` à criação do pacote. A execução do arquivo externo é
sempre no MiniNT. A rede continua offline por padrão; uma política explícita da
14.9 pode ser fornecida separadamente.

`--window-script ARQUIVO` é um roteiro tipado do host, para testes reproduzíveis.
Não pode ser combinado com `--interactive` ou o `--script` do visualizador.
O formato é `milissegundos|título convidado|operação|argumentos`, por exemplo:

```text
300|12F Main|down|65|30|1
360|12F Main|up|65|30|0
450|12F Main|text|233
900|12F Main|resize|480|300
2400|@focused|close
```

Os títulos precisam ser únicos para seleção explícita; `@focused` seleciona a
janela principal do foco convidado. O relógio começa na primeira apresentação
nativa. As ações `scale` e `fail` exercitam caminhos sintéticos de DPI e falha;
não modificam a escala do monitor nem configurações do Windows. Não existe
operação de mensagem Windows arbitrária nesse arquivo.

`window-bridge.json` registra quadros, comandos, descartes e uma linha do tempo
de tipos/destinos dos eventos, sem seu texto ou códigos de tecla.
`native-windows.json` registra criação, destruição, geometria, DPI, coalescência
e erros. Capturas `native-window-*.ppm` usam `WM_PRINTCLIENT` somente nas
janelas próprias e permitem conferir conteúdo sem capturar o desktop pessoal.
O programa do host sempre recolhe suas janelas ao encerrar ou falhar.

## Aceitação

Os seis testes novos são `window_bridge_core`, `window_bridge_native`,
`window_bridge_runtime_tools` e as integrações `window_bridge_programs_integration`,
`window_bridge_flappy_integration` e `window_bridge_components_integration`.
Pacotes e roteiros inválidos são recusados antes de iniciar a VM. Uma pasta de
saída existente é recusada sem sobrescrever os relatórios da sessão anterior.

O núcleo verifica o protocolo, cotas, replay, relações, identidade de sessão,
desenho independente, recortes, janela fora do desktop, foco, entrada dirigida,
geometria e conversões de DPI. O teste nativo cria HWNDs reais de sessões
distintas, confere seus pixels, entrada, modalidade, minimizar/restaurar,
mensagens sintéticas de DPI, limpeza e preservação de uma janela externa de
controle. Injeções de falha após criação de HWND e alocação de DIB/DC verificam
recolhimento de recursos parciais; esses pontos de injeção só existem no teste
nativo, sem exposição no protocolo ou executável de produção. Um ID convidado com o valor de um HWND externo não recebe poder sobre
ele.

São **11 cenários MiniNT por configuração**: aplicativo próprio em duas sessões,
queda do convidado, watchdog, falha simulada da ponte, alternativa do visualizador,
três execuções do Flappy original e dois diálogos de controles, confirmado e
cancelado. O Flappy testa configurações alternativas, movimento, texto e efeito
dos comandos de voo. EXE, settings e ícone originais são conferidos antes/depois.
Os eventos automatizados usam APIs Win32 somente nas janelas próprias de teste;
isso não representa uma avaliação manual de todos os teclados, monitores ou
configurações de desktop.

O validador `tools/validate_phase12f.py` exige **139 testes de regressão Release
e 64 de aceitação Debug**, sem falhas ou ignorados e com fontes, binários e
referências estáveis. O pacote só é criado com essas provas e é testado outra
vez após extração e verificação de todos os arquivos/ZIP, nas duas configurações.
Inclui as regressões das fases anteriores. Resultados concretos e hashes ficam
nos manifestos e relatórios da build privada. Debug dos testes do host exige
também o runtime de depuração do Visual Studio. Linux não foi revalidado.

A 12F termina nesta matriz. A interface principal WinUI e a compatibilidade
gráfica mais ampla conservam seus próprios lotes no roadmap. A **14.10 não é
iniciada** por esta entrega.

Referências de contrato: [SetWindowPos](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowpos),
[contexto de DPI por thread](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setthreaddpiawarenesscontext),
[ajuste do frame para DPI](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-adjustwindowrectexfordpi)
e [WM_DPICHANGED](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged).
