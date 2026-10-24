# Fase 12 — lote de compatibilidade gráfica

**23/10/2026: avanço parcial; lote do Flappy Window não concluído. A 12F não foi
iniciada.** Esta entrega conecta pela primeira vez um arquivo PE separado ao
kernel de processos e ao compositor do convidado. A validação desta matriz
pequena não declara compatibilidade com aplicativos Windows em geral.

## Caminho implementado

`guardian-gui-pe` recebe kernel, aplicativo e uma pasta nova de evidências.
O arquivo PE é copiado sem alteração a uma região reservada, somente leitura
e sem execução, com descritor vinculado ao boot. O kernel verifica os limites,
a reserva e o hash antes de usar seu carregador PE existente. Cabeçalhos,
relocações, imports e proteções continuam sendo verificados no convidado.
O aplicativo não fica embutido no kernel. A DLL USER32 própria fica embutida;
não são carregadas DLLs do Windows host.

O programa de referência `guardian-gui-application.exe` importa funções com
assinaturas do Windows, cria uma janela, pinta retângulos, processa mensagens
e muda o desenho após uma tecla e um clique. Seus procedimentos de janela e
todo o código da DLL executam em CPL3 no convidado. Comandos tipados de desenho
passam pelo kernel e pelo canal gráfico já existente; o host recebe pixels e
metadados validados. O visualizador continua sendo uma única janela nativa.
Não existe encaminhamento arbitrário de funções, callbacks, HWND ou HDC ao host.

A DLL implementa um subconjunto de 18 exports: `RegisterClassA`,
`CreateWindowExA`, `DefWindowProcA`, `DestroyWindow`, `ShowWindow`,
`UpdateWindow`, `InvalidateRect`, `BeginPaint`, `EndPaint`, `GetClientRect`,
`FillRect`, `GetSysColorBrush`, `GetMessageA`, `PeekMessageA`,
`TranslateMessage`, `DispatchMessageA`, `PostQuitMessage` e `GetSystemMetrics`.
Há restrições explícitas de parâmetros: classes sem dados extras, janelas
visíveis com estilo `WS_CAPTION | WS_VISIBLE`, sem menu ou parentesco; fila sem
filtros e com `PM_REMOVE`/`PM_NOREMOVE`, invalidação integral sem apagamento,
pintura por retângulos e duas cores de sistema do desktop virtual. Isso não
representa implementação integral dessas APIs para qualquer parâmetro.
O dispositivo atual fornece texto ASCII, encaminhado como `WM_CHAR`; não
fornece códigos físicos de tecla, estado de modificadores ou `WM_KEYUP`.
`TranslateMessage` cobre apenas um subconjunto sem modificadores. Jogos que
dependem de teclas físicas exigem ampliar esse contrato antes da aceitação.

A matriz usa 320×240, uma thread de GUI no processo raiz, até quatro classes e
janelas e 16 mensagens em fila. A fonte PE e a imagem carregada têm teto de
128 KiB cada. Chamadas de arquivo/console não fazem parte deste novo contrato
e retornam sem suporte; os contratos anteriores de armazenamento permanecem
separados. Snapshots desse contrato são recusados explicitamente. Não há CRT
geral, GDI/DWM completos, Unicode, aceleração GPU ou integração com a fila do
scanner nesta entrega. O scanner da fase 14 mantém seu backend documentado.

## Compilar e executar a referência

No Windows x64, com MSVC e WHP disponíveis, usar uma pasta de build nova:

```powershell
cmake -S . -B build-gui -A x64 -DBUILD_TESTING=ON -DGUARDIAN_GUI_COMPATIBILITY_ONLY=ON -DGUARDIAN_BUILD_PE_GUI=ON
cmake --build build-gui --config Release --target guardian-gui-pe pe_gui_boot_tests pe_gui_native_oracle --parallel 4
ctest --test-dir build-gui -C Release -R "^pe_gui_" --output-on-failure
build-gui\Release\guardian-gui-pe.exe build-gui\guest\Release\guardian-kernel-pe-gui.gk build-gui\guest\fixtures\Release\guardian-gui-application.exe build-gui\sessao-nova --interactive
```

Na janela do visualizador, digitar `G`, clicar na área cliente e fechar a
janela para concluir a sequência da referência. A execução tem orçamento real
de 30 segundos. Entrada manual e roteiro temporizado são modos separados;
`--script ARQUIVO` usa o formato existente dos roteiros do convidado.
O perfil de compilação acima exclui os frontends/dependências do scanner;
destina-se às ferramentas e testes do núcleo. No build normal, o novo contrato
experimental exige `GUARDIAN_BUILD_PE_GUI=ON` explicitamente. Linux/KVM não
recebeu construção/aceitação deste novo contrato nesta entrega.

## Avaliação do Flappy Window 1.5

Arquivo fornecido pelo usuário, SHA-256
`f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6`.
O inventário estático, sem executar o jogo, encontrou:

