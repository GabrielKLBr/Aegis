# 14.10F — Direct2D, DirectWrite, WIC e texturas WARP

Entrega delimitada para **Windows 10 x64, NT 10.0.19045**, no modo
**--software-graphics**. A aplicação, suas DLLs e callbacks executam no MiniNT.
O Windows host desenha com Direct2D por software e DirectWrite, decodifica
imagens com WIC e processa texturas com D3D11 WARP em um auxiliar restrito.
Não há promessa de DirectX completo ou compatibilidade universal.

## Contrato e isolamento

Kernel guardian-kernel-software-graphics.gk, capacidade
GuardianSoftwareGraphics/6/guest, protocolo v6 e auxiliar
guardian-software-graphics-host.exe. O manifesto vincula perfil, kernel,
volume e SHA-256 do auxiliar. As variantes A–E preservam seus protocolos.
Command/Reply/Packet conservam 972/896/3260 bytes. IDs de objetos incluem
sessão e são verificados por processo, thread, tipo e parentesco. Ponteiros,
interfaces COM, callbacks e handles nativos não cruzam a ponte.
As interfaces COM do convidado seguem os tipos e a ABI do SDK.

O auxiliar mantém token restrito, integridade baixa, dois handles herdados,
um processo e as restrições de interface das fases anteriores. O Job limita
memória a 256 MiB e encerra o auxiliar se o supervisor sair. Comandos têm
prazo de dez segundos; desligamento normal tem cinco segundos. O limite do
Job inclui alocações internas, que não são totalmente representadas pela
contabilidade de pixels.

A superfície de ataque inclui os componentes Direct2D, DirectWrite, WIC e
WARP do Windows. O auxiliar não é AppContainer e esse token não impede toda
leitura de arquivos pessoais. Os testes não provam impossibilidade de escape
ou contenção suficiente para executar malware real com segurança.

## Recursos implementados

- **Direct2D:** fábrica de thread única, render target de HWND ou DC de
  janela própria; limpeza, pincel sólido/cor/opacidade, linhas, retângulos,
  elipses, transformação afim, recorte, texto e bitmaps WIC. Erros diferidos
  aparecem em EndDraw. BindDC cobre a área inteira do DC autorizado, a 96 DPI.
  Não aceita DC de desktop ou memória arbitrária. Resize segue o cliente
  real limitado. Uma pintura mantém seu DC até EndPaint quando o host
  redimensiona durante o callback.
- **DirectWrite:** fábricas SHARED/ISOLATED com objetos próprios; formato,
  família/peso/estilo/stretch, alinhamento e quebra de linha básicos.
  O auxiliar usa fábrica isolada e fontes nativas do catálogo permitido.
  Texto até 127 unidades UTF-16, família/locale até 31 e fonte de 1 a 96.
  Layouts, fontes personalizadas e análise tipográfica permanecem pendentes.
- **WIC:** ativação das fábricas conhecidas dentro do convidado, streams
  copiados e leitura somente do volume convidado. PNG/BMP/JPEG de um quadro,
  tamanho/formato/resolução, conversão BGRA/PBGRA e CopyPixels limitado
  com retângulo, stride e padding. Origens do conversor: 24bpp BGR/RGB,
  32bpp BGRA/PBGRA/RGBA e 8bpp indexed. Quadros sobrevivem à liberação do
  decoder/stream. Sem scaler, encoders, GIF/TIFF ou múltiplos quadros.
  O auxiliar carrega windowscodecs.dll do System32 e obtém diretamente as
  fábricas internas enumeradas; não ativa extensões pelo registro.
- **D3D11:** WARP com feature level real 9_1; texturas zeradas BGRA8/RGBA8,
  um mip/array, sem MSAA, RTV, limpeza, cópia, staging de leitura/Map/Unmap,
  Flush, query EVENT e consulta de conclusão. Binding de um RTV, viewport
  e ClearState. Sem buffers de vértices, shaders, Draw, pipeline 3D ou
  inicialização por D3D11_SUBRESOURCE_DATA. O nível 9_1 identifica o
  dispositivo nativo escolhido; não declara todo o contrato D3D desse nível.
