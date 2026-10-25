# Fase 14.5 — runtimes C/C++ originais no MiniNT

Implementação Windows x64 para um corpus delimitado de console, compilado pelo
MSVC 19.44 com `/MD`, startup normal e handlers FH3 e FH4. O kernel e as camadas
NTDLL/KERNELBASE/KERNEL32 continuam próprios. UCRT, VC runtime, MSVCP e MSVCRT
selecionadas são entradas legais locais e executam exclusivamente no convidado.
Esta fase não estabelece compatibilidade com a maioria dos aplicativos Windows.

## O que foi implementado

- Kernel `guardian-kernel-crt.gk` e variantes próprias das quatro DLLs de suporte;
  as variantes anteriores permanecem disponíveis para a regressão.
- Dependências Win32 de inicialização, ponteiros codificados por processo,
  consulta de módulos por endereço, locale, console CRLF e sincronização.
  Critical sections recursivas, SRW exclusivo, init-once e condition variables
  funcionam no convidado, sem consumir um handle por lock da CRT.
- API sets da CRT encaminham à UCRT selecionada, usando dados reencodificados
  do schema Win10. Não existe fallback para funções ou endereços do host.
- Exports C++ com nomes decorados de até 511 caracteres e imports de dados.
  O corpus usa `std::cout` da MSVCP original, além de strings e vetores.
- `RtlCaptureContext`, consulta de metadados de unwind e restauração virtual
  limitada de um frame. Metadados vêm apenas de módulos do processo convidado;
  syscalls privadas são limitadas às DLLs próprias autorizadas.
- Consolidação de unwind para catches C++ por valor e referência; callbacks e
  destrutores executam em CPL3. Record, ponteiro do frame, contexto e destino
  são copiados/validados antes de qualquer retomada.
- Correção de `EXCEPTION_TARGET_UNWIND` para `0x20`, conferida contra o SDK.
  `0x40` é o bit de collided unwind; a constante anterior fazia a CRT original
  destruir objetos fora do escopo do `try` que estavam vivos após o `catch`.
- Limpeza FLS antes de destruir o heap que armazena os dados da CRT. Heaps com
  máximo zero podem crescer dentro da cota de 1 MiB da implementação.
  O PEB informa o mesmo heap retornado por GetProcessHeap.
- Atributo de herança registrado para handles de arquivo da CRT, sem habilitar
  herança de handles na criação de processos. Descritores de segurança externos
  e flags sem implementação retornam erro.

## Corpus de aceitação

| Cenário | Verificação |
| --- | --- |
| Programa C/CRT com entrada própria de diagnóstico | UCRT original, malloc/free, matemática, puts/flush, captura e unwind de contexto. |
| MSVC `/MD`, FH3 | Construtor global, main, ambiente, strings, vector, cout, exceções int/runtime_error, RAII e objetos vivos após o catch. |
| MSVC `/MD`, FH4 | Mesmo programa usando `vcruntime140_1.dll` e os metadados FH4 do compilador atual. |
| Threads | `std::thread`, mutex/condition variable, errno e TLS isolados, construtor/destrutor de thread_local. |
| Arquivos e conversão | fopen/fwrite/fflush/fseek/fread/fclose/remove, arquivo ausente, realloc, erro matemático e locale UTF-8 com par substituto UTF-16. |
| Processo filho | Startup normal da CRT com três argumentos, incluindo `hello world` entre aspas, espera e limpeza do filho. |
| MSVCRT original | LoadLibrary, malloc/free, strlen e atoi; descarregamento. Otimização LFH recusada explicitamente. |
| Limites | CopyFileW sem implementação encerra o programa com diagnóstico; rethrow C++ é recusado e a análise fica inconclusiva. |
| Integridade | Arquivo adulterado e runtime fora da política de versão/digest não são aceitos. |

`crt_windows_reference` executa somente nossos dois programas de teste copiados
para uma pasta limpa, usando as DLLs instaladas do Windows. As DLLs fornecidas
para o convidado não são executadas no host. Essa referência confirma os
resultados do corpus, não equivalência ao kernel de uma revisão NT exata.

## Versões e requisitos

`config/crt-references.json` fixa tamanho, versão e SHA-256 das nove referências
inventariadas. As cinco entradas efetivamente exercitadas são MSVCRT
7.0.19041.546, UCRT 10.0.19041.789, `vcruntime140.dll`, `vcruntime140_1.dll` e
`msvcp140.dll` 14.44.35211.0. As quatro extensões MSVCP adicionais foram apenas
inventariadas; não recebem aceitação de execução nesta fase.

