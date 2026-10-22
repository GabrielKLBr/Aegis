# Fase 11 — Gráficos, entrada e janelas

Iniciada em 18/10/2026 por solicitação explícita do usuário. O produto continua
Windows x64/MSVC/WHP; o desenvolvimento e os testes Linux usam ELF/KVM nativos,
sem Wine ou QEMU. A fase 12 não está autorizada nem foi iniciada.

## Correções Windows aceitas em 19/10/2026

As oito falhas de Windows foram corrigidas. A suíte completa aprovou **52/52
em Debug e 52/52 em Release**, sem ignorados. O resultado oficial é `passed` /
`awaiting_linux_acceptance`: a revisão corrigida ainda precisa de revalidação
Linux com os mesmos hashes. A fase 12 não foi iniciada.

Correções, novos testes, requisitos do auxiliar de memória e limites dos
checkpoints: [relatório das correções](CORRECOES-WINDOWS-FASE11-20261019.md).
O [diagnóstico anterior](VALIDACAO-WINDOWS-FASE11-20261019.md) foi preservado.
As seções seguintes registram a revisão Linux aceita em 18/10; suas pendências
Windows representam aquele momento.

## Aceitação final no Linux

**Implementação da matriz própria concluída; Linux/KVM aprovado em Debug,
Release e Sanitize. Aceitação nativa Windows/MSVC/WHP pendente. A fase 12 não
foi iniciada.**

Evidência: `build-phase11/validation-20261018-191149-4baab81b/validation.json`.
Estado `passed` / `awaiting_native_windows_acceptance`. As fontes foram
recompiladas em diretórios novos e seus hashes permaneceram iguais até o fim.
Não houve Wine/QEMU, binários reaproveitados ou testes ignorados.

| Configuração | CTests | Boots/retomadas gráficos | Pausas gráficas | Suíte |
| --- | --- | --- | --- | --- |
| Debug | 32 aprovados | 15 | 3 | 433,33 s |
| Release | 32 aprovados | 15 | 3 | 305,55 s |
| Sanitize (ASan/UBSan) | 32 aprovados | 15 | 3 | 534,43 s |

Totais: **96 execuções CTest, 132 boots antigos de regressão, quatro controles
fatal/IST corretamente recusados como sucesso, 45 boots/retomadas gráficos e
nove pausas gráficas**. Cada configuração inclui 9136 verificações do núcleo
GUI e 20000 comandos hostis, oito pixels independentes e hash completo
conferidos pelo oráculo Python, clonagem/retomada/roteiro vinculado ao snapshot
e o visualizador nativo de software. Nenhum alerta de ASan/UBSan.

A matriz antiga permaneceu aprovada: armazenamento e recuperação de 52
prefixos de falha, catálogo e suas transações interrompidas, 81 boots/retomadas
de snapshots do console e 27 pausas, 24 boots de seu gerenciador, 72 boots/retomadas
de rede e suas capturas IPv4/IPv6. Esses valores estão identificados nos
relatórios por configuração e não substituem a aceitação nativa Windows.

Foram preparados 19 módulos Windows com LLVM ELF produzindo COFF e fontes
idênticas às aceitas. Não houve execução Windows. Para encerrar formalmente,
rodar `python tools/validate_phase11.py` no Windows x64/MSVC/WHP: o gate exige
52 CTests em Debug/Release, inclusive as comparações Microsoft de 153 handlers
e 59 casos de unwind. A aceitação entre hosts só é combinada com hashes de
fontes correspondentes.

Os marcos intermediários abaixo são históricos; os resultados desta seção
constituem a aceitação Linux final.

## Perfil implementado

A referência da fase 0 é uma aplicação gráfica própria. Esta implementação
oferece uma aplicação própria em CPL3 com duas ferramentas/janelas: Paint
desenha ao clicar, e Notes mostra
o último caractere recebido pelo teclado. As duas ferramentas usam a API gráfica explícita do
projeto; não importam USER32/GDI e não comprovam compatibilidade binária com
aplicações gráficas arbitrárias do Windows. A matriz é deliberadamente pequena:
bitmap BGRA opaco 320×240, rasterização por software, quatro janelas, texto ASCII
com fonte 5×7 própria (letras/dígitos/espaço; minúsculas desenhadas como
maiúsculas; demais imprimíveis usam o glifo de substituição `?`), retângulos,
pequenos bitmaps, foco, ordem de sobreposição,
cursores internos, diálogo modal e DPI lógico 96/144/192. Não há GPU anunciada.