- **DXGI:** fábrica/interface base, adaptador lógico de software, associação
  a janela própria e swapchain lógico de um buffer, em janela. GetBuffer
  fornece textura do convidado; Present copia os pixels limitados à janela
  nativa. Sem flip, sincronização com monitor, fullscreen, compartilhamento,
  outputs do host ou GPU passthrough. ResizeBuffers recusa referências
  diretas/indiretas ao buffer. Prioridade de GPU admite apenas o padrão zero.

D3D9 retorna null/D3DERR_NOTAVAILABLE. D3D12/OpenGL permanecem sem backend;
imports ausentes são recusados antes do boot. CreateDXGIFactory1/2 só podem
fornecer a interface base; IIDs novos são recusados. Métodos HRESULT ausentes
retornam E_NOTIMPL; operações void ausentes registram diagnóstico e falham
no EndDraw/estado do dispositivo. Retorno neutro de recusa escalar não conta
como implementação.

## Cotas, cobertura e registros

Até 64 recursos de software por sessão, 128 wrappers COM no convidado,
16 MiB de dados contabilizados, 1 MiB por imagem codificada, imagens/texturas
até 512×512 e cliente até 1280×720 a 96 DPI. Mantêm-se as cotas GDI herdadas.
Rejeitam-se campos reservados, formatos/flags desconhecidos, texto inválido,
NaN/infinito e chunks não canônicos. Números são validados por ordenação
inteira IEEE-754, sem depender do modo FP do convidado.

software-graphics.json separa status de transporte e apiHresult e registra
recursos, bytes e uso efetivo: máscara 1 Direct2D, 2 DirectWrite, 4 WIC,
8 WARP. Isso não significa cobertura integral. analysis.json mantém a
telemetria herdada; não há classificador de malware novo nesta fase.

A [matriz COM](../config/software-graphics-api-matrix.json) contém 465 slots
de 21 interfaces, incluindo métodos herdados repetidos e recusas. Não é uma
porcentagem de compatibilidade. Exports reais e execução são evidências
separadas. HeapSetInformation permanece recusada no runtime anterior; a
amostra independente ignora essa falha conforme seu código original.

## Validação e uso

SoftwareGraphicsScene.cpp usa headers/imports normais do SDK. O mesmo EXE
executa nativamente em pasta sem DLLs convidadas e no MiniNT. Comparam-se
pixels completos do cliente antes/depois de resize, texto acentuado, hashes
dos pixels PNG/BMP/JPEG e da textura WARP. Também se exercitam queries,
identidade COM, referências do swapchain e liberação após fechar a janela.

A amostra Microsoft Direct2DHelloWorld é compilada sem alterar os fontes
fixados no [corpus](../config/software-graphics-corpus.json); o mesmo EXE
é comparado nativamente e no convidado. Fontes/EXEs externos não são
redistribuídos. Esse resultado não é extrapolado a todos os programas.

Testes negativos cobrem parâmetros, thread, recursos estrangeiros, cotas,
imagem inválida, hardware, níveis ausentes, shaders/Draw/scaler, interfaces
novas, D3D9, erros diferidos e limpeza. Ferramentas verificam imports
estáticos/delay/forwarders/ordinais, API sets, ciclos, modos e identidade do
auxiliar. Exigem-se a regressão herdada e testes do ZIP extraído nas duas
configurações, com hashes conferidos após execução.

Crie o volume com tools/software_graphics_runtime.py usando --build,
--config Release, --reference CRT-LEGAL, --schema-reference SCHEMA-LEGAL,
--output BUNDLE-NOVO e três --asset apontando para image.png/image.bmp/image.jpg
de BUILD/software-assets/Release. Execute BUILD/Release/guardian-desktop.exe
com kernel BUILD/guest/Release/guardian-kernel-software-graphics.gk,
BUNDLE-NOVO/runtime.gpv, manifest.json, binding.gnt, SESSAO-NOVA e
--software-graphics --seconds 30.

Fontes vão ao Aegis e builds somente ao Aegis-builds privado. A **14.10G**
permanece planejada; parar após a entrega F.
