# 14.10C — Desenho GDI nativo

Entrega delimitada de Windows 10 x64 no MiniNT. O modo `--native-gdi` acrescenta
`gdi32.dll`, `user32.dll` e `win32u.dll` próprios, protocolo v3 e kernel com
capacidade `GuardianNativeGdi/3/guest`. O processo auxiliar usa GDI real do
Windows para executar os comandos. As modalidades 12F, 14.10A e 14.10B continuam
com seus contratos próprios.

## Desenho e recursos

O conjunto implementado está em `guest/fixtures/NativeGdi32.def` e
`NativeGdiUser.def`. Inclui DC de janela e memória, BeginPaint/EndPaint,
brushes sólidos, pens sólidos, fontes permitidas, DIBs, seleção/liberação,
SaveDC/RestoreDC, regiões retangulares, clipping, FillRect, Rectangle,
MoveToEx/LineTo, TextOutW, DrawTextW, cores/modo de fundo, métricas e extensão
de texto, BitBlt, StretchDIBits, GetPixel, GdiFlush e capacidades declaradas.

As chamadas operam sobre HDC/HGDIOBJ reais **dentro do auxiliar**. O convidado
recebe IDs opacos ligados à sessão, processo e thread. Não recebe HWND, HDC,
ponteiro de pixels, procedimento ou endereço real do host. O gateway não oferece
uma operação genérica de chamar funções, carregar DLLs ou abrir dispositivos.

Cada janela customizada possui uma superfície de memória desenhada por GDI no
auxiliar. WM_PAINT e WM_PRINTCLIENT apresentam essa superfície por GDI; o
redimensionamento preserva o conteúdo existente. A cópia para o HWND também
ocorre no host. Texto, linhas e formas são produzidos pelo Windows, sem enviar
uma janela previamente rasterizada pelo MiniNT.

Bitmaps de aplicação podem atravessar o canal em blocos limitados. O exemplo
transfere apenas um bitmap de 16×16 para a operação StretchDIBits. O restante
da cena é produzido pelas chamadas GDI. Não há conversão silenciosa para o
apresentador de pixels da 12F.

GetDC e BeginPaint aceitam apenas janelas customizadas da própria thread e
sessão; DC de desktop, janela externa ou controle nativo não é fornecido. Há
uma aquisição de DC de janela ativa por janela. DCs de memória são independentes.
Uma aquisição BeginPaint possui o par EndPaint e uma aquisição GetDC usa
ReleaseDC. O par incorreto retorna erro.

Objetos selecionados em DCs ou estados salvos não podem ser apagados. Bitmaps
de aplicação não podem ser selecionados simultaneamente em dois DCs. A
restauração devolve a seleção e o clipping, e a exclusão de DC restaura seus
objetos iniciais. Objetos de stock são representados por IDs próprios.

## DIBs, texto e DPI

O lote aceita DIB de 32 bits BI_RGB, top-down, sem seção de arquivo ou paleta.
CreateDIBSection retorna um buffer alocado **no convidado**. Escritas nele são
sincronizadas antes do uso pelo GDI; GdiFlush copia de volta as alterações de
GDI para permitir leitura do buffer convidado. O programa precisa usar esse
ponto de sincronização antes de ler pixels modificados pelo GDI e antes de
intercalar acesso direto com desenho. Escritas concorrentes sem sincronização
não têm garantia de coerência.

Fontes criadas pelo aplicativo aceitam os nomes Consolas, Arial e Segoe UI,
altura de −8 a −64, peso de 100 a 900 e qualidade sem antialiasing. Fontes de
stock declaradas também estão disponíveis. Nenhum arquivo de fonte é carregado
por caminho fornecido pelo convidado. A face efetivamente resolvida e as
métricas são devolvidas pelas APIs. Os modos e limites são explícitos no
validador do protocolo; flags de DrawText que modificam o buffer são recusadas.

