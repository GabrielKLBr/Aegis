# Fase 14.7 — desktop gráfico e aplicativos externos

O kernel `guardian-kernel-desktop.gk` acrescenta o subsistema de janelas e as
DLLs próprias USER32, GDI32, GDI32FULL, WIN32U e DWMAPI ao MiniNT. A entrega é
limitada ao perfil Windows 10 x64 e às referências CRT fixadas na 14.5.
A execução do Flappy Window 1.5 fornecido pelo usuário integra a aceitação;
a resolução estática de seus imports não é usada como prova funcional.

## Execução e fronteira com o host

O programa, seus callbacks de janela, os temporizadores e as bibliotecas rodam
em CPL3 no convidado. O kernel valida PID/TID, identificadores, permissões,
coordenadas, recortes e tamanhos. A entrada de serviços gráficos é a porta A7
exposta pela DLL WIN32U própria; ela não permite executar funções no host.
O modelo mantém janelas, mensagens, pintura e entrada sob controle do MiniNT.

O host recebe somente pixels e metadados tipados. O novo visualizador apresenta
um desktop de 1280×720 dentro de uma única janela. As janelas do Flappy ainda
não se tornam janelas independentes do Windows. Essa integração pertence à 12F.
Handles HWND/HDC do host, endereços de funções e callbacks não atravessam a
fronteira. Não são instalados hooks globais, nem há entrada global, clipboard,
arrastar arquivos, acesso ao desktop externo ou aceleração GPU.

## Lote implementado

- Classes e janelas A/W, pais e proprietários, estilos básicos, posições,
  redimensionamento, coordenadas cliente/tela, ordem de apresentação, foco,
  títulos, área de pintura e limpeza no encerramento. BUTTON/STATIC próprios
  atendem os controles básicos utilizados pelo Flappy; não constituem um
  pacote completo de controles comuns.
- Filas por thread, mensagens postadas e enviadas entre threads do mesmo
  processo, execução do WndProc na thread destinatária e resposta sincronizada.
  Identificadores de outro processo e ciclos de parentesco são recusados.
- BeginPaint/EndPaint, atualização e invalidação, retângulos, linhas simples,
  texto, recorte, brushes, pens, fontes próprias e pequenos bitmaps. Objetos GDI
  selecionados são acompanhados e o acesso ao estado da DLL é sincronizado.
  A fonte bitmap Guardian Sans substitui as famílias solicitadas: as métricas
  e a aparência não equivalem às fontes TrueType do Windows.
- Estado escuro básico por DWM, dimensões e DPI fixo em 96. As demais operações
  DWM, efeitos de composição, regiões complexas e formatos gráficos fora do
  contrato falham explicitamente ou não são exportados.
- Teclas físicas down/up, scan code, modificadores e texto são eventos distintos.
  TranslateMessage gera caracteres no convidado. Texto Unicode explícito,
  mouse, perda de foco e fechamento seguem contratos separados; não há uma
  duplicação de WM_CHAR pelo visualizador do host. A tradução automática cobre
  o subconjunto de teclado descrito nos testes; layouts/IME completos ficam
  fora desta aceitação.
- Semáforos, pseudohandles duplicados, afinidade real da única vCPU,
  prioridades, suspensão e consulta/restauração do contexto de threads próprias.
  Uma restauração inválida é recusada antes de modificar o contexto. Registros
  de unwind encadeados adiante, emitidos pelo próprio MSVC, são aceitos com
  orçamento de travessia; ciclos não permitem execução indefinida.

## Arquivos e configurações

GPVs da 14.7 podem incluir arquivos de dados limitados a `C:\App`, com extensões
.txt/.ico/.dat/.bin e até 8 MiB por entrada. O executável e o runtime continuam
somente leitura. Arquivos pequenos de dados são copiados inicialmente para o
disco do convidado sem sobrescrever um arquivo persistido. Recursos maiores,
como o ícone do Flappy, são lidos do catálogo imutável por buffers validados.
Não há abertura de caminhos arbitrários do host em nome do convidado.

O disco mutável preserva os limites da 14.6: imagem de 64 KiB, 32 nós e até
512 bytes por arquivo. O novo leitor trata o fim do arquivo sem transformar
uma leitura além do último byte em erro de espaço. Configurações persistem
entre sessões por `security-state.bin`; estados curtos ou corrompidos são
recusados. Snapshots completos do desktop ainda não são suportados.

## Limites observáveis

