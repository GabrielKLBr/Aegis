# Fase 8 — Exceções, DLLs dinâmicas e aceitação final

**Continuação posterior:** a fase 9 foi autorizada pelo usuário e está em
validação em [PHASE-9.md](PHASE-9.md). A aceitação Windows desta fase 8 continua
pendente. O registro abaixo descreve a entrega anterior a essa autorização.

Atualização: 18/10/2026. **Implementação concluída para a matriz de consoles
próprios; aceitação no Linux aprovada; aceitação nativa no Windows pendente.**
A fase 8 ainda não está encerrada no produto Windows. A fase 9 não foi iniciada.

## Resultado e evidências

Foram implementados os itens funcionais que faltavam: despacho e continuação
de exceções x64 em modo usuário, unwind não local com `__finally`, carga e
descarga dinâmica de DLLs, TLS por thread, rollback e testes de falhas no
início, durante a execução e no término dos processos. Os consoles anteriores
continuam na regressão. O suporte Windows/MSVC/MASM/WHP permanece na árvore;
a execução desta entrega usou ferramentas ELF e KVM, sem Wine ou QEMU.

Evidência principal:
`build-linux-kvm/validation-20261018-050639-287554/validation.json`.
O arquivo `phase8-completion.json`, na mesma pasta, registra explicitamente
`linux_passed`, `awaiting_native_windows_acceptance` e `native_windows: not_run`.
Os manifestos incluem hashes das fontes e artefatos. Nenhum teste foi ignorado.

| Verificação executada | Resultado |
| --- | --- |
| Debug, Release e AddressSanitizer/UndefinedBehaviorSanitizer | 20 CTests em cada configuração: 60 execuções aprovadas |
| 11 imagens de produção × Debug/Release × 16/32/64 MiB × dois boots | 132 boots reais aprovados |
| Falha fatal e double fault com IST, em Debug/Release | Quatro execuções corretamente recusadas como sucesso |
| Console final com exceções e DLLs dinâmicas | 12 boots na matriz de RAM/configurações, incluídos nos 132 |
| Motor de despacho/continuação/unwind não local | 415 verificações e 64 pontos de falha de leitura por configuração |
| Loader PE | 1.651 verificações; 49 pontos de falha na carga dinâmica, além dos 181 e 192 pontos anteriores |
| Busca de handlers | 3.059 verificações, 155 cenários e 980 falhas de leitura injetadas |
| Unwind portátil anterior | 1.045 verificações, 59 cenários e 391 falhas de leitura, mais o controle de sete pontos |
| Persistência | Segundo boot reutiliza o overlay, recupera a sequência anterior e mantém a base intacta |
| Windows/MSVC/WHP e comparações com APIs Microsoft | Não executados nesta sessão |

## Pacote Linux entregue

`dist/guardianav-phase8-linux-x86_64-20261018.tar.gz` contém os executores ELF,
13 imagens de kernel, assinaturas, documentação e evidências. SHA-256:
`4dfa540575a990d821d3a7360672b11ca7f815d88ca13852466916b39903c895`.
O pacote foi extraído em pasta temporária; os 33 hashes foram conferidos.
Capacidades, scanner, hash e inspeção funcionaram a partir de outra pasta.
A imagem `complete` realizou dois boots adicionais pelo pacote extraído,
recuperando a sequência 0 → 3 → 6 no mesmo overlay, com a base intacta e
recursos vivos zerados. Evidência: `package-validation.json` na pasta principal
de validação acima. Os pacotes anteriores permanecem preservados.

## Comportamento exigido por aplicação

O sucesso depende das verificações internas de cada programa e dos contadores
finais do kernel. Retornar sem falha não é o único critério. A imagem
`guardian-kernel-complete.gk` incorpora o console final, o filho, o runtime e as
DLLs; os arquivos PE próprios executam dentro do convidado.

| Aplicação/componente selecionado | Critérios exercitados e aprovados no Linux |
| --- | --- |
| Console inicial e DLL/TLS | Relocations, imports por nome/ordinal, TLS, callbacks, chamada da DLL, resultado e detach |
| Console de runtime | Arquivos, registro, console, heap, locks, permissões e recuperação após reinício; saída `PE RUNTIME OK` |
| Console de threads/lifetime, incorporado ao console final | Pilhas, TEB/TLS privados, 31 workers da prova anterior, contenção, filhos, saída explícita e continuação sem a thread principal |
| `guardian-complete-console.exe` — exceções | UD2 real recuperado com RIP/RAX alterados, busca de handler, divisão por zero, execução de `__finally`, retomada no `__except` e `RaiseException` com argumentos 7/11 |
| `guardian-complete-child.exe` — falhas | Sete modos conferem códigos de saída para exceção não tratada, contexto/MXCSR inválido, exceção aninhada, continuação proibida, destino de unwind forjado e acesso à arena privada do kernel |
| `guardian-dynamic.dll` | Quatro ciclos de carga/descarga; exports por nome/ordinal; referências sem reinicialização; TLS em thread preexistente e em novas threads; 64 ciclos concorrentes de referência por workers; unwind cruzando a DLL, `finally` e detach observáveis |
| `guardian-failing.dll` | Oito recusas de inicialização com repetição limpa; exceção em callback de attach e em detach durante saída do processo contidas; recursos recolhidos |
| Filho com referência dinâmica pendente | Saída normal drena referências e executa detach; código 17 preservado |

