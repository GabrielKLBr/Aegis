# 14.11 — CIL limitado, contratos WinRT e instalação portátil

Entrega inicial delimitada para Windows x64/WHP, pelo executável de laboratório
`guardian-desktop.exe --frameworks`. O kernel próprio
`guardian-kernel-frameworks.gk` contém agora o contrato `GuardianFramework/2/cil-forms`
(`GuardianFramework/1/cil-console` nas duas entregas anteriores).
Esta entrega não conclui a compatibilidade ampla com os frameworks previstos no
roadmap. A integração desses novos contratos ao scanner/serviço continua na 14.13.

## Implementado

- **CIL de console:** `mscoree.dll` própria interpreta um subconjunto de IL em
  PE32+ x64, ILONLY, com referência a `mscorlib` v4. Métodos estáticos e de
  instância não virtuais, construtores, campos de instância/estáticos,
  inicializadores de tipo, operações inteiras de 32 bits, comparações, saltos,
  chamadas/recursão limitadas, strings UTF-16 e `Console.Write/WriteLine` nas
  assinaturas aceitas. `Main(string[])`
  recebe somente uma lista vazia; argumentos adicionais são diagnosticados.
  Não carrega o CLR Microsoft nem executa código gerenciado no host.
- **WinRT local ao convidado:** HSTRING e string references, inicialização por
  thread/apartment e ativação de `Windows.Foundation.PropertyValue`. Int32,
  Boolean e String implementam `IPropertyValue` e suas interfaces `IReference<T>`;
  nomes de classe foram comparados com o Windows. Outras criações e arrays
  retornam `E_NOTIMPL`; classes desconhecidas retornam `REGDB_E_CLASSNOTREG`.
  Nenhuma classe WinRT é ativada no host. Esse conjunto de contratos não anuncia
  suporte ao framework WinRT completo ou a aplicativos UWP.
- **`version.dll`:** leitura de RT_VERSION em arquivos PE64 do convidado, fixed
  information, strings e translation via consultas W. A variante A cobre paths
  ASCII e consultas binárias; consultas de texto A são recusadas. Recursos com
  múltiplas línguas e resolução/mesclagem MUI não são implementados.
- **Instalação portátil:** cópia de payload do pacote para o namespace gravável,
  registro HKCU, início do EXE instalado, estado persistente, retomada e remoção.
  Os testes usam um cliente compilado contra o SDK, sem imports privados na
  aplicação. Não se trata de um instalador MSI, MSIX, AppX ou SetupAPI.

Arquivos grandes instalados são referências imutáveis a bytes do pacote
vinculado por SHA-256. O marcador guarda caminho canônico, tamanho e checksum;
o kernel só o resolve contra os arquivos do pacote validado. O checksum do
marcador não substitui o vínculo criptográfico do volume. Cópias até 512 bytes
usam o armazenamento comum. Escrita em uma referência instalada é recusada;
copiar novamente, ler, consultar tamanho e remover são suportados no recorte.
O destino continua sujeito às ACLs e aos limites existentes do VFS. Uma leitura
do pacote com nome longo verifica também a ACL do diretório pai; isso não amplia
o limite de nome de arquivo do armazenamento gravável.

## Limites e perfis

O interpretador aceita arquivos de até 1 MiB, metadata de até 256 KiB, 128
métodos, corpos de até 4096 bytes, 32 posições de pilha/locais, oito argumentos,
oito frames, 100 mil instruções e 64 KiB de saída. Excesso de cotas, erro
aritmético e saída inválida interrompem o cenário e deixam a análise inconclusiva.
UTF-8 de console é validado incrementalmente, incluindo sequências truncadas,
surrogates, encodings excessivamente longos e controles de terminal proibidos.

EH, P/Invoke, I8, generics, JIT, GC e BCL completos não são implementados.
O validador estático não é um verificador CLI
completo: tipos/pilha, orçamento e outros requisitos também são verificados em
execução. Uma inspeção aceita não equivale a garantir que o programa funcionará.

O console externo foi exercitado nos perfis NT 6.1.7601, 10.0.19045 e
10.0.22631. O perfil 6.1 usa aliases API-set v2 próprios e delimitados para
adaptadores do pacote; não é um schema nativo de Windows 7 nem uma alegação de
conformidade completa. WinRT é testado no perfil 10.0.19045; no 6.1 retorna
`E_NOTIMPL` antes da ativação. Esses resultados não estendem automaticamente
a matriz gráfica/de áudio a todos os perfis.

**Pendentes da 14.11 ampla:** CLR/CoreCLR reais, .NET Framework/.NET modernos,
WinForms completo, WPF, WinUI 3, Windows App SDK e frameworks de terceiros além do recorte
de CIL descrito. `clr.dll`, `coreclr.dll`, `msi.dll` e `setupapi.dll` não são
fornecidos por esta entrega. Imports/pacotes reconhecidos sem suporte recebem
diagnóstico; dependências desconhecidas também passam pela validação de imports.
Não se declara que a maioria dos programas Windows funciona.

## Verificação e reprodução