O pacote selecionado foi aceito apenas no perfil NT Win10. Win7 e outras
combinações são recusadas pelo construtor de volumes, inclusive Win11 ainda
sem matriz CRT própria. A versão do runtime não é inferida da versão do NT.
Uma nova versão exige política explícita e nova validação, não apenas renomear
a DLL. As referências provêm da ISO legal já fornecida e do VC Redist da
instalação local do Visual Studio; não são coletadas automaticamente do host.

As regras de redistribuição continuam as dos fornecedores:
[Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-160),
[implantação UCRT](https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment?view=vs-2019).
O pacote desta fase contém apenas componentes próprios, programas compilados e
evidências. O usuário fornece novamente as DLLs legais fixadas para construir
um volume local; ISOs, DLLs de entrada e volumes com código Microsoft não entram
na publicação dos fontes ou da build.

## Limites que permanecem

- Rethrow, exceções aninhadas durante um catch/cleanup, collided unwind e
  tradução de exceções não estão aceitos. A segunda exceção é contida e não
  pode produzir um veredito de execução completa/segura.
- `RtlVirtualUnwind` não implementa context pointers nem variantes além do
  corpus x64 limitado. Dados de unwind incompatíveis falham de forma contida.
- 1 MiB por heap, quatro heaps por processo, três processos e quatro threads
  por processo. Não foram removidas cotas para acomodar a CRT.
- Arquivos mutáveis continuam limitados a 512 bytes, com volume efêmero e
  compartilhado dentro da sessão. Não há acesso a arquivos reais do host.
- Locale: C, UTF-8 do corpus e metadados selecionados pt-BR/en-US; não é NLS
  completo. Console aceita ASCII/tab/CR/LF, com orçamento de 4096 bytes; a
  conversão Unicode da CRT não equivale a console Unicode completo.
- `validation/crt-export-coverage.json` identifica imports que encerram o
  programa caso chamados. Outros contratos não implementados retornam erro
  explícito, como HeapSetInformation/LFH, pipes, enumeração e metadados de
  arquivos, horários locais e formatação de datas. Eles não contam como APIs
  funcionais só porque o símbolo pode ser resolvido.
- Não foram validados executáveis de toolchains diferentes, `/MT`, CRTs de
  depuração Microsoft, GUI, DirectX/WinRT ou o Flappy. A build Debug usa `/MD`
  com runtimes redistribuíveis normais, sem incluir DLLs de depuração Microsoft.
- A fase 14.6 não foi iniciada. As pendências anteriores de SMP/compatibilidade
  e as demais fases continuam identificadas no roadmap.

## Verificação e reprodução

Quatro novos testes CTest: `crt_contract`, `crt_windows_reference`,
`crt_runtime_tools` e `crt_runtime_integration`. Critérios de entrega: suíte
Release completa de 104 testes, aceitação de 29 testes sem ignorados em Release
e Debug, fontes/binários/referências estáveis, e smoke test do pacote em ambas
as configurações. Os números efetivamente obtidos ficam nos JSON/XML de
validação; tentativas incompletas são preservadas separadamente.

```powershell
cmake -S . -B BUILD -G "Visual Studio 17 2022" -A x64 -DGUARDIAN_GUI_COMPATIBILITY_ONLY=ON -DGUARDIAN_NATIVE_REFERENCE_DLL=NTDLL-LEGAL -DGUARDIAN_APISET_REFERENCE_DLL=SCHEMA-LEGAL -DGUARDIAN_CRT_REFERENCE_DIR=PASTA-CRT
cmake --build BUILD --config Release --parallel 6
cmake --build BUILD --config Debug --parallel 6
ctest --test-dir BUILD -C Release -j 1 --output-on-failure --output-junit REGRESSAO.xml
python tools/validate_phase14_5.py --build BUILD --config Release --original NTDLL-LEGAL --schema-reference SCHEMA-LEGAL --reference PASTA-CRT --output ACEITACAO-RELEASE
python tools/validate_phase14_5.py --build BUILD --config Debug --original NTDLL-LEGAL --schema-reference SCHEMA-LEGAL --reference PASTA-CRT --output ACEITACAO-DEBUG
python tools/package_phase14_5.py --build BUILD --release ACEITACAO-RELEASE --debug ACEITACAO-DEBUG --regression REGRESSAO.xml --output PACOTE-NOVO
python tools/smoke_phase14_5.py --package PACOTE-NOVO --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output SMOKE-NOVO
```

Parar após a entrega desta fase; a publicação automática do commit não autoriza
iniciar a 14.6.
