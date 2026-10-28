# 14.10E — GDI ampliado em janelas do host

Entrega delimitada para aplicações x64 no perfil **NT 10.0.19045**. O modo
**--expanded-gdi** acrescenta operações GDI à ponte nativa. Windows desenha
texto, fontes, curvas, regiões e bitmaps nas janelas da sessão; aplicação,
DLLs e callbacks permanecem no MiniNT. Imagens da aplicação podem atravessar
o contrato limitado. Uma janela rasterizada previamente no convidado não
conta como aceitação deste modo.

## Protocolo e contratos

Kernel guardian-kernel-expanded-gdi.gk, capacidade GuardianExpandedGdi/5/guest,
protocolo v5 e auxiliar guardian-expanded-gdi-host.exe. O manifesto vincula
kernel, volume, perfil, modo e SHA-256 do auxiliar. As variantes A–D continuam
separadas. Command (972 bytes), Reply (896) e Packet (3260) preservam o tamanho
anterior; magic/version distinguem a nova variante. Não há ponteiros,
procedimentos ou handles nativos do host no convidado.

A [matriz versionada](../config/gdi-api-matrix.json) contém **411 entradas**:
358 com implementação restrita, 31 pendentes e 22 recusadas por política.
Entradas incluem variantes e aliases internos em DLLs diferentes; isso não
significa 358 APIs universais ou implementações independentes. A matriz
registra ANSI/Unicode, encaminhamento, dependências e limites. O inventário
confere os exports reais e as lacunas, mas não substitui a execução dos testes.
O relatório registra separadamente o status da ponte e o retorno da API nativa.

MessageBeep permanece exportado por compatibilidade, retorna falha explícita
e registra diagnóstico; áudio é pendência da 14.10G. TranslateMessage no
convidado recebe entrada que o auxiliar já traduziu; síntese de caracteres
a partir de teclas postadas não é declarada. APIs pendentes sem export,
como wsprintfA/W, são recusadas no inventário antes da execução.

## Ampliação implementada

- Curvas: Ellipse, RoundRect, Arc, Chord, Pie, Polyline, Polygon, PolyBezier,
  variantes To e PolyPolygon; canetas com estilos e pincéis hachurados.
- Regiões: elipses/retângulos arredondados, combinação, deslocamento, limites,
  interseção com ponto/retângulo, recorte combinado, exclusão e deslocamento.
- Paths: início/fim/cancelamento, fechamento, preenchimento, contorno,
  conversão em região, recorte, achatamento e alargamento; leitura limitada.
- Estado: SaveDC/RestoreDC herdados preservam os estados nativos; modos de
  mapeamento, origens/extensões, conversão LP/DP, transformações afins,
  direção de arco, ROP2, modos de estiramento e cores de objetos DC.
- Raster: BitBlt/StretchBlt/PatBlt com ROPs enumeradas, SetPixel, DIBs BI_RGB
  de 24/32 bits em ambas as orientações, sincronização da memória da aplicação
  com o bitmap nativo e StretchDIBits SRCCOPY.
- Texto: variantes A em CP1252 e W em UTF-16 validado, alinhamento, espaçamento,
  justificação, ExtTextOut com recorte/opacidade/avanços, índices de glifos,
  extensões cumulativas, larguras ABC, métricas e descritores de fontes.
- Catálogo adicional: Tahoma, Verdana, Times New Roman, Courier New, Calibri,
  Cambria e Georgia, além das famílias anteriores. Pedidos permanecem no
  catálogo; face resolvida, métricas e glifos são os do Windows host.
- Cursores padrão são aplicados somente sobre clientes das janelas da sessão;
  arquivos de cursor e mudanças globais de exibição do cursor não são expostos.

GetObject traduz descritores; bmBits aponta para memória do convidado quando
aplicável. Ponteiros do bitmap nativo e bytes de padding não são copiados.
Seleções ativas e salvas continuam protegidas contra exclusão.

## Compatibilidade medida de criação e pintura

