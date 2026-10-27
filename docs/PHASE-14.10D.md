# 14.10D — Controles, diálogos e aceitação da GUI nativa

Entrega delimitada para aplicativos x64 no perfil NT 10.0.19045. O modo
**--native-controls** usa janelas, controles e GDI reais do Windows host.
O aplicativo, suas DLLs e seus callbacks continuam no MiniNT; o EXE externo
não é carregado no host. As variantes A/B/C e o apresentador 12F são preservados.

## Contrato e funções

A variante usa guardian-kernel-native-controls.gk, capacidade
GuardianNativeControls/4/guest, protocolo v4 e guardian-native-controls-host.exe.
O CLI verifica kernel, modo e SHA-256 do auxiliar. USER32/GDI32/WIN32U próprios
e forwarders próprios COMDLG32, COMCTL32 e DWMAPI atendem os exports declarados
nas tabelas NativeControlsUser.def, NativeControlsGdi32.def e NativeControls-*.def.

- LISTBOX e COMBOBOX nativos, com strings, seleção e mensagens mediadas.
- Menus de texto, submenus de um nível e notificações nas janelas da sessão.
- STATIC com ícone; ícones padrão e ICO limitado a DIB 32 bits até 64×64.
  A decodificação ocorre no convidado; CreateIconIndirect recebe um bitmap
  validado no auxiliar. Esses ícones são tratados como opacos neste lote;
  PNG e todas as combinações de transparência do ICO não são declarados.
- WM_SETFONT/WM_GETFONT nos controles e WM_SETICON/WM_GETICON nas janelas.
  Objetos selecionados ficam retidos até a liberação da referência.
- CreateFontA/CreateFontIndirectA e TextOutA com conversão CP1252. O catálogo
  admite Arial, Consolas, Segoe UI, Sans Serif, MS Shell Dlg, MS Shell Dlg 2 e
  seleção padrão. A face resolvida e as métricas são as do Windows host.
- MessageBoxA/W e um recorte GetOpenFileNameA/W, com controles nativos e
  modalidade/callbacks no MiniNT.
- Desktop lógico de 1280×720, cores/pincéis de sistema, estilos estendidos
  permitidos, foco da sessão e atributo de tema escuro.

Declarar um export não significa aceitar todas as suas opções. Mensagens
arbitrárias, owner draw e ponteiros do convidado não são enviados a controles.

## Arquivos, modalidade e apresentação

O seletor é um diálogo de entrada de caminho no namespace convidado. Usa
CreateFile do MiniNT para abrir arquivos existentes; não utiliza o navegador
Shell nem os discos pessoais do host. Cancelamento e erro são distintos,
com CommDlgExtendedError e validação dos buffers. Hooks, templates, seleção
múltipla, salvamento e navegação Shell completa ficam fora deste recorte.

Os diálogos desabilitam o proprietário e restauram seu estado ao encerrar.
Ponteiros de procedimentos e estado da aplicação permanecem no convidado.
Clipboard, drag/drop e ativação arbitrária de COM/Shell não são liberados.

A visão lógica usa 96 DPI. O auxiliar cria, move, dimensiona e pinta os HWNDs
reais. Sua política explícita guest-bounded-rectangles impede que mínimos
automáticos do gerenciador do host ampliem um retângulo solicitado pelo
convidado, inclusive antes de WM_NCCREATE e durante movimentação. Isso atende
às pequenas janelas do Flappy. A referência SDK aplica a mesma política e
confere criação e movimento. Não se declara equivalência universal com o
gerenciamento de janelas de uma instalação Windows; callbacks gerais de
WM_GETMINMAXINFO/WM_WINDOWPOSCHANGING não recebem ponteiros do host.

Texto, fontes e formas são desenhados por GDI no host. Bitmaps/ícones da
aplicação podem atravessar o contrato limitado; a janela não é rasterizada
previamente no MiniNT. Fallback da 12F não conta como aceitação nativa.
Versões do Windows host e do NT alvo são registradas separadamente. Fontes,
substituições, temas e DPI podem variar; as comparações valem para a máquina
registrada, sem equivalência automática entre todas as versões NT.

