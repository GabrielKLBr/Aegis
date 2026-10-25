# Fase 14.8 — componentes e serviços de desktop no MiniNT

Esta entrega implementa um lote delimitado de COM, RPC, automação, propriedades,
Shell, controles e diálogos para o perfil **Windows 10 x64**. Os aplicativos de
aceitação são executáveis e uma DLL independentes, compilados com imports
normais do SDK Windows. As bibliotecas próprias cumprem os cenários descritos
abaixo; não se declara implementação completa dessas DLLs nem compatibilidade
geral com componentes arbitrários do Windows.

## Execução e isolamento

`guardian-kernel-components.gk` conserva os serviços da 14.7 e acrescenta um
canal de comunicação entre processos do convidado. O kernel copia mensagens
limitadas, verifica PID/TID, identidade e integridade, atribui identificadores
monotônicos e controla a propriedade de endpoints e respostas. Há **12 endpoints,
16 chamadas simultâneas e 768 bytes por mensagem**. Excesso de recursos falha;
cancelamento e saída de processos/threads recolhem as chamadas e endpoints.
Respostas de outro processo ou thread não são aceitas.

A entrada A8 é reconhecida somente em código executável de `combase.dll` no
convidado. O transporte não contém endereços de funções, ponteiros de objetos ou
handles do host. Factories, objetos COM, callbacks de janela, stubs RPC e loops
de servidor executam em CPL3. Ativação usa o registro virtual, o carregador de
DLLs e a criação de processos do MiniNT. Nenhuma chamada é encaminhada ao COM,
RPC, Explorer, seletor de arquivos ou registro do host.

Os testes de referência `component_windows_*` executam programas próprios no
Windows para comparar contratos. Eles não registram classes persistentes no
host nem abrem diálogos pessoais. Os aplicativos de aceitação executam no MiniNT.

## Contratos implementados

| Família | Lote implementado |
| --- | --- |
| OLE32 / COMBASE | Inicialização STA/MTA por thread, contagem balanceada, mudança de modelo recusada, consulta de apartment, ativação de DLL via `DllGetClassObject`, ativação de processo via `LocalServer32`, factories e locks, revogação e descarregamento condicionado a `DllCanUnloadNow`. |
| Marshaling | Streams de memória limitados, `CoMarshalInterface`, `CoUnmarshalInterface`, `CoReleaseMarshalData` e transferência entre threads para `IDispatch`. Proxies enviam dados ao apartment proprietário; `CoWaitForMultipleHandles` e a consulta de mensagens do USER32 atendem as chamadas no convidado. |
| OLEAUT32 | BSTR com comprimento explícito, inclusive NUL embutido; VARIANT escalares, strings e cópia de arrays; SAFEARRAY unidimensional limitado a 256 elementos de I4/UI4/R8/BSTR/VARIANT. Cópia profunda, bounds, locks e liberação. Ordinais da interface OLEAUT usados pelos import libraries do SDK são preservados. |
| RPCRT4 | UUIDs locais, strings/bindings `ncalrpc`, buffers RPC limitados e dispatch tables explícitas de interface versão 1.0 com sintaxe NDR32 2.0. O stub do aplicativo trata o payload; não há um interpretador NDR geral nem suporte a stubs MIDL arbitrários. |
| PROPSYS | `IPropertyStore` em memória, até 16 propriedades, enumeração, cópia, leitura e commit; PROPVARIANT escalares, strings de até 255 caracteres e blobs de até 512 bytes. |
| COMCTL32 | EDIT, lista de uma coluna, barra de progresso e seleção de páginas. Até 32 itens de texto por controle; seleção e notificações usam estruturas próprias do contrato Win32. |
| COMDLG32 | Abrir/salvar por diálogo próprio no MiniNT, campo editável, confirmação, cancelamento, restituição do estado do proprietário e validação do caminho. A seleção devolve somente caminhos do convidado. |
| SHELL32 / SHLWAPI | Pastas conhecidas virtuais, operações limitadas de caminhos, execução de `.exe` presente no pacote, espera pelo processo e cópia/movimento/exclusão de arquivo único no namespace convidado. |
| UXTHEME | Handles de tema próprios, validação de janela, desenho básico e liberação. Não reproduz os recursos visuais completos do Windows. |

Marshaling de chamadas de automação admite até quatro argumentos escalares ou
BSTR de até 128 caracteres. `GetIDsOfNames` admite um nome por chamada. Interfaces
arbitrárias, ponteiros, arrays remotos, type libraries, ActiveX, agregação,
monikers, DCOM e autenticação remota ficam fora deste contrato. Inicializações
MTA são atendidas; uma DLL registrada como `Both` pode ser ativada em STA/MTA.
Transições automáticas para um modelo incompatível ainda falham explicitamente.

