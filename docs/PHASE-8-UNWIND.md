# Fase 8 — Unwind virtual x64 e captura de chamadas

Data de referência: 17/10/2026. Estado: **fase 8 em andamento**.
Este é o quinto marco. A fase 9 não foi iniciada.

## Resultado

O kernel agora restaura virtualmente o contexto de um chamador x64 usando
`.pdata` e `UNWIND_INFO` v1 dos módulos PE carregados. A operação trabalha
em uma cópia dos registradores: não modifica a pilha examinada, não retoma
código e não executa handlers. É uma base para o tratamento de exceções;
o despacho de exceções de usuário e o unwind não local continuam pendentes.

A DLL própria exporta um subconjunto de `RtlCaptureStackBackTrace`. O pedido
parte de uma syscall real em CPL3, com os registradores salvos pelo kernel.
As primeiras chamadas internas do runtime são omitidas. O programa recebe
até 16 endereços de retorno de seus módulos executáveis, pode omitir até
16 chamadores e pode solicitar a soma de 32 bits dos endereços como hash.
Uma captura bem-sucedida preserva `GetLastError`, inclusive quando omitir
chamadores produz uma lista vazia. Parâmetros fora desses limites falham.
O acesso a ponteiros inválidos do chamador ainda segue o comportamento fatal
de falha de página do convidado; esta API não implementa SEH para esses acessos.

O kernel obtém os limites da pilha pelo vínculo entre PID, TID e a alocação
privada da thread. Não confia em `StackBase`/`StackLimit` escritos no TEB pelo
usuário. A prova altera temporariamente esses campos e continua capturando
a pilha correta. As leituras passam pelas tabelas de páginas do processo,
com verificação de limites e overflow; nunca usam endereços do Windows host.

## Formato suportado

O motor sem CRT, STL ou Win32 trata funções folha, alocações pequenas e grandes,
push/save de registradores não voláteis, frame pointer, registradores XMM6–15,
frames de máquina e encadeamento de metadados. Em um prólogo parcial, executa
somente as operações já alcançadas. Reconhece epílogos com ajuste de RSP,
`lea` pelo frame pointer, pops, retorno, saltos diretos para fora da função
e salto indireto via slot RIP-relative. O percurso limita o encadeamento a
32 entradas e a captura a uma quantidade fixa de frames.

As tabelas pertencem a módulos já validados e protegidos pelo loader. O motor
confere novamente as entradas usadas, códigos, comprimentos, registradores,
parentes e leituras. Um erro não publica contexto parcialmente restaurado nem
um prefixo da captura. A chegada ao bootstrap fora dos módulos PE encerra o
percurso normalmente, sem tentar interpretar sua pilha de assembly.

Não há suporte declarado a metadados v2/v3, tabelas JIT, handlers de linguagem,
SEH/C++ exceptions ou todos os formatos possíveis de tail call. Não é uma
exportação completa de `RtlVirtualUnwind` ou de `CONTEXT` do Windows.
O processo continua sendo encerrado se uma exceção de usuário não tratada
atingir o caminho atual de traps.

Referências de formato: [unwind x64 da Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170)
e [prólogos e epílogos x64](https://learn.microsoft.com/en-us/cpp/build/prolog-and-epilog?view=msvc-170).
A implementação é própria e limitada aos caminhos descritos acima.

## Validação

O teste nativo aprova 2.166 verificações por configuração e compara os registradores restaurados com `RtlVirtualUnwind`
do Windows em 59 cenários. Cobre prólogos parciais, saves antes/depois da
alocação fixa, SIMD, frame pointer com pilha dinâmica, frames de máquina,
epílogos e encadeamento. Cada leitura desses cenários sofre uma falha injetada
individualmente: são 391 pontos, além de sete de um controle separado,
verificando que o contexto original permanece intacto.
Há testes adicionais de metadados inválidos, guardas, limites, captura, hash,
omissão de frames e falha posterior sem publicação de resultados parciais.
Essa chamada ao Windows existe somente no teste nativo de comparação.

No convidado, o console próprio percorre uma cadeia de chamadas que atravessa
o executável e a DLL. Confere os módulos dos frames, a lista com omissão, o hash,
a preservação de erro, limites falsos no TEB e rejeição de pedidos malformados.
A prova roda na thread inicial e em três workers com pilhas privadas. Mantém
integralmente a prova anterior com 31 workers, 360 incrementos protegidos,
23 processos filhos e dez falhas de criação por inicialização.
Por boot há 16 capturas com frames e quatro capturas vazias após omitir a cadeia.

O marcador `PE x64 virtual stack capture PASS` só sai depois do retorno zero
do console e da limpeza de todos os processos PE. O contrato atual exige
37 CTests em Debug e em Release, sem ignorados, e 800 boots na matriz completa,
com regressão das fases 1–7 e dos marcos anteriores da fase 8.

Os comandos e evidências deste marco usam o prefixo `phase8-unwind-`:

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
python tools\validate_phase8_unwind.py --snapshot snapshots\codex-before-phase8-unwind-20261017 --run-programs
```

O snapshot guarda os originais. O verificador confere os hashes das evidências
anteriores e dos discos das fases 7 e dos quatro marcos anteriores da fase 8.
O manifesto novo registra fontes, imagens, programas, relatórios e discos.
Nenhum ajuste de boot, NX ou segurança do Windows host faz parte deste marco.

## Próximo trabalho dentro da fase 8

Ainda faltam despacho de exceções para o usuário, chamada e retorno de handlers,
unwind não local, carregamento/descarregamento dinâmico de DLLs, expansão dos
casos de falha e a auditoria final de aplicações. A conclusão da fase exige
esses comportamentos; capturar chamadas não encerra a fase 8.