Os sete modos negativos conferem, respectivamente, `C000001D`, `C0000026`,
`C000001D`, `C0000025`, `C0000026`, `C0000029` e `C0000005`. Falhas de callback
encerram o processo afetado. O pai verifica os resultados e continua.
No relatório final, processos, objetos, handles e referências vivos são zero;
não há drivers, pedidos I/O, pins, handles de volume ou watches restantes.
A diferença de páginas livres continua igual à reserva fixa anterior (21).

Marcadores adicionais obrigatórios do boot final:

```text
PE x64 exception dispatch recovery nonlocal unwind PASS
PE dynamic DLL TLS references rollback unload PASS
```

## Implementação e limites

`shared/guardian/Exception.h` define os layouts x64 de registro, contexto e
dispatcher. `guest/ExceptionDispatch.cpp` procura handlers e prova os frames e
o destino antes de iniciar unwind não local. A restauração valida RIP, RSP,
flags, seletores e MXCSR; os limites de pilha vêm do vínculo PID/TID no kernel.
Estado privado e contexto original ficam na arena de supervisor reservada no
BootData (`0x110000` a `0x130000`). O loader privado usa a página seguinte.
A própria matriz confirma que CPL3 não consegue ler essa arena.

`guest/PeExceptions.cpp` publica uma cópia limitada na pilha da thread e
retoma o trampoline em CPL3. Handlers e callbacks de DLL executam em CPL3.
O kernel confere thread, token e contexto na resposta. A restauração conserva
FS/GS e o estado de segmentos sob controle do kernel. Não há chamada direta a
código de aplicação em CPL0 nem remendo de bytes de instruções do convidado.

O runtime oferece o subconjunto C SEH x64 versão 1, `__C_specific_handler`,
`RaiseException` e `RtlUnwindEx` necessário às provas selecionadas. Há limites
de 32 frames e 64 entradas de escopo. `RtlUnwindEx` requer despacho ativo,
frame de destino provado e endereço de continuação dentro da função alvo.
Exceções aninhadas, collided unwind, C++ EH, exit unwind para frame nulo e
contextos AVX/debug não fazem parte do contrato desta etapa.

`LoadLibraryA`, `FreeLibrary` e `GetProcAddress` usam um catálogo explícito de
quatro módulos por processo, sem procurar DLLs ou caminhos do host. Há até
64 referências dinâmicas por módulo, imagem de até 128 KiB e quatro slots TLS
de até 256 bytes. Callbacks são serializados pelo lock do loader e os planos
de attach/detach são atualizados para evitar ponteiros de módulo descarregado.
Novos mappings, referências e TLS pertencem à transação; `DllMain` que recusa
a inicialização provoca detach do prefixo inicializado e rollback. Dependências
continuam mapeadas enquanto forem alcançáveis por módulos fixos ou referências.
Falha ao remover memória conserva a propriedade para nova tentativa de limpeza.

Exports de dados, forwarders, delay/bound imports, CRT e DLLs arbitrárias não
são declarados compatíveis. Exceções em callbacks de inicialização/término
são testadas pelo caminho que encerra o processo e recolhe seus recursos;
recuperar e continuar a mesma transação após essa exceção não é suportado.

O KVM não oferece neste backend a permissão independente de execução física
por página usada pelo WHP: o NX vem das tabelas do convidado, e o relatório
publica `secondStageExecuteProtection: false`. A aceitação Linux não substitui
a proteção/execução do produto Windows. Detalhes: [LINUX-KVM.md](LINUX-KVM.md).

## Repetir e encerrar a aceitação

No Linux, com KVM acessível e LLVM nativo disponível:

```bash
python3 tools/validate_phase8_complete.py
```

No Windows x64 nativo, com Visual Studio 2022 C++, Windows SDK, CMake 4+,
Python e WHP funcionando, executar da pasta do projeto:

```powershell
python tools\validate_phase8_complete.py
```

O comando Windows usa uma árvore nova e exige **40 CTests em Debug e Release**,
incluindo o console final em WHP, os testes de despacho e **153 comparações de
handlers + 59 de unwind com `RtlVirtualUnwind`**. Esses resultados ainda não
existem para esta revisão. O histórico Windows anterior permanece preservado.
Depois de essa matriz passar, seus manifestos devem ser ligados a este registro
antes de marcar a fase 8 encerrada. A fase 9 depende de nova solicitação.
Contrato: `config/phase8-completion-acceptance.json`.

A regressão detectou que kernels antigos recebiam `InvalidHandle` na consulta
de DLLs pendentes durante shutdown. O caminho agora devolve `Unsupported`
explicitamente, e o runtime trata apenas a ausência declarada desse recurso.
A execução completa foi repetida após a correção. A tentativa anterior ficou
preservada em `build-linux-kvm/validation-20261018-050301-157944/` com falha.

Originais preservados e manifesto:
`snapshots/codex-before-phase8-completion-20261018/`.
Os registros dos marcos anteriores continuam válidos para as revisões neles
indicadas: [loader](PHASE-8.md), [runtime](PHASE-8-RUNTIME.md),
[threads](PHASE-8-THREADS.md), [lifetime](PHASE-8-LIFETIME.md),
[unwind](PHASE-8-UNWIND.md) e [preparação de handlers](PHASE-8-HANDLERS.md).

Referências dos layouts e contratos consultados:
[tratamento de exceções x64](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64),
[CONTEXT x64](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-context),
[RtlUnwindEx](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlunwindex) e
[NTSTATUS](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55).
