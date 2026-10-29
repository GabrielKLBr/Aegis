# Fase 14.12 — x86 e perfis NT adicionais

## Estado: primeiro lote 14.12A, núcleo x86 para Windows 10

O kernel x64 do MiniNT inicia executáveis PE32 e DLLs próprias de 32 bits em
modo de compatibilidade x86 da CPU, CPL3, sobre WHP. A entrada do executável e
seus callbacks continuam dentro da VM.

Este lote estabelece o núcleo da 14.12. A fase ampla permanece aberta: não
implementa WOW64 completo, DLLs Microsoft arbitrárias ou suporte geral a x86.
O único perfil aceito é **NT 10.0.19045 / Windows 10 x86**. Outros perfis são
recusados neste pacote, sem promover o inventário de versões a suporte binário.

## Implementação

- Carregador PE32 separado por configuração de kernel: I386/0x14c, cabeçalho
  0x10b, IAT de quatro bytes e relocations HIGHLOW. Misturar PE32 e AMD64 no
  mesmo grafo é recusado. Permanecem limites, rollback e proteção W^X.
- Entrada em CS 0x1b, pilha de 32 bits, stdcall/cdecl e retorno por interrupt
  gate DPL3 dedicado. FS:[18h] identifica o TEB32, FS:[30h] aponta ao PEB32 e
  PEB:[8] contém a base da imagem.
- `kernel32.dll`, `user32.dll`, `win32u.dll` e `msvcrt.dll` próprias geradas
  pelo cross compiler x86, sem vincular a CRT do host. DllMain e a entrada do
  aplicativo executam no convidado.
- Busca de DLLs em `C:\Windows\SysWOW64`; consulta de módulo a
  `C:\Windows\System32\kernel32.dll` redirecionada ao módulo x86. Isso ainda
  não é redirecionamento geral de arquivos/registro.
- Thunks com ponteiros de 32 bits, sondagem de memória no kernel e pedidos com
  tamanho/versão e origem validados. O protocolo gráfico mantém IDs opacos de
  64 bits, representados por IDs de 32 bits no convidado. IDs antigos não
  identificam janelas novas; HWNDs reais não atravessam a VM.
- USER32 usa o auxiliar restrito existente: janela, BUTTON, EDIT e STATIC reais
  no Windows; eventos e WndProc stdcall x86 no MiniNT. O relatório
  `native-user32.json` conserva o identificador do backend **14.10B/protocolo 2**;
  manifesto e prova desta entrega identificam 14.12.
- Telemetria das operações observadas. Saída diferente de zero e acesso
  inválido geram sessão malsucedida e análise **inconclusiva**, com limpeza de
  janelas e auxiliar também em caso de exceção.

## Contratos e limites atuais

`kernel32` cobre identificação, pseudo-handles, LastError, consulta WOW64 do
processo atual, console por WriteFile, heap/alocação RW, relógio, módulos já
carregados, export por nome, ExitProcess e conversões UTF-8/CP1252 próprias.
O inventário gerado permite conferir exports; o nome não implica contrato
Windows completo.

Há um processo e uma thread de aplicação. LastError e a tabela gráfica
dependem desse limite. A alocação aceita endereço escolhido pelo kernel,
reserva+commit RW, até 1 MiB por região e 64 regiões simultâneas. VirtualFree
libera apenas regiões desse alocador; módulos/TEB/pilhas ficam protegidos.
São recusadas páginas RWX. O heap usa uma região por alocação. A tabela conserva
até 128 IDs gráficos durante a sessão, além do limite de objetos vivos do
backend. Sleep consulta o relógio em laço; não é sincronização Win32 completa.

`msvcrt` atende `puts` e `printf` de texto literal, sem conversões `%`. Não é
CRT geral. Imports dinâmicos por ordinal, API sets, LoadLibrary, processos filhos,
threads e os contratos NT/Win32 restantes exigem continuação.

TLS PE32, SEH x86, exceções C++, load config/CFG, delay imports e CLR são
**recusados**. Acesso inválido encerra o processo com status de exceção, sem
despacho de handlers SEH. Não há GDI/DirectX/áudio x86, registro/VFS x86 geral,
programas de 16 bits ou ARM neste lote.

## Validação delimitada

**Resultados Windows/WHP em 29/10/2026:** Release **18/18**, Debug **18/18**,
com fontes e binários estáveis. O parser ASan aprovou 10.240 verificações e
4.608 truncagens em Release; 13.312 verificações e 6.144 truncagens em Debug,
além das 1.000 mutações por configuração. Esses números são verificações
internas, não contagens de programas independentes.

