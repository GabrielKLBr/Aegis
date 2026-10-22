# Fase 8 — Loader, runtime e aplicações de console

**Continuação em 18/10/2026:** exceções, unwind não local e DLLs dinâmicas
implementados e aceitos na matriz Linux/KVM. A aceitação Windows desta revisão
continua pendente. Estado atual e critérios: [PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md).
Os resultados abaixo registram os marcos históricos; a fase 9 não foi iniciada.

Base ativa: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Estado em 17/10/2026: **em andamento**. O primeiro marco, descrito abaixo, cobre
loader PE e console com DLL/TLS. O segundo marco conecta arquivos, registro,
console, heap e locks: [PHASE-8-RUNTIME.md](PHASE-8-RUNTIME.md).
A validação atual soma 35 testes por configuração e 720 boots.
A fase 9 não foi iniciada. Os números de 33 testes e 640 boots abaixo registram
a validação do primeiro marco.

## Implementação deste marco

`guest/PeLoader.cpp` reaproveita os conceitos do loader anterior do MiniNt
(PE32+, relocations DIR64 e resolução de imports), substituindo sua dependência
de Win32, STL e stubs do host por um backend freestanding. O parser, as
relocations, a ligação das DLLs e a aplicação das permissões executam em CPL0
do convidado. Os endereços gravados na IAT pertencem às imagens do convidado.

O carregador verifica cabeçalhos, tamanhos, alinhamentos, limites e sobreposição
de seções, diretórios, strings, descritores de import, thunks e exports. Aceita
imports por nome ou ordinal e a alternativa com `OriginalFirstThunk` zero.
DLLs são encontradas em uma lista explícita de fontes; nomes não viram caminhos
do Windows host. Dependências ausentes, ciclos, símbolos ausentes e recursos
sem implementação produzem falha. Uma carga rejeitada remove as novas imagens,
preservando dependências carregadas anteriormente. Falhas de remoção permanecem
rastreáveis e permitem nova tentativa de limpeza.

As imagens começam graváveis e sem execução. Depois dos fixups, os cabeçalhos
ficam somente para leitura; cada seção recebe R, RW ou RX, e lacunas ficam sem
acesso. Seções W+X e IAT dentro do código ou dos cabeçalhos são recusadas.
No convidado, essas permissões passam pelo gerenciador de memória existente e
pelas permissões físicas do WHP.

Há leitura e relocation dos diretórios TLS, atribuição do índice e validação
dos templates e callbacks. O bootstrap da matriz inicial instala o template e
a parte zerada, publica o vetor TLS no TEB, chama os callbacks e `DllMain` na
ordem de dependência, executa o ponto de entrada do console e faz o detach na
saída normal. Inicializa os campos usados pela aplicação: identidade, limites
da pilha, ponteiro do próprio TEB, vetor TLS, ponteiro para PEB e base da imagem.
Esses campos ainda não constituem uma implementação completa de PEB/TEB.

O loader verifica os limites da tabela `.pdata`, as faixas de funções e a
estrutura dos códigos de unwind versão 1 sem handlers. Uma função folha x64
pode dispensar essa tabela; a DLL otimizada em Release exercita esse caso.
**Ainda não há dispatch de exceções de usuário nem execução do unwind.**

## Aplicação e critérios de comportamento

Os arquivos em `guest/fixtures` são compilados pelo MSVC como um executável de
console e uma DLL PE reais, sem CRT: `guardian-pe-console.exe` e
`guardian-pe-runtime.dll`. Um gerador incorpora seus bytes ao kernel de teste.
Nenhum código de máquina desses PEs é remendado pelo host. As modificações de
relocation e IAT são feitas pelo carregador no convidado.

O console executa em CPL3, em espaço de processo privado, com pilha e proteção
de memória próprias. Os critérios exercitados são:

- Os dois módulos carregam fora dos endereços preferidos, com DIR64 aplicadas.
- Dois imports, por nome e ordinal, apontam para funções distintas da DLL.
- Os callbacks TLS de ambos os módulos executam antes dos respectivos pontos
  de entrada, e os acessos `__declspec(thread)` usam o vetor publicado no TEB.
- `DllMain` confirma o estado produzido pelo seu callback TLS.
- O console confere identidade, PEB e limites da pilha, obtém 42 da chamada à
  DLL e 17 da função importada por ordinal, e termina com código zero.