| Requisito | Flappy | Novo contrato |
|---|---|---|
| Arquivo PE | 3.202.095 bytes | Até 131.072 bytes |
| Imagem em memória | 1.548.288 bytes | Até 131.072 bytes |
| DLLs importadas | USER32, GDI32, KERNEL32, MSVCRT e DWMAPI | USER32 própria, com subconjunto explícito |
| Entradas de importação | 178, incluindo uma duplicata | 16 correspondem a exports disponíveis; 162 permanecem ausentes neste contrato |
| Execução do jogo, configurações e encerramento | Critério de aceitação do roadmap | Não demonstrados |

Os 88 imports de MSVCRT e 59 de KERNEL32 não são resolvidos automaticamente
pelas implementações existentes sob outro nome de DLL. Também faltam nove
imports de USER32, cinco de GDI32 e um de DWMAPI. Quantidades são entradas de
importação, não funções comportamentalmente validadas. Ter um export não prova
que os parâmetros usados pelo jogo sejam suportados.

Ainda é necessário ampliar o carregador e os orçamentos de memória com testes,
implementar e validar as dependências e a inicialização do runtime, integrar
arquivos/configurações e completar as operações gráficas realmente usadas.
O limite atual de quatro módulos e o orçamento de páginas privadas também
precisam de revisão para esse aplicativo; aumentar apenas constantes ou usar
retornos de sucesso vazios não satisfaz o critério de compatibilidade.
O jogo não foi executado no Windows host como alternativa.

`tools/inspect_gui_compatibility.py` produz inventário de imports comparado aos
exports da DLL realmente compilada. `candidateForGuestLoader` significa apenas
que os pré-requisitos estáticos verificados passaram; o carregador e a execução
continuam sendo a autoridade. `functionalCompatibilityProven` permanece falso
nessa ferramenta, que não executa programas.

## Validação e decisão sobre a 12F

O mesmo fonte da aplicação de referência é compilado contra o USER32 nativo
do Windows e contra a implementação do convidado. O teste nativo envia
mensagens somente à sua própria janela; não injeta entrada global.
O teste de boot fornece o PE como arquivo independente em 16, 32 e 64 MiB e
confere três estados de pixels de forma independente: pintura inicial,
alteração após tecla e alteração após clique. Confere ainda fechamento,
limpeza do processo, páginas da fonte RO/NX, preservação dos bytes, recusa de
ponteiros inválidos/overflow e de acesso a janelas do host, PE inválido, fonte
adulterada e snapshots sem suporte. Um controle privado do host permanece
fora da RAM convidada e seu arquivo permanece inalterado. Esses testes cobrem
os canais exercitados; não constituem prova geral de ausência de vulnerabilidades.

As evidências ficam em `build-phase12-gui/`. O validador
`tools/validate_phase12_gui.py` registra JUnit, hashes dos fontes e dos binários
GUI selecionados e sua estabilidade, mantendo `phaseComplete: false`.

| Configuração Windows/MSVC/WHP | Resultado final | Evidência |
|---|---|---|
| Release: três testes novos e doze regressões de PE, gráficos e análise | 15/15, sem ignorados; 139,19 s | `acceptance-release-final/validation.json` e JUnit |
| Debug: três testes novos | 3/3, sem ignorados | `acceptance-debug-final/validation.json` e JUnit |

As regressões incluem os quatro kernels PE anteriores, o núcleo de runtime,
gráficos, visualizador, aplicação gráfica anterior e análise comportamental.
Não foi executada a suíte completa do scanner da fase 14 nesta entrega.
Durante a preparação, corrigiu-se uma dependência incompleta do teste de análise:
ele também precisa construir os kernels de referência CPU, storage, rede,
hardware e o driver/API próprios. As tentativas anteriores com referências
ausentes e os ajustes de compilação foram preservados, sem contá-los como
aceitação. Os mapas finais de fontes Debug/Release coincidem e ficaram estáveis
durante os testes. O backup recuperável é `baseline-verified.zip`, vinculado ao
commit inicial e preservando a inclusão anterior da 12F no roadmap.

Pacote de demonstração: `dist/aegis-phase12-gui-prototype-windows-x64-20261023/`,
com manifesto SHA-256 e cópia das evidências. O PE de referência é distribuído
com extensão `.gpe`, destinado ao carregamento no convidado. Não é uma entrega
de compatibilidade do Flappy nem um encerramento da fase 12.

**Pré-requisitos mínimos demonstrados para a referência própria:** carregar um
PE externo ao kernel, desenhar e receber entrada pelo visualizador.
**Pré-requisitos para a matriz do Flappy e para iniciar a 12F nesse escopo:**
não atendidos. A aplicação externa representativa ainda não executa; não se
declara encerrado o lote nem se inicia a 12F automaticamente.

Contratos consultados: [CreateWindowExA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexa),
[PeekMessageA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagea)
e [BeginPaint](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-beginpaint).