Há 32 janelas, 16 filas de thread, 64 mensagens por fila, 128 comandos gráficos
por janela e 32 temporizadores. Janelas podem ter até 4096×4096, com coordenadas
limitadas e desenho recortado ao desktop. Bitmaps/imagens transferidos por GDI
estão limitados a 64×64; os ícones aceitos usam o subconjunto BMP de 32 bits.
Uma sessão tem limite de tempo e de saídas da vCPU. Excesso de recursos é erro,
e não uma confirmação de sucesso.

A sessão gráfica permite retenção explícita de até 65536 eventos no host.
O limite padrão e o contrato de políticas anteriores permanecem preservados;
a capacidade ampliada é selecionada antes de iniciar a sessão. Não há snapshot
com essa capacidade ampliada. Sessões longas ou intensas podem esgotá-la: os registros passam a indicar
perda e o resultado comportamental fica inconclusivo. A aceitação verifica
explicitamente ausência de perda em cada execução do corpus. Ela não prova
cobertura de todas as chamadas nem classificação completa de malware.

O MSVCRT original utilizado pelo Flappy ainda emite diagnósticos não fatais de
opções de heap não implementadas e de parâmetro do runtime C. O aplicativo
segue executando no corpus aceito; esses caminhos não são apresentados como
implementação completa do CRT. Áudio, menus completos, COM, WinRT, DirectX,
frameworks, x86 e compatibilidade geral permanecem nos lotes seguintes.

## Reprodução e critérios de aceitação

`tools/desktop_runtime.py bundle` cria o GPV com as DLLs próprias e referências
locais legais. `guardian-desktop` aceita `--interactive` ou um roteiro de
entrada com tempo contado desde a primeira consulta à fila de mensagens.
Esse marco evita entregar todos os controles durante uma inicialização lenta.
O pacote privado inclui exemplos e instruções; ele não contém os binários
Microsoft de entrada nem o Flappy original.

Os sete testes novos são:

| Teste | Evidência exigida |
| --- | --- |
| desktop_windows_demo | Classes, pintura, texto, coordenadas e temporizadores no Windows nativo. |
| desktop_windows_threads | Execução de callbacks e respostas na thread destinatária no Windows. |
| desktop_core | Propriedade de janelas, ciclos, filas/cotas, teclado, Unicode, recorte, timers, protocolos e unwind limitado. |
| desktop_viewer | Entrada tipada pela janela do visualizador, foco, fechamento e apresentação de pixels. |
| desktop_runtime_tools | Exports/forwarders, arquivos de dados, hashes e recusa de alterações no pacote. |
| desktop_runtime_integration | Aplicativos MSVC /MD, mensagens entre threads, contexto, configurações em duas sessões e estados inválidos. |
| desktop_flappy | Executável original em três sessões, texto desenhado, teclas físicas, janelas em movimento, configurações alternativas e encerramento. |

O SHA-256 fixado do Flappy é
`f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6`.
O teste altera apenas uma cópia de settings.txt: pássaro de 20 para 28 pixels,
canos de 80 para 64 e velocidade de 4 para 2, conferindo o efeito no convidado.
Os arquivos originais são verificados novamente depois do teste. O fechamento
respeita as janelas adicionais que o próprio jogo abre ao fechar seus elementos.

`tools/validate_phase14_7.py` exige os 116 testes de regressão em Release e
41 testes de aceitação em Debug, sem falhas ou ignorados, e verifica que fontes,
binários e entradas não mudaram durante a execução. Resultados, nomes dos testes
e hashes ficam nos arquivos validation.json. O empacotador recusa provas
incompletas ou desatualizadas. O smoke test verifica cada arquivo do ZIP e
executa o conteúdo empacotado nas duas configurações com entradas locais.

## Reavaliação da 12F

A matriz desta entrega fornece a base para iniciar uma 12F delimitada ao
Flappy e aos aplicativos próprios: carregamento de PE externo, callbacks no
convidado, múltiplas janelas, texto, entrada, encerramento e protocolo gráfico
validado. A decisão depende da aceitação Release/Debug registrada acima.

A 12F continua sem implementação. Ela deverá criar e limpar as correspondentes
janelas do host, validar isolamento entre sessões, foco por janela, modalidade,
DPI/redimensionamento e falhas da ponte. A prova com um visualizador único não
antecipa esses resultados. O início exige nova solicitação do usuário.

Contratos de referência: [WM_PAINT](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-paint),
[entrada de teclado](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-keyboard-input)
e [unwind x64](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).