`tools/validate_compat32.py` reúne **18 casos por configuração**, somente para
este lote e suas dependências usadas; não repete baterias antigas.

- Parser/carregador sob AddressSanitizer: grafo PE32, fixups, IAT, W^X, rollback,
  arquitetura/TLS/overflow recusados, todas as truncagens do EXE gráfico e
  1.000 mutações determinísticas. Entradas analisadas, nunca executadas no host.
- Console/GUI próprios: ABI/FS/PEB, busca SysWOW64, heap, cota, dupla liberação,
  ponteiro inválido, páginas executáveis recusadas e IDs antigos sem alias.
- Entrada dirigida só ao auxiliar do teste: texto `abc` em EDIT e clique no
  BUTTON, conferidos pelo callback convidado; sem usar mouse/teclado globais.
- Console C independente do projeto `leachim6/hello-world`, fonte não alterado
  fixado por blob/SHA-256 em `config/compat32-corpus.json`. Compila com entrada
  e DLLs próprias e imprime `Hello World` no MiniNT. Não demonstra CRT amplo;
  a GUI própria não conta como corpus gráfico independente.
- Falha de memória com/sem janelas e ExitProcess(17): análise inconclusiva,
  nenhum processo/objeto/handle/referência vivo, auxiliar encerrado e zero janelas.
- Perfil, arquitetura, CLR, RWX, truncagem, DLL x64 e modo de lançamento
  incompatíveis recusados; manifesto adulterado para x64 recusado antes da VM.

As provas exigem estabilidade dos hashes de fontes/binários. O empacotador só
aceita provas atuais aprovadas em Release e Debug. O pacote extraído é testado
no mesmo escopo; os hashes são conferidos antes e depois. Builds ficam no
repositório privado. Runtimes Microsoft, fonte e binário externos não são
redistribuídos; a reprodução exige ferramentas/referências locais.

## Reprodução no Windows

Requisitos: Windows x64 com NX/WHP, Python, Visual Studio 2022 C++ x86/x64 e SDK
Windows. Forneça o runtime ASan da instalação local. Use configuração CMake
que já habilite o kernel USER32 nativo.

```powershell
cmake --build PASTA_BUILD --config Release --target guardian_compat32_runtime guardian-compat32-tests
cmake --build PASTA_BUILD --config Debug --target guardian_compat32_runtime guardian-compat32-tests
python -B tools/validate_compat32.py --build PASTA_BUILD --config Release --output PROVA_RELEASE_NOVA --corpus CACHE_EXTERNO --asan-runtime PASTA_ASAN
python -B tools/validate_compat32.py --build PASTA_BUILD --config Debug --output PROVA_DEBUG_NOVA --corpus CACHE_EXTERNO --asan-runtime PASTA_ASAN
python -B tools/package_phase14_12.py --build PASTA_BUILD --release PROVA_RELEASE_NOVA --debug PROVA_DEBUG_NOVA --output PACOTE_NOVO
```

Os EXEs/DLLs x86 são entradas **somente para o convidado**. O construtor abaixo
inspeciona os arquivos, valida imports e cria volume, manifesto, inventário e
binding sem executar o programa. `compat32_runtime.verify` verifica seus hashes
e vínculos. O comando seguinte executa a entrada dentro do MiniNT:

```powershell
python -B tools/compat32_runtime.py --build PASTA_BUILD --config Release --application APP_X86.exe --output BUNDLE_NOVO
PASTA_BUILD/Release/guardian-desktop.exe PASTA_BUILD/guest/Release/guardian-kernel-compat32.gk BUNDLE_NOVO/runtime.gpv BUNDLE_NOVO/manifest.json BUNDLE_NOVO/binding.gnt SESSAO_NOVA --compat32 --seconds 15
```

## Continuação da 14.12

Antes da conclusão ampla: corpus GUI independente; thunks de arquivos/registro
com visões separadas; TLS/SEH e serviços nativos necessários aos programas
escolhidos; expansão das DLLs gráficas e demais famílias por demanda. Cada
perfil NT novo exige referências legais e aceitação por build. A 14.13 não é
iniciada nesta entrega.

Referências: [aplicativos de 32 bits](https://learn.microsoft.com/en-us/windows/win32/winprog64/running-32-bit-applications)
e [detalhes do WOW64](https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details).
O MiniNT usa implementação própria, sem redistribuir o WOW64 do Windows.