A visão GDI usa coordenadas e capacidades lógicas de 96 DPI. O auxiliar escala
a apresentação para o HWND físico. O relatório registra a versão real do
Windows host e o perfil NT alvo 10.0.19045. A comparação de pixels/métricas vale
para a mesma máquina e a configuração registrada; ela não comprova equivalência
entre versões Windows, instalações de fontes ou topologias de monitores.

Os contratos seguem, dentro desse recorte, a documentação de
[contextos e estados GDI](https://learn.microsoft.com/en-us/windows/win32/gdi/device-context-functions),
[DIB sections e sincronização](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection)
e [pintura da área cliente](https://learn.microsoft.com/en-us/windows/win32/gdi/drawing-in-the-client-area).

## Limites e supervisão

- Até 128 entradas de objetos, incluindo stock, e 32 DCs.
- Até oito superfícies de janela; cada cliente pode ter até 1280×720 pixels lógicos.
- Bitmap de aplicação de até 512×512; 24 MiB para pixels das superfícies e bitmaps.
- Transferência em blocos de 128 pixels e até 8 MiB enviados por sessão.
- Até oito estados salvos por DC, textos de até 127 unidades UTF-16 e operações
  SRCCOPY para cópia/escala do lote.
- Herança de dois endpoints, token restrito, integridade baixa, Job de um
  processo e 64 MiB privados, mantendo a supervisão da 14.10A/B.

O perfil do auxiliar não é AppContainer nem impede toda leitura de arquivos
pelo processo. A superfície de ataque acrescentada pelas APIs nativas continua
documentada em [SECURITY.md](../SECURITY.md).

O kernel informa o fim de threads e só ele conclui o canal da sessão. DCs,
objetos e superfícies são liberados no encerramento. Quedas, prazo da VM ou
auxiliar travado encerram o Job. O arquivo `native-gdi.json` registra autores,
operações USER32/GDI, status, contadores, recursos restantes, bytes de bitmaps,
versões NT e resultado do processo auxiliar.

## Aceitação e uso

O critério da entrega exige **152 testes Release de regressão e 77 Debug de
aceitação**, sem ignorados e com fontes/binários estáveis. Os cinco testes novos
cobrem aplicação Windows de referência, protocolo, processo GDI real, integração
no MiniNT e rejeições de execução/empacotamento.

A mesma fonte de aplicação SDK executa no MiniNT e no Windows nativo. A cena
de 160×112 compara o hash de todos os pixels, métricas, extensão, face da fonte
e retângulo calculado por DrawText. Também exercita BeginPaint/EndPaint,
BitBlt para a janela, redimensionamento e liberação explícita dos recursos.
Os testes incluem captura da própria janela do auxiliar, IDs inválidos e
estrangeiros, autoria, objetos selecionados/salvos, cotas, falhas e contagem
de handles GDI antes/depois de 32 ciclos de alocação. Falhas parciais de
criação de DC e o limite total de bytes de bitmaps também são exercitados.

O pacote extraído repete os cenários gráficos anteriores e os novos em Release
e Debug. As evidências, incluindo erros encontrados no desenvolvimento, acompanham
a build privada. Não são distribuídos DLLs Microsoft de entrada, Flappy, ISOs,
discos ou estados pessoais.

```text
python -B tools/native_gdi_runtime.py --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output pacote-gdi
PASTA-BUILD/Release/guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-native-gdi.gk pacote-gdi/runtime.gpv pacote-gdi/manifest.json pacote-gdi/binding.gnt sessao-nova --native-gdi --seconds 25
```

Usar diretório de saída novo. Alvo agregado CMake: `guardian_native_gdi_runtime`.
O auxiliar deve ficar ao lado de guardian-desktop; o manifesto fixa seu SHA256.

GDI e USER32 universais não são declarados. Paletas, impressão/dispositivos,
DCs externos, formatos comprimidos, ROPs gerais, paths, transformações, fontes
arbitrárias, diálogos completos, clipboard, DirectX e WinRT não pertencem ao
recorte desta entrega. A aceitação do Flappy original por esta ponte nativa,
controles adicionais e integração dos diálogos ficam na 14.10D.

**Parar ao concluir a 14.10C. A 14.10D depende de nova solicitação.**
