# 14.11 — CIL limitado, contratos WinRT e instalação portátil

Entrega inicial delimitada para Windows x64/WHP, pelo executável de laboratório
`guardian-desktop.exe --frameworks`. O kernel próprio
`guardian-kernel-frameworks.gk` contém o contrato `GuardianFramework/1/cil-console`.
Esta entrega não conclui a compatibilidade ampla com os frameworks previstos no
roadmap. A integração desses novos contratos ao scanner/serviço continua na 14.13.

## Implementado

- **CIL de console:** `mscoree.dll` própria interpreta um subconjunto de IL em
  PE32+ x64, ILONLY, com referência a `mscorlib` v4. Métodos estáticos, operações
  inteiras de 32 bits, comparações, saltos, chamadas/recursão limitadas, strings
  UTF-16 e `Console.Write/WriteLine` nas assinaturas aceitas. `Main(string[])`
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

Campos, inicializadores estáticos, EH, P/Invoke, I8, generics, JIT, GC e BCL
completos não são implementados. O validador estático não é um verificador CLI
completo: tipos/pilha, orçamento e outros requisitos também são verificados em
execução. Uma inspeção aceita não equivale a garantir que o programa funcionará.

O console externo foi exercitado nos perfis NT 6.1.7601, 10.0.19045 e
10.0.22631. O perfil 6.1 usa aliases API-set v2 próprios e delimitados para
adaptadores do pacote; não é um schema nativo de Windows 7 nem uma alegação de
conformidade completa. WinRT é testado no perfil 10.0.19045; no 6.1 retorna
`E_NOTIMPL` antes da ativação. Esses resultados não estendem automaticamente
a matriz gráfica/de áudio a todos os perfis.

**Pendentes da 14.11 ampla:** CLR/CoreCLR reais, .NET Framework/.NET modernos,
WinForms, WPF, WinUI 3, Windows App SDK e frameworks de terceiros além do recorte
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