- A desmontagem e o término devolvem todas as páginas alocadas para a prova.

As mensagens `PE loader mapping checks PASS` e
`PE console TLS DLL execution PASS` só são emitidas depois dos respectivos
critérios. O relatório de processos conserva os contadores da matriz anterior;
a execução adicional do console é verificada separadamente por essas mensagens
e pelo teste de boot da fase 8.

## Validação e evidências

A validação deste marco exige Debug e Release completos, 33 testes por
configuração e nenhum teste ignorado. A matriz soma 640 boots: 80 com o novo
loader/console e 560 de regressão das fases 1–7. Cada boot novo verifica 26
condições de mapeamento antes de executar o console e a matriz anterior.
Os testes anteriores mantêm seus 240 cenários externos de falha no total.

O teste nativo executa o mesmo `PeLoader.cpp`, usando os dois PEs compilados e
um backend que aplica permissões. Cobre arquivos truncados, cabeçalhos e
diretórios malformados, sobreposições, W+X, imports, ordinais, DLLs, TLS e
metadados de unwind inválidos. Injeta falha em cada operação de backend do
caminho de carga e verifica rollback e ausência de módulos publicados. Também
exercita dependência pré-carregada e falha de unmap com nova tentativa.
Os totais de cada configuração ficam em `phase8-change-manifest.json`.

Uma árvore nova, `build-phase8-clean`, valida a compilação das oito imagens de
produção e seus contratos PE. Os sete exemplos de referência do MiniNt são
executados por configuração, junto com auditorias e provas pela CLI.
Arquivos de evidência e discos novos usam prefixo `phase8-`. Os 347 arquivos
históricos registrados no snapshot e os 141 artefatos de disco da fase 7
continuam verificados por SHA-256, sem sobrescrita.

Os comandos atuais estão em [PHASE-8-RUNTIME.md](PHASE-8-RUNTIME.md).
Os relatórios `phase8-*` deste primeiro marco são históricos; a continuação
usa `phase8-runtime-*` para preservá-los. Os manifestos mantêm explicitamente
`status: in_progress` e `next_phase_started: false`.

## Limites e trabalho restante da fase 8

O loader aceita até quatro módulos de 128 KiB e 16 seções por módulo, com
alinhamentos de seção de 4 KiB e de arquivo de 512 bytes. Há limite de 256
relocations DIR64, 256 exports, 256 thunks por descritor, oito callbacks TLS por
módulo e 256 bytes de TLS por módulo. A matriz de execução atual usa dois
módulos e uma thread por processo PE. A imagem do kernel permanece limitada
a 1 MiB, sem imports, TLS ou relocations próprios.

Forwarders, imports adiados/bound, handlers SEH e outros
diretórios sem implementação são recusados. O carregamento é inicial e segue
um catálogo fixo de arquivos próprios; ainda não há `LoadLibrary` de usuário.
O console de prova não usa CRT nem DLLs do Windows. Não há alegação de executar
programas Windows arbitrários ou de compatibilidade geral.
Atualização do segundo marco: cadeias de metadados de unwind agora são
validadas para aceitar a saída otimizada do compilador, ainda sem executar
o unwind. Veja [PHASE-8-RUNTIME.md](PHASE-8-RUNTIME.md).

Para concluir a fase 8, ainda faltam:

Atualização: os itens 1 e 2 abaixo já possuem o subconjunto inicial validado
no [segundo marco](PHASE-8-RUNTIME.md). As ampliações e limitações restantes
estão nesse documento; filhos/threads PE, exceções e DLLs dinâmicas seguem pendentes.

1. Conectar o runtime PE às APIs NT/Win32 da matriz de console, incluindo
   arquivos, registro, erro e saída de console.
2. Integrar heap e locks reais ao runtime em modo usuário.
3. Criar filhos e threads a partir de PEs, com TEB/TLS privados por thread,
   callbacks de thread, cancelamento e limpeza nos caminhos de falha.
4. Implementar dispatch de exceções de usuário e unwind x64, com provas de
   recuperação e propagação entre funções.
5. Completar carga/descarga dinâmica de DLLs e a matriz de falha na inicialização,
   execução e término, com critérios por aplicação.
6. Reunir a auditoria final da fase e parar antes da fase 9, conforme solicitado.

Referências de formato: [PE/COFF da Microsoft](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
e [tratamento de exceções x64](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).
