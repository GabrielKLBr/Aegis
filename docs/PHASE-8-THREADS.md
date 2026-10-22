# Fase 8 — Threads PE com dados privados e contenção

Data de referência: 17/10/2026. Estado da fase: **em andamento**.
Este terceiro marco continua o runtime da fase 8. A fase 9 não foi iniciada.

## Comportamento implementado

A DLL própria passa a exportar `CreateThread`, `ResumeThread`,
`GetExitCodeThread` e `SwitchToThread`. `WaitForSingleObject` e `CloseHandle`
aceitam os handles das novas threads; `ReleaseMutex` continua exigindo mutex.
A criação aceita execução imediata ou `CREATE_SUSPENDED`. Identidade, token,
direitos e processo de origem são determinados no kernel do convidado.

Cada worker recebe uma pilha com 60 KiB comprometidos e uma primeira página
não mapeada, além de uma página privada para TEB, vetor TLS, dois blocos TLS e
inicialização. O PEB e o heap são compartilhados pelo processo. A thread inicial
continua usando seu TEB privado na página de bootstrap. Não há estruturas do
host expostas à aplicação por essas APIs.

O bootstrap chama callbacks TLS e DLL com `DLL_THREAD_ATTACH` antes da função
da thread e `DLL_THREAD_DETACH` após seu retorno. Um mutex por processo serializa
essas chamadas e a inicialização/finalização do processo; o bloqueio é feito
pelo escalonador. O código dos callbacks pode ceder execução sem permitir que
outro callback entre ao mesmo tempo. O primeiro attach do processo usa o motivo
próprio de processo, sem duplicar o attach de thread.

Ao retornar, o resultado DWORD sinaliza o objeto de término. Já na tabela de
páginas do kernel, são liberados TEB, pilha, vínculo do processo e slot do
escalonador. O objeto de término sobrevive enquanto houver handles ou esperas:
um handle antigo mantém seu resultado depois de o slot receber outra geração
de thread. Fechar o último handle de uma thread ainda viva não elimina seu
vínculo interno. Permissões de consulta, espera e modificação são independentes.

A criação faz rollback se a reserva, o commit ou a preparação do TLS falhar.
A quota de quatro threads inclui a inicial. Endereços de entrada precisam estar
nas seções RX dos PEs carregados; esta restrição é mais estreita que a API do
Windows, que pode aceitar uma entrada inválida e falhar na execução posterior.

## Provas de comportamento

A aplicação própria cria três workers suspensos por rodada, em dez rodadas.
Confere `STILL_ACTIVE`, timeout antes de retomar, contagem anterior de suspensão,
limite de threads, resultado e espera repetida após término. Mantém o handle da
primeira thread durante todas as rodadas para verificar a preservação do objeto
antigo. Handles fechados e uso de thread como mutex são recusados.

Os 30 workers verificam TEB, pilha, identidade, TLS e último erro independentes.
Usam o mesmo heap e seção crítica, cedendo execução tanto dentro da seção
quanto com blocos de heap em uso. Há 360 incrementos protegidos por boot, com
preservação e zeragem dos dados após realocação. Uma thread adicional sai com um
mutex adquirido, exercitando abandono, espera, aquisição e liberação pelo outro
participante. Os 31 pares de callbacks de thread precisam estar equilibrados
antes do término do programa. O bloco TLS completo da DLL na thread inicial
precisa permanecer byte a byte igual durante todas as rodadas. A DLL também
cede execução em seus callbacks e
recusa reentrada concorrente.

O teste nativo mantém as 693 verificações anteriores e acrescenta **904**
verificações de ciclo de vida, incluindo 64 reutilizações, espera pendente,
permissões, resultado antigo após reutilização, recusa de reap de thread viva,
recusa de ID obsoleto e fechamento do handle antes da saída. A contagem final de
páginas, threads, handles, objetos e referências deve voltar à base inicial.

Uma prova dirigida reproduziu uma escrita indevida dos sentinelas FS/GS do
bootstrap antigo no TLS da thread principal. O teste do bloco completo recusou
a versão anterior à correção com status 51036, preservado em
`build-codex/phase8-concurrency-tls-before-fix.json`. O caminho de criação agora
reserva esses sentinelas ao workload antigo. A matriz final exige a preservação
do bloco inteiro e retorno zero com a correção.

O marcador `PE private TEB TLS thread lifecycle contention PASS` depende do
retorno zero da aplicação e da limpeza completa conferida pelo kernel.
A matriz usa somente PEs de referência do projeto, em CPL3 e WHP reais.

## Validação e preservação

O contrato exige 35 testes por configuração em Debug e Release, sem ignorados,
720 boots positivos (80 deste runtime e 640 de regressão), 240 cenários externos
de falha e os pares persistentes de arquivos/registro do marco anterior. Os
80 boots do runtime somam 2.480 workers e 28.800 incrementos protegidos.

São executados os sete programas de referência por configuração, auditorias das
nove imagens e provas da CLI. Uma árvore independente,
`build-phase8-concurrency-clean`, recompila as nove imagens de produção e faz
suas auditorias. Os nomes dos alvos de produção permanecem os mesmos.

Novas evidências e discos usam o prefixo `phase8-concurrency-`. O snapshot dos
17 originais é `snapshots/codex-before-phase8-concurrency-20261017`.
São conferidos por SHA-256 os 457 arquivos históricos de evidência e os discos
da fase 7 e dos dois marcos anteriores da fase 8. Não são alteradas opções de
boot, segurança ou NX do Windows.

Contrato: `config/phase8-concurrency-acceptance.json`.
Verificador: `tools/validate_phase8_concurrency.py`.
Manifesto: `build-codex/phase8-concurrency-change-manifest.json`.
Os documentos dos marcos anteriores conservam os resultados e limites históricos.

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild *> build-codex\phase8-concurrency-full-debug-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-concurrency-debug-tests-details.log
.\Build-Kernel.ps1 -Configuration Release -FullBuild *> build-codex\phase8-concurrency-full-release-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-concurrency-release-tests-details.log
cmake -S . -B build-phase8-concurrency-clean -G "Visual Studio 17 2022" -A x64 *> build-codex\phase8-concurrency-clean-configure.log
cmake --build build-phase8-concurrency-clean --config Debug --target guardian_guest --parallel 2 *> build-codex\phase8-concurrency-clean-debug-build.log
python tools\validate_phase8_concurrency.py --snapshot snapshots\codex-before-phase8-concurrency-20261017 --run-programs
```

## Limites e continuação

Este marco aceita quatro threads por processo e uma vCPU; não implementa SMP.
A reserva/commit de pilha é fixa; tamanhos personalizados, atributos de segurança
e outras flags de criação são recusados. O heap continua limitado a 8 KiB e
cada módulo recebe no máximo 256 bytes de TLS. `SwitchToThread` informa se havia
outro participante pronto e entrega a decisão ao escalonador existente.

Ainda há um único processo PE no teste. A criação de filhos PE e a vida completa
da thread principal/última thread, incluindo `ExitThread` explícito, continuam
pendentes. Os workers deste marco terminam retornando da função; a aplicação
principal espera por todos antes de retornar. O término com workers ainda
ativos precisa da próxima matriz de ciclo de vida. Não há suporte declarado a
código arbitrário, handlers SEH de usuário, unwind x64 em execução ou carga e
descarga dinâmica de DLLs. A validação final por aplicação permanece pendente.

Referências de contrato: [CreateThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread),
[GetExitCodeThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodethread),
[DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain) e
[SwitchToThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-switchtothread).