Exemplos independentes revelaram lacunas além dos exports GDI:

- CW_USEDEFAULT em janelas principais usa a política lógica do convidado:
  posição 120,100 e tamanho 640×480. Não representa toda a política de
  posicionamento/cascata de uma instalação Windows.
- SW_SHOWDEFAULT usa o padrão do convidado, SW_SHOWNORMAL.
- As combinações permitidas de CS_HREDRAW, CS_VREDRAW e CS_DBLCLKS correspondem
  a classes nativas pré-registradas no auxiliar.
- InvalidateRect e UpdateWindow têm caminhos distintos. A primeira invalida
  de forma assíncrona; a segunda apresenta a região já inválida. Uma pintura
  entregue fica pendente até EndPaint, preservando invalidações posteriores
  sem criar ciclos extras quando o programa invalida antes de BeginPaint.
  Se o host redimensiona durante um callback de pintura, classes com
  CS_HREDRAW/CS_VREDRAW recebem nova invalidação após a entrega de WM_SIZE
  ao convidado. Isso evita preservar desenho calculado com o tamanho antigo.
- Mudanças nativas consecutivas de posição e tamanho mantêm a última
  WM_WINDOWPOSCHANGED, WM_MOVE e WM_SIZE de cada janela enquanto aguardam o
  convidado. Isso evita esgotar a fila de 256 eventos ao arrastar uma janela
  durante um cálculo longo. Mensagens explicitamente postadas e envios com
  resposta não entram nessa combinação. Um teste força 400 alterações sem
  consumir eventos e confere a geometria final e a limpeza.

A regressão do modo 12F também corrigiu o roteiro de DPI: uma mudança de
escala usa a geometria nativa atual, mesmo antes de o convidado confirmar um
redimensionamento. O teste força duas trocas de DPI sem confirmação e confere
o tamanho lógico final em uma janela com moldura. A área cliente usa a
escala solicitada; as bordas usam o DPI real do HWND, que não muda quando o
teste envia uma troca sintética de DPI. Isso evita acrescentar pixels de
moldura à área cliente a cada transição. O programa integrado exige cliente 480×300 em
240,160 e DPI lógico 96; a contagem incidental de duas mensagens WM_SIZE
foi substituída por esse estado verificável e pelo recebimento de WM_SIZE.
O histórico nativo registra cada transição de DPI no momento em que ela
acontece, além dos quadros. Uma ida a 144 e volta a 96 antes do próximo quadro
não desaparece das evidências por combinação de quadros. O teste força essa
sequência sem confirmação intermediária do convidado.

O caso negativo de travamento usa três segundos para incluir a inicialização
a frio e exige timedOut, falha do convidado e primeira imagem sincronizada,
além da recuperação de todas as janelas.

A política guest-bounded-rectangles da 14.10D permanece. O desktop lógico é
1280×720 a 96 DPI. Versão do host, versão alvo e diferenças são registradas
separadamente; não se anuncia equivalência com todos os perfis NT, fontes,
temas e configurações de DPI.

## Limites e recusas

Mantêm-se 32 janelas/superfícies, 128 entradas GDI, 32 DCs, 24 MiB de pixels e
8 MiB enviados por sessão, além dos limites de menus/ícones/timers da 14.10D.
Bitmaps têm até 512×512; pontos e retângulos usam coordenadas limitadas.
Polys têm até 32 pontos por chamada e paths até 128 pontos; GetPath devolve
até 32. Regiões têm até 64 KiB de dados e limites de geometria. Transformações
devem ser finitas, limitadas e não singulares. Fontes têm dimensões e estilos
limitados; textos usuais têm até 127 unidades e ExtTextOut até 64.

Os ROPs aceitos estão enumerados no contrato. Tamanhos negativos e espelhamento
de bitmaps, formatos comprimidos/paletizados, fontes por caminho ou memória,
captura do desktop/outras aplicações, impressão e escapes de drivers ficam
fora do contrato. Texto dentro de um path aberto é recusado. AlphaBlend,
metafiles, GetFontData, enumeração irrestrita de fontes e outras lacunas estão
identificadas na matriz.