## Limites e isolamento

Até 32 janelas/superfícies, clientes de 1280×720, 128 entradas GDI, 32 DCs,
24 MiB de pixels e 8 MiB enviados por sessão, mantendo os limites de bitmap e
texto da 14.10C. Há oito menus, 32 itens por menu e 32 ícones; menus, ícones e
timers compartilham um teto de 64 entradas. Textos USER32 têm até 255 unidades
UTF-16; textos GDI, até 127.

IDs são vinculados à sessão, processo e thread. HWND/HDC/HFONT/HICON reais,
ponteiros e procedimentos do host não atravessam o canal. Recursos em DCs,
controles ou estados salvos não podem ser apagados enquanto referenciados.
O encerramento remove as janelas/controles antes dos objetos associados.

O auxiliar mantém token restrito, integridade baixa, Job de um processo,
64 MiB privados e dois endpoints herdados. As esperas do protocolo D usam
timers anônimos de alta resolução por thread, preservando tamanho, sequência,
monitor de consumo, timeout e encerramento. A resolução global não é alterada.
Essas medidas não garantem ausência de escape: o auxiliar não é AppContainer
e seu perfil não impede toda leitura de arquivos. Ver [SECURITY.md](../SECURITY.md).

## Aceitação e uso

Critério: **158 testes Release de regressão e 83 Debug de aceitação**, sem
ignorados e com fontes/binários estáveis, mais teste do ZIP extraído. Os seis
testes novos cobrem protocolo, auxiliar real, referência SDK, integração,
Flappy e rejeições de execução/empacotamento.

O corpus inclui uma aplicação de controles/menus/seletor, um editor ANSI,
threads, a cena GDI da 14.10C e o Flappy original. As mesmas fontes SDK de
controles/editor e a cena GDI também executam como referências nativas,
comparando resultados, fontes, métricas, pixels e geometria.

Os cenários incluem confirmação/cancelamento, entrada nos controles nativos,
modalidade, arquivo convidado e recusa de um arquivo existente somente no
host, redimensionamento, concorrência, encerramento, queda, prazo e morte
do auxiliar. Autoria, tipos inválidos, seleção, cotas, falhas parciais e
vazamentos são verificados.

Flappy permanece inalterado, com SHA-256
f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6.
São três sessões: configuração normal, alternativa e normal. Conferem-se
janelas independentes, movimento, teclas, texto do placar capturado da
própria janela e limpeza. EXE/assets originais não são redistribuídos.
Diagnósticos CRT anteriores, como HeapSetInformation sem suporte, permanecem
registrados e não equivalem a um runtime Windows completo.

Relatórios native-controls.json, boot.json e analysis.json, referências e
capturas próprias acompanham as evidências. Erros de desenvolvimento ficam
separados da matriz final.

Na regressão Windows, o teste antigo de vínculo de pacotes passou a usar
aplicação, DLL e kernel compilados do projeto: o container PE artificial que
ele criava foi bloqueado pelo Defender (erro Win32 225). A proteção permanece
ativa; os casos específicos do parser continuam sintéticos. Também foi
acrescentada uma espera limitada pela primeira apresentação visível da 12F,
para que o convidado não avance enquanto suas primeiras janelas são criadas;
os quadros seguintes mantêm a coalescência assíncrona.

    python -B tools/native_controls_runtime.py --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output pacote-controles
    PASTA-BUILD/Release/guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-native-controls.gk pacote-controles/runtime.gpv pacote-controles/manifest.json pacote-controles/binding.gnt sessao-nova --native-controls --seconds 30

Para Flappy, fornecer --application CAMINHO-FLAPPY e --asset para settings.txt
e icon.ico. Usar saídas novas. Alvo CMake: guardian_native_controls_runtime.

USER32/GDI universais, todos os controles, diálogos Shell completos, fontes
arbitrárias e todos os perfis NT não são declarados. A expansão GDI segue na
14.10E; DirectX e mídia/áudio ficam na 14.10F/G.

**Parar ao concluir a 14.10D. A 14.10E exige nova solicitação.**