`tools/validate_frameworks.py` executa somente a 14.11 e as dependências usadas
por ela. Verifica console/Unicode/aritmética, um programa externo do Mono com
fonte/licença fixados em `test/frameworks/mono/ORIGIN.json`, perfis, WinRT,
recursos de versão, limites de cópia, instalação/retomada/remoção, falhas de CIL,
recusas de frameworks e vínculo de modo. Os clientes WinRT e Version do SDK são
comparados nativamente em diretório limpo, sem DLLs do convidado. Clientes de
cópia/instalação e EXEs CIL são executados somente no MiniNT.

O teste do parser usa AddressSanitizer no host: 20 mil mutações determinísticas,
todos os truncamentos do fixture e verificações UTF-8. Os mutantes nunca são
executados. Aceitar uma mutação para inspeção não significa que ela foi executada
nem prova conformidade/ausência de vulnerabilidades. Não há alegação de
impossibilidade de escape ou de indetectabilidade nesta entrega.

Configure `GUARDIAN_BUILD_FRAMEWORKS=ON` e, para instrumentar apenas o teste do
parser, `GUARDIAN_FRAMEWORKS_ASAN=ON`. Compile `guardian_framework_runtime`,
`guardian-desktop` e `guardian-framework-tests` para Release/Debug. Execute:

```text
python tools/validate_frameworks.py --build BUILD --config Release --output NOVO --reference CRT --schema-reference SCHEMA --asan-runtime ASAN_BIN
```

São necessários MSVC/SDK, WHP, o compilador C# x64 do .NET Framework instalado e
referências locais legais do CRT/API-set. O pacote contém apenas as DLLs próprias;
referências Microsoft e o runtime ASan não são redistribuídos. Repita com Debug.
`tools/framework_runtime.py` oferece `inspect`, preparação de volumes e
`--portable install|resume|uninstall`. `tools/package_phase14_11.py` exige recibos
aprovados e hashes atuais antes de empacotar. Não execute os clientes de
instalação/cópia diretamente no Windows do host.

Aceitação Windows/MSVC/WHP em 29/10/2026: **38/38 Release e 38/38 Debug**, com
fontes e binários estáveis durante cada rodada. Em cada configuração, o parser
passou 20 mil mutações e 3584 truncamentos com AddressSanitizer. O programa externo
instalou, iniciou, imprimiu seu resultado, retomou de estado persistido e foi
removido no perfil 10.0.19045. Os outros dois perfis cobrem o cenário de console;
não representam toda a instalação/GUI desses sistemas. Recibos e hashes acompanham
a build privada. A validação é limitada à 14.11 e às suas dependências usadas.

## Continuação — base de objetos gerenciados

O lote seguinte implementa objetos de classes próprias que derivam diretamente
de `System.Object`, métodos de instância não virtuais, construtores, campos e
`readonly`. O interpretador verifica o tipo concreto ao atribuir referências a
locais, argumentos, retornos e campos tipados. Um campo de outra classe ou uma
escrita `readonly` fora do construtor são recusados durante a execução.
Inicializadores estáticos executam uma vez por tipo/processo; falha do
inicializador interrompe o cenário. Não há herança customizada nem dispatch
virtual neste recorte.

Também estão disponíveis arrays unidimensionais de Int32, String e Object,
comprimento, leitura/escrita com verificação de limites, boxing/unboxing de
Int32/Boolean com identidade de referência e operações selecionadas de String:
concatenação de duas a quatro strings, comprimento, igualdade ordinal e
`IsNullOrEmpty`. A saída de objetos sem override usa o nome do tipo; arrays
usam os nomes dos tipos de elementos. APIs de string não enumeradas continuam
sem suporte.

Limites adicionais por processo: 32 tipos, 64 campos, 32 alocações de objetos
incluindo boxing, 16 arrays com até 256 elementos cada e 64 strings dinâmicas
com até 512 unidades UTF-16 cada. As alocações permanecem até o encerramento do
processo; isso **não é um coletor de lixo**. Excesso de cota é inconclusivo,
inclusive se os objetos já não estiverem referenciados pelo programa.

`Managed.cs` é um controle próprio e inofensivo, executado no CLR do host apenas
para obter uma saída de referência, depois comparada com o MiniNT. Entradas
externas e fixtures IL inválidos nunca são executados no host. O ASan exercita
os seeds de console e de objetos separadamente. Os testes IL negativos removem
o bootstrap nativo gerado pelo ILASM: eles verificam as regras do interpretador,
sem anunciar suporte ao bootstrap CLR nativo. O pacote inclui os fontes `.il`
necessários para reproduzir esses casos.

**Estado após a base de objetos:** a fase ampla permanece aberta. Essa entrega
ainda não executava WinForms/WPF/WinUI ou CLR/CoreCLR completos. A conclusão exige definir aplicações
alvo por framework, fazê-las executar seus cenários no convidado e verificar
instalação, encerramento e comportamento nos perfis NT anunciados.