Gerenciador e rasterizador ficam no kernel convidado. Syscalls validam e copiam
comandos de 160 bytes e retornam eventos de 32 bytes dentro da página de dados
de usuário; ponteiros de kernel, overflow e dados fora do intervalo são recusados.
O programa CPL3 cria as janelas, desenha e processa eventos reais do canal.
Não são imagens desenhadas antecipadamente pelo host.

O dispositivo versão 1 usa a porta 0xF3 e páginas privadas do kernel:
pacote 0x135000, enumeração somente leitura 0x136000, relatório 0x137000 e pixels
0x140000–0x18AFFF (307200 bytes). O host valida sessão, sequência, dimensões,
stride, tamanho, opacidade, títulos ASCII limitados, IDs, geometria, foco e
parentesco modal antes de apresentar uma cópia. Não recebe HWND, callbacks,
ponteiros ou solicitações para operar janelas externas.

Cada janela mantém no máximo 32 operações de desenho. Um preenchimento de todo
o cliente inicia uma nova lista; demais excessos retornam Full. Existem 16
eventos, 256 criações de janela por sessão, 8192 trocas de dispositivo e
30 segundos no visualizador interativo. Esses limites são explícitos; não são
promessas de executar programas ou sessões longas.

## Canal interativo

`guardian-gui` abre uma única janela nativa controlada. Windows usa uma janela
Win32 e StretchDIBits; Linux usa SDL2 com renderizador de software. O compositor
convidado desenha suas janelas dentro dessa superfície. Entrada só é aceita
para o identificador dessa janela e enquanto ela detém foco; coordenadas são
convertidas para o desktop virtual. Perda de foco limpa entrada pendente no
convidado. Não há hook global, captura global, movimento artificial do cursor,
injeção de teclas no host nem encaminhamento de títulos para novas janelas reais.

Clipboard, arrastar arquivos, impressora, USB, unidades compartilhadas e acesso
a janelas do host possuem operações separadas e retornam Denied. A máscara de
permissões é zero; ativação desses recursos ainda não está implementada.
Teclado suporta ASCII e Backspace/Tab/Enter/Escape; não há IME, Unicode geral,
leiautes internacionais, atalhos globais, captura do mouse ou arraste de bordas.
Cursores são desenhados na própria superfície. DPI da API é lógico; não anuncia
paridade completa com DPI awareness ou USER32 do Windows.

## Execução

Linux, após compilar imagens e ferramentas:

```sh
./guardian-gui guardian-kernel-graphics.gk evidencias
./guardian-gui guardian-kernel-graphics.gk evidencias --interactive
```

O primeiro comando executa a sequência de referência com entrada explícita e
salva `boot.json`, `desktop.ppm` e `graphics.json`. O segundo aceita interação
somente na janela criada. Enter ou um clique dispensa o diálogo; clique no Paint
para desenhar e no Notes para dar foco antes de digitar. Escape ou fechar a
janela termina. A sessão que excede 30 segundos é diagnosticada como timeout.

No Windows, o mesmo executável é construído pelo CMake/MSVC; precisa da
validação nativa desta revisão. No Linux, SDL2 em tempo de execução e seus
headers de desenvolvimento habilitam o visualizador. Sem os headers, a execução
gráfica sem janela continua disponível, mas o gate completo da fase 11 recusa
uma suíte sem o teste do visualizador. O cache de desenvolvimento usado nesta
máquina fica fora do projeto, em `~/.cache/guardian-native-deps/sdl2`.

## Gerenciador e snapshots

`guardian-vm create ID IMAGE graphics` admite o novo perfil sem rede.
O worker executa a sequência de referência sem janela, com histórico separado
em `graphics-history/<token>` e cota de 64 sessões. Snapshots guardam RAM do
compositor, pixels, sequência e posição da entrada determinística. Restauração
exige exatamente o mesmo roteiro; clone e retomada produzem o mesmo bitmap.
O formato dos perfis antigos permanece compatível.