O auxiliar mantém token restrito, integridade baixa, Job com um processo e
64 MiB, dois endpoints herdados, sequência, autoria dos objetos e prazos.
Paths, regiões e buffers ampliam a superfície de ataque GDI; uma lista de
nomes não garante ausência de escape. O auxiliar não é AppContainer e seu
perfil não impede toda leitura de arquivos. Ver [SECURITY.md](../SECURITY.md).

## Corpus e evidências

A cena SDK própria compara seis páginas de pixels e métricas entre Windows
e MiniNT: formas, regiões, transformações, paths, raster 24/32 bits e texto.
Inclui entrada, redimensionamento, pintura finita, glifos, descritores e limpeza.

O corpus externo acrescenta Bezier e Clover, atribuídos a Charles Petzold,
compilados sem alteração dos fontes com o SDK Windows, em ANSI e Unicode.
O mesmo EXE é executado na referência nativa e no MiniNT; não é ligado contra
as bibliotecas de importação próprias. Comparam-se pixels do cliente em dois
tamanhos e mudanças por mouse no Bezier. O WhatSize é um caso negativo:
wsprintfA/W permanecem pendentes, sem substituir formatação por sucesso vazio.

[Origem e hashes dos exemplos](../config/gdi-corpus.json) fixam um espelho
público. O [site do autor](https://www.charlespetzold.com/books/) oferece os
fontes; o download do ZIP retornou HTTP 465 nesta execução, portanto não se
declara comparação de bytes com esse ZIP. Os fontes e binários desses exemplos
não são redistribuídos. Recibos de compilação vinculam fontes e EXEs utilizados.
O Flappy original permanece no MiniNT; não é executado no host.

**Critério final:** 167 testes Release de regressão, 92 Debug de aceitação,
sem ignorados e com fontes/binários estáveis; mais teste do ZIP extraído nas
duas configurações e verificação dos arquivos após a execução. O pacote
preserva as evidências de falhas de desenvolvimento. O laboratório loopback
reserva TCP e UDP em candidatos independentes, pois faixas reservadas pelo
Windows podem diferir entre protocolos. Corpus aprovado é um
recorte medido, não compatibilidade GDI geral.

## Uso

Alvo CMake: guardian_expanded_gdi_runtime. Fornecer CRT/schema legais e, para
reproduzir o corpus externo, fontes correspondentes aos hashes configurados:

    cmake -S . -B PASTA_BUILD -DGUARDIAN_GUI_COMPATIBILITY_ONLY=ON -DGUARDIAN_EXPANDED_GDI_CORPUS=PASTA_FONTES
    cmake --build PASTA_BUILD --config Release --target guardian_expanded_gdi_runtime

Acrescentar as opções de referências CRT, schema e Flappy das fases anteriores.

    python -B tools/expanded_gdi_runtime.py --build PASTA_BUILD --config Release --reference PASTA_CRT --schema-reference SCHEMA --output PACOTE_NOVO
    PASTA_BUILD/Release/guardian-desktop.exe PASTA_BUILD/guest/Release/guardian-kernel-expanded-gdi.gk PACOTE_NOVO/runtime.gpv PACOTE_NOVO/manifest.json PACOTE_NOVO/binding.gnt SESSAO_NOVA --expanded-gdi --seconds 30

A galeria guardian-expanded-gdi-scene.exe é o padrão deste criador. O CLI
mantém o prazo de 1–30 segundos; fechar a janela permite o encerramento normal.
Para Flappy, fornecer --application e os assets settings.txt/icon.ico.
Saídas devem ser novas. O teste do pacote requer também --corpus-binaries
apontando para a pasta externa que contém Release/Debug e seus recibos.

**Parar na 14.10E. DirectX/Direct2D/DirectWrite/codecs continuam na 14.10F,
dependendo de nova solicitação.**