O servidor STA precisa atender seu loop de chamadas/mensagens. O timeout padrão
das chamadas remotas é de três segundos e cancela a resposta pendente; ele não
termina arbitrariamente o código que já entrou no servidor. Encerramento ou
revogação do endpoint desconecta os proxies correspondentes. O listener RPC
suportado é o síncrono; o modo que exigiria workers automáticos é recusado.
TCP, HTTP e outros transportes externos não são habilitados por esta fase.

## Catálogo e estado persistente

GPV versão 2 comporta até **32 arquivos**, mantendo o cabeçalho anterior e uma
tabela adicional de entradas. O kernel desta fase admite até **24 módulos** por
processo. Os limites de 8 MiB por arquivo e 64 MiB por volume são mantidos; o
bundle inclui somente dependências alcançáveis e os servidores explicitamente
fornecidos. O leitor continua validando GPV versão 1; o host exige o kernel de
componentes para um volume versão 2. Extents, duplicidades e hashes continuam
validados antes da execução. Não foi ampliado o protocolo de dados do runtime.

O disco mutável continua com 64 KiB, 32 nós e arquivos de até 512 bytes. Chaves
de registro com GUID de 38 caracteres usam o payload antes não utilizado de
nós Key, com flag e validação específicas. Tamanho dos registros e dois slots
de commit permanecem preservados. Consulta e enumeração devolvem o GUID inteiro;
dois GUIDs com o mesmo prefixo não se tornam a mesma chave. Estados anteriores
são lidos; estados que usam o novo recurso devem ser usados com esta versão do
runtime, sem misturá-los com builds históricas.

## Limites de desktop

A apresentação continua no visualizador único da 14.7. O diálogo de arquivos é
um formulário limitado; ainda não é o seletor do Explorer com navegação completa.
Não há impressão, hooks de diálogo, templates personalizados, seleção múltipla,
associações gerais de arquivos, execução de URLs, elevação ou integração com o
Explorer. Confirmação de sobrescrita de arquivo existente não está implementada
e é recusada. EDIT não implementa seleção, caret completo ou IME; listas não
implementam múltiplas colunas, virtualização ou ordenação. Não há TreeView,
toolbars e pacote completo de controles comuns nesta matriz.

Temas usam o desenho e a fonte bitmap próprios. A verificação gráfica exige
texto visível no campo do diálogo, entrada de texto/backspace, seleção e
movimento da barra de progresso. Os diagnósticos não fatais de CRT e os limites
de rastreamento da 14.7 continuam documentados em [PHASE-14.7.md](PHASE-14.7.md).

## Aceitação e reprodução

Os oito testes novos são `component_core`, `component_runtime_tools`,
`component_windows_client`, `component_windows_controls`, `component_windows_rpc`,
`component_com_integration`, `component_rpc_integration` e
`component_controls_integration`.

A matriz cobre componente em DLL, factory em outro processo, retorno de valores
e strings, execução na STA proprietária, chamadas MTA concorrentes, classes e
valores persistidos em outra sessão, classe ausente, erro do componente, queda
de servidor, timeout, RPC escalar, procedimento inválido, endpoint encerrado,
controles, diálogo confirmado/cancelado, entrada editável e processo iniciado
pelo Shell. O núcleo testa cotas, propriedade, resposta falsificada, cancelamento,
limpeza, formatos de volume e corrupção dos nomes longos do registro.

`tools/validate_phase14_8.py` exige **124 testes de regressão em Release** e
**49 testes de aceitação em Debug**, sem falhas ou ignorados. Fontes, binários e
entradas legais são comparados antes/depois. O Flappy original da 14.7 integra
a regressão. Evidências ficam nos arquivos `validation.json`, `ctest.xml` e logs.
O pacote só pode ser criado com evidências estáveis e é executado novamente
nas duas configurações antes da publicação privada.

`tools/component_runtime.py bundle` prepara o volume a partir das DLLs próprias,
de referências CRT e schema locais. Referências Microsoft, Flappy, ISOs, volumes
GPV e dados de sessão não são redistribuídos. A invocação usa o mesmo
`guardian-desktop.exe` com `guardian-kernel-components.gk`; os comandos completos
e os testes do pacote acompanham a build privada.

A 14.9 e a 12F permanecem separadas e não são iniciadas por esta entrega.

Referências de contrato: [apartments STA](https://learn.microsoft.com/windows/win32/com/single-threaded-apartments),
[inicialização COM](https://learn.microsoft.com/windows/win32/learnwin32/initializing-the-com-library),
[marshaling](https://learn.microsoft.com/windows/win32/api/combaseapi/nf-combaseapi-comarshalinterface)
e [IMarshal](https://learn.microsoft.com/windows/win32/api/objidl/nn-objidl-imarshal).