Snapshots interativos são recusados explicitamente: eventos externos ao
checkpoint não têm uma semântica de replay implementada. A janela interativa
é iniciada diretamente por `guardian-gui`; não se promete uma janela conectada
a um worker em segundo plano. Novos perfis combinando GUI, PE, rede e disco
exigem integração adicional; o contrato `graphics` atual é distinto.

## Validação

O gate exige builds novos Debug/Release e host com ASan/UBSan,
32 CTests por configuração Linux (52 no Windows), as 132 inicializações antigas,
os controles fatal/IST e a preservação dos testes de rede, processos, disco,
catálogo e snapshots. Testes gráficos incluem entrada inválida, clipping, quotas,
vida útil, modal/foco, ponteiros, 20000 comandos hostis, pixels e hash verificados
por código Python independente, onze boots/retomadas e três pausas por
configuração (incluindo dois fechamentos antecipados), mais três boots gerenciados e uma execução do programa de referência.

A pré-compilação de ramos Windows no Linux é preparação, não aceitação MSVC/WHP.
Os resultados Linux finais estão no início deste documento; o gate Windows permanece pendente.

## Recuperação e verificações de desenvolvimento

As 350 fontes/documentos originais foram copiados antes de qualquer alteração
para `snapshots/codex-before-phase11-20261018`, com manifesto SHA-256.
Os pacotes e evidências das fases anteriores foram preservados. Logs de
compilação e testes preliminares ficam em `build-phase11/development-logs`;
eles não substituem a aceitação reconstruída.

Os prechecks demonstraram as duas ferramentas CPL3, nove boots/retomadas e três
pausas com pixels iguais, três execuções gerenciadas e recusa de roteiro de
entrada diferente. Os testes foram ampliados para fechar durante o diálogo,
inclusive depois de perda de foco. Houve correções de expectativas de teste
(coordenada sobre um glifo e contador de sequência de uma instância distinta)
e de uso da CLI (contrato posicional e retomada de clone com checkpoint).
O ramo Windows foi preparado com identificador de cursor wide e compilação
COFF; não foi executado pelo Linux.

O teste do visualizador passou também no servidor de vídeo nativo disponível,
abrindo/fechando somente sua própria janela. A suíte automatizada usa o driver
SDL dummy para repetir desenho e filtro de identificador sem depender de um
desktop. A entrada dos testes de aplicações é roteirizada pelo canal do
dispositivo; não é uma certificação de teclado físico, IME ou todos os
gerenciadores de janelas do Linux.

O prazo do teste completo de snapshots das fases anteriores passou de 240 para
360 segundos: a evidência aceita da fase 10 já consumia 232,45 segundos em
Sanitize. Os limites de execução dos convidados e critérios de sucesso foram
preservados. As fontes ficam congeladas durante a matriz final.

