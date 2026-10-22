# Validação Windows das atualizações até a fase 11

Data: 19/10/2026. Plataforma: Windows 11 x64, MSVC 19.44 e WHP real.
Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.

**Resultado: a revisão ainda não passou na aceitação Windows.** Foram executados
os 52 testes de cada configuração, sem testes ignorados. As mesmas oito falhas
apareceram em Debug e Release.

| Configuração | Aprovados | Falhas | Ignorados | Tempo da suíte |
| --- | ---: | ---: | ---: | ---: |
| Debug | 44/52 | 8 | 0 | 304,76 s |
| Release | 44/52 | 8 | 0 | 187,50 s |

Total: **104 execuções, 88 aprovadas e 16 falhas**, correspondentes a oito testes
distintos. As fontes permaneceram iguais durante as duas suítes.
A compilação de Release foi preparada enquanto Debug executava; as suítes
CTest foram executadas uma após a outra.

## O que passou

As regressões anteriores e o console completo passaram: processos, memória,
threads, arquivos/registro, heap, TLS, filhos, exceções x64, unwind e DLLs
dinâmicas. Cada configuração aprovou as 40 inicializações do console completo,
incluindo limpeza dos recursos. As comparações com o Windows executaram
153 casos de handlers e 59 casos de unwind por configuração.

Também passaram os testes do núcleo de rede, serialização/relay, rasterizador,
janelas/entrada e visualizador nativo. Isso não aprova os caminhos integrados
de rede e gráficos dentro do convidado, que falharam conforme a tabela abaixo.

## Falhas confirmadas

| Teste | Resultado em ambas as configurações |
| --- | --- |
| `guest_session_snapshot` | `Snapshot check 6`: falha ao definir a interrupção pendente no teste arquitetural WHP |
| `vm_catalog` | `Cannot publish transaction file` no caminho de publicação atômica de arquivos |
| `vm_manager` | Início pausado falha em `WHvMapGpaRange`, código `0xC0370008` |
| `guest_network_boot` | `Unknown network command` |
| `vm_network` | Worker termina com `Unknown network command` |
| `guest_graphics_boot` | `Graphics boot check 1` |
| `graphics_application` | `Unknown graphics command` |
| `vm_graphics` | Início pausado falha em `WHvMapGpaRange`, código `0xC0370008` |

A revisão do fonte encontrou um indício para investigar os comandos de rede e
gráficos: o monitor WHP repassa 32 bits de RAX mesmo em acessos I/O de um byte,
enquanto os controladores comparam o valor completo com 1 e 2. É uma hipótese
de causa comum; esta rodada registrou as falhas sem aplicar uma correção nesse
caminho. A falha de publicação ainda precisa do código `GetLastError` para
identificar sua causa, e os caminhos de IRQ/mapeamento precisam de diagnóstico
WHP específico.

## Ajustes necessários para compilar os testes

A primeira tentativa parou na compilação. Foram corrigidos dois pontos:

- Inclusão explícita de `<string>` em `GuestExceptionDispatchTests.cpp`, necessária para `std::to_string` no MSVC.
- Renomeação de uma variável local de porta em `VmMain.cpp`, eliminando o aviso de ocultação tratado como erro.

Os originais estão em `snapshots/codex-before-windows-phase11-tests-20261019`,
com patch das duas mudanças. Depois desses ajustes, todos os executáveis e as
imagens da suíte foram construídos em uma árvore nova, e as auditorias das
imagens passaram em Debug e Release.

As 331 entradas do manifesto de fontes da validação Linux inicialmente
coincidiam com esta pasta. Os dois ajustes de compilação mudam os hashes desses
arquivos; os relatórios e pacotes Linux anteriores foram preservados. A
aceitação conjunta não foi marcada como completa. A fase 12 não foi iniciada.

## Evidências

A pasta é `build-phase11/validation-20261019-115314-1d807fdc/`:

- [Resumo com hashes, resultados e erros completos](../build-phase11/validation-20261019-115314-1d807fdc/windows-tests-summary.json).
- [JUnit Debug](../build-phase11/validation-20261019-115314-1d807fdc/host/tests-Debug.xml) e [log Debug](../build-phase11/validation-20261019-115314-1d807fdc/Debug-ctest.log).
- [JUnit Release](../build-phase11/validation-20261019-115314-1d807fdc/host/tests-Release.xml) e [log Release](../build-phase11/validation-20261019-115314-1d807fdc/Release-ctest.log).
- [Registro do verificador oficial](../build-phase11/validation-20261019-115314-1d807fdc/validation.json): ele interrompe a sequência após Debug falhar. Release foi concluído separadamente, com as mesmas fontes e critérios.
- [Primeira tentativa, interrompida na compilação](../build-phase11/validation-20261019-114948-3d2798a1/validation.json).

O comando principal foi `python tools/validate_phase11.py`. A continuação
Release usou a mesma árvore `host`, compilação/auditoria e `ctest -C Release
--output-on-failure --output-junit tests-Release.xml`.