Validação desta continuação em 29/10/2026: **56/56 Release e 56/56 Debug**,
com hashes estáveis de fontes e binários. Inclui comparação exata da saída do
controle gerenciado com o CLR nativo, instalação/retomada/remoção desse controle,
referência nula, limites de arrays e alocações, falha de inicializador e três
programas IL inválidos (escrita readonly, campo de outra classe e local tipado
incompatível). A matriz anterior de 38 casos fica preservada como histórico;
esta ampliação não é uma regressão completa das outras fases.

## Continuação — WinForms selecionado com janelas do Windows

Primeiro alvo escolhido: contratos de **.NET Framework 4 x64** no perfil
**NT 10.0.19045**, usando o interpretador próprio e a ponte USER32/GDI existente.
Não se carrega `System.Windows.Forms.dll` nem o CLR Microsoft: referências de
metadata aceitas são resolvidas para implementações próprias explicitamente
permitidas. `mscorlib`, `System` e `System.Windows.Forms` têm AssemblyRef major 4;
tipos da interface também são vinculados ao nome correto do assembly.

O exemplo independente é **Rosetta Code, Window creation, C#**, sem alterações,
fixado pelo blob de origem e SHA-256 em `config/framework-forms-corpus.json`.
Seu cenário cria uma Form, define título, registra Disposed com um delegate,
exibe a janela, entra em `Application.Run()` e chama `Application.Exit()` ao
fechá-la. `tools/framework_forms_corpus.py` baixa e verifica esse fonte em uma
pasta local. O exemplo e o EXE compilado não são redistribuídos pelo pacote;
nenhuma entrada externa gerenciada é executada no host.

Contratos deste lote:

- `Form`, `Button`, `Label`, `TextBox` e `Control.ControlCollection.Add`;
- Text, Left, Top, Width, Height, Visible e IsDisposed nas assinaturas aceitas;
- Show/Hide, Close/Dispose, Application.Run(Form), Run(), Exit() e DoEvents();
- EventHandler com método estático ou de instância de classe própria,
  `ldftn` como token de metadata opaco, Button.Click e Component.Disposed;
- inscrição/remoção de eventos com identidade de alvo/método e snapshot da
  lista de handlers durante a emissão; callbacks permanecem na thread convidada;
- janelas customizadas do broker e controles BUTTON/STATIC/EDIT nativos, com
  texto/fontes fornecidos pelo Windows. HWNDs do host não chegam ao interpretador.

O teste de entrada enumera somente janelas do auxiliar filho do launcher em
teste, confirma suas classes nativas e consulta a fonte do EDIT. Envia BM_CLICK
apenas ao botão desse processo. Fechamentos usam WM_CLOSE na janela identificada;
não há movimentação de mouse nem digitação no desktop. Os testes verificam
callbacks, texto Unicode, remoção de handlers, objetos descartados, liberação
dos recursos e encerramento do auxiliar. Erros de callback e cotas continuam
inconclusivos; agora também passam pelo encerramento normal do processo e do
canal gráfico antes de registrar a falha.

Continuam valendo os limites do heap gerenciado. Cada evento tem até oito
handlers; textos de interface têm até 255 unidades UTF-16, sem NUL embutido ou
controles fora do contrato. Dimensões são limitadas a 1024 e posições a 8192.
O loop aceita até 4096 mensagens e 15 segundos por Run, dentro do prazo do
launcher. Não há GC, layout automático, designer, herança de Form, dispatch
virtual customizado, timers gerenciados, drawing/structs de System.Drawing,
diálogos e controles adicionais, delegates genéricos, EH ou interoperabilidade
CLR nativa. Chamadas/assinaturas fora da lista são recusadas; não há fallback
para executar a aplicação no host. O suporte gráfico não é anunciado nos
perfis NT 6.1/10.0.22631 por estes resultados.

Reprodução do lote, sem repetir as baterias anteriores:

```text
python tools/validate_framework_forms.py --build BUILD --config Release --output NOVO --reference CRT --schema-reference SCHEMA --asan-runtime ASAN_BIN --corpus CACHE_LOCAL
```

Repita em Debug. `tools/package_phase14_11.py --forms` exige os recibos desse
lote e os hashes atuais. O parser recebe o seed de interface com metadata de
tipos aninhados/delegates sob AddressSanitizer; mutantes não são executados.
Instalação/retomada/remoção são exercitadas no convidado tanto pelo controle
próprio como pelo exemplo independente. Os recibos acompanham a build privada.

Este lote amplia a 14.11; **a fase ampla permanece aberta**. Os próximos blocos
ainda precisam implementar o runtime/BCL e os contratos gráficos requeridos
por aplicações maiores e pelos demais frameworks, antes de anunciar compatibilidade.

Aceitação deste lote em 29/10/2026: **20/20 Release e 20/20 Debug**, com fontes
e binários estáveis. Em cada configuração, o parser passou 20 mil mutações e
5120 truncamentos sob AddressSanitizer. O exemplo externo completou a instalação,
execução gráfica, retomada e remoção no convidado. Os dois conjuntos anteriores
(38 e 56 casos) permanecem registros históricos, sem repetição nesta rodada.