Referência da API SDL usada pelo visualizador: [fila de eventos](https://wiki.libsdl.org/SDL2/SDL_PollEvent)
e [identificação de janela](https://wiki.libsdl.org/SDL2/SDL_GetWindowID).

## Limites de plataforma preservados

A proteção de execução física de segundo estágio continua diferente entre os
hosts: WHP controla R/W/X, enquanto o backend KVM declara
`secondStageExecuteProtection:false` e usa NX nas tabelas do convidado. Esta
fase não altera essa limitação. Não há SMP, aceleração 3D, DirectX/OpenGL,
importação de driver de GPU, API binária USER32/GDI nem perfil de Windows
completo. O fonte Win32 do visualizador não torna esses recursos disponíveis
para a aplicação convidada.

| Referência gráfica | Contrato implementado | Evidência exigida |
| --- | --- | --- |
| Paint próprio x64/CPL3 | Retângulos, texto, bitmap e clique | Pixel desenhado e hash completo |
| Notes próprio x64/CPL3 | Texto ASCII, foco e DPI 144 | Caractere K; teclas sem foco descartadas |
| Diálogo próprio modal | Bloqueia pai, aceita dispensa/fechamento | Foco recusado; fechar após blur funciona |
| Visualizador Linux | SDL2/software, uma janela controlada | Apresentação e filtro de ID no processo |
| Visualizador Windows | Janela Win32/software | Compila no precheck; execução nativa pendente |
| Programas GUI PE Windows genéricos | Fora da matriz atual | Não anunciados como compatíveis |

## Primeiro resultado da matriz reconstruída

Debug aprovou os 32 CTests e 66 boots de regressão das fases anteriores.
O núcleo gráfico aprovou 9136 verificações e 20000 comandos hostis; os gráficos
executaram onze boots/retomadas e três pausas, três boots gerenciados e uma
execução com oráculo Python independente. Release e Sanitize permanecem em
execução; este marco isolado não encerra a aceitação.

## Unidade de execução da referência

Paint e Notes são duas ferramentas no mesmo programa de referência, executado
por uma entrada CPL3 no perfil CPU. Compartilham o espaço de endereços; não são
dois processos PE nem dois executáveis USER32 independentes. O campo
`applications=2` do relatório conta esses dois workloads de janela, não
processos do sistema. A matriz inicial da fase 0 exige uma aplicação GUI própria.
Os nomes “two applications” presentes nas saídas dos testes identificam as
duas ferramentas dessa referência; não comprovam isolamento entre processos
gráficos. O isolamento entre processos da fase 5 continua testado em seu
contrato separado.

## Segundo resultado da matriz reconstruída

Release também aprovou 32 CTests e 66 boots de regressão. A matriz já soma
64 CTests e os 132 boots antigos previstos, com pixels iguais nas retomadas
gráficas e três execuções gerenciadas por configuração. O host com
ASan/UBSan está em execução; a aceitação final ainda depende dessa rodada.
A prévia `preview.png` na pasta de evidências foi gerada dos pixels produzidos
pelo convidado Release e conferidos pelo oráculo independente.

O bitmap da referência coincide byte a byte entre Debug e Release (PPM SHA-256
`4d26b5738133e4946a3d86aa6b7f246c25561d0a77df9f0c4c14ad4ded3c5c9b`). Isso inclui o descarte de teclas sem foco e
a retomada; não constitui compatibilidade gráfica com outros aplicativos.

## Entrega e recuperação

As mudanças revisáveis ficam em `changes.patch` e `changes.json` na pasta de
evidências. Há manifesto de documentação, precheck Windows e arquivo
`accepted-sources.tar.gz` com cópia exata das fontes e hashes por arquivo.
A cópia anterior de 350 arquivos permanece disponível no diretório de snapshots.

O empacotador é `python3 tools/package_phase11.py EVIDENCE`. Ele exige a
aceitação aprovada e os mesmos hashes, recusa sobrescrever pacotes existentes
e inclui quatro executáveis ELF, 15 imagens (13 de produção e dois controles
fatais), capturas da referência, documentação e manifesto SHA-256. O pacote
é construído nesta máquina Linux Mint 22.3/GCC 13; portabilidade para outras
versões de libc/distribuições ainda não foi certificada.

## Pacote Linux verificado

Arquivo: `dist/guardianav-phase11-linux-x86_64-20261018.tar.gz` (aproximadamente
1,8 MiB), com SHA-256
`18e53606193f319d828573f8d92f46ea780638cf86b3b566194cd40f4f78f749`.
O arquivo `.sha256` fica ao lado.

O pacote foi extraído em pasta temporária fora do projeto. Foram conferidos
97 arquivos pelo manifesto e os quatro executáveis ELF, com 15 imagens do
convidado. Passaram o scanner nativo, o oráculo de pixels da aplicação, três
boots gerenciados com clone/restauração, o visualizador nativo e dois boots
do console completo com persistência (sequência recuperada e base intacta).
Os sentinelas de Wine/QEMU não foram acionados. Evidências:
`package-validation.json`, `package-commands.json`, `package-graphics/`,
`package-graphics-manager.json` e `package-complete-0/1.json`.

A entrega inclui 37 alterações textuais revisáveis, os 350 arquivos anteriores
intactos no snapshot e um arquivo de fontes exatas com 369 arquivos. A cópia
de fontes e a documentação interna do pacote representam o momento do
empacotamento; este registro externo acrescenta o hash do pacote e seu teste
de extração para evitar referência circular de hashes. Os hashes de todas as
fontes aceitas permanecem iguais.

**Parada:** a fase 11 está implementada e aprovada para a matriz própria no
Linux. A conclusão formal depende dos 52 CTests por configuração no Windows
nativo. A fase 12 não foi iniciada.
