# 14.14 — Linha de base de compatibilidade e prontidão para a fase 15

**29/10/2026: avaliação reproduzível entregue; aceitação de compatibilidade ampla
não atingida.** Esta entrega mede o fluxo unificado da 14.13A. Não implementa
as APIs que faltam nem inicia a fase 15.

## Protocolo congelado antes da campanha

A política estabelecida na 14.1 foi preservada: pelo menos 32 aplicações,
quatro em cada uma das oito categorias, oito por arquitetura nativa x86/x64,
75% funcionais por perfil e 50% por categoria. Fixtures próprias não contam;
versões alternativas de um mesmo programa não ampliam o denominador.

O [corpus congelado](../config/nt-compatibility/phase14.14-corpus.json) registra
32 identidades de aplicação, versões por recurso `VS_FIXEDFILEINFO`, SHA-256,
arquitetura, origem, passos e critérios de sucesso. São **13 x86, 18 x64 e um
AnyCPU**. AnyCPU não é contado como x86. A seleção, o congelamento e a primeira
campanha foram feitos nessa ordem. Nenhum programa foi substituído após falhar.
O identificador do plano é
`c49fc515bb21d1b0fbc84722f1c0f29577f0e9228823eb9f34970277e01b7ed1`.

A amostra é de conveniência: programas instalados e componentes Windows
disponíveis localmente, com concentração em produtos Microsoft. São aplicações
distintas, não 32 fornecedores ou projetos independentes. Não representa o
ecossistema Windows e não sustenta uma afirmação sobre a maioria dos programas.
Os executáveis foram copiados para um cache local, sem alterações, e conferidos
antes/depois da campanha. Nem eles nem as DLLs Microsoft são redistribuídos.

O Hello World vem do fonte C independente já fixado na 14.12. Seu fonte está
inalterado, mas o binário usa uma entrada própria e um subconjunto literal de
CRT. Essa adaptação consta no plano. Não é um executável Windows de distribuição
original. A versão sem recurso de versão é identificada pelo hash e proveniência.

As expectativas de Sort, Findstr, More e Ping foram consultadas na documentação
primária: [Sort](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sort),
[Findstr](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/findstr),
[More](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/more) e
[Ping](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/ping).
Essas expectativas não substituem uma referência nativa executada.

## Método e fronteira da medição

`tools/compatibility_campaign.py` congela entradas, executa os controladores e
agrega resultados por aplicação, categoria, arquitetura, perfil e configuração.
Usa os binários estáveis da 14.13A, com hashes registrados; não recompila nem
atualiza os serviços instalados. Uma fixture própria verifica WHP/VM antes de
cada configuração, fora do denominador independente.

Cada EXE do perfil oferecido é submetido ao controlador com o catálogo padrão
da 14.13A e orçamento de quatro segundos de execução. A preparação mantém seus
limites de DLLs, imagem de 8 MiB e armazenamento. A maioria é recusada na
preparação, antes de entrar no aplicativo. `dispatchAttempted` indica chamada
ao controlador, não comprovação de execução do EXE no convidado.

O único oráculo automatizado de uso completo desta campanha é o Hello World:
linha exata esperada, saída zero, relatório completo e ausência de recursos vivos
ou fault. Os demais têm roteiros completos congelados, mas essa campanha faz
uma sondagem de inicialização. O harness ainda não fornece argumentos, arquivos
auxiliares, servidores de laboratório ou automação específica desses roteiros.
Se a inicialização já é rejeitada, o bloqueio impede o roteiro; se o aplicativo
chega a executar, ele permanece parcial sem testemunho do uso previsto.
Janela criada, export resolvido ou código de saída zero não bastam.

São registrados estes resultados:

- **functional:** o roteiro tem evidência suficiente do uso previsto;
- **partial:** carregou/executou, mas o roteiro não foi concluído ou verificado;
- **incompatible:** o fluxo atual encontrou uma barreira de preparação, formato,
  capacidade ou kernel antes de obter uso verificável;
- **not-evaluated:** o perfil não é oferecido pelo runtime unificado.

Uma falha de infraestrutura reconhecida invalida a campanha, sem reduzir
artificialmente a taxa de compatibilidade. Fontes, binários, plano e entradas
devem permanecer estáveis. Toda célula pertence ao denominador congelado;
perfis sem execução conservam taxa observada nula em JSON (`null`), e não uma
taxa medida de 0%. Não se troca o identificador NT para fingir outra execução.

O executável selecionado e suas DLLs não são executados/carregados no host.
Somente controladores/helpers próprios são iniciados. Não houve execução nativa
dos programas de referência, alteração de proteções do Windows ou pedido de
elevação. A coleta do recurso de versão é leitura de metadados, não validação
da assinatura ou do fornecedor.

## Resultado

Matriz por aplicação e dados completos sanitizados:
[tabela](PHASE-14.14-RESULTS.md) · [JSON](PHASE-14.14-RESULTS.json).

| Configuração | Perfil | Funcionais | Parciais | Incompatíveis | Não avaliados |
|---|---|---:|---:|---:|---:|
| Release | NT 10.0.19045 | 1/32 | 1 | 30 | 0 |
| Debug | NT 10.0.19045 | 1/32 | 1 | 30 | 0 |
| Release e Debug, cada | NT 6.1.7601 | 0 | 0 | 0 | 32 |
| Release e Debug, cada | NT 10.0.22631 | 0 | 0 | 0 | 32 |

**3,125% do corpus selecionado** completou o roteiro no perfil Win10, nas duas
configurações. É somente o Hello World adaptado. Entre os 31 executáveis de
aplicações/componentes instalados, nenhum completou seu roteiro. O Flappy
executou parcialmente; não foi comprovada uma partida jogável.

No Win10, as barreiras se repetiram em ambas as configurações:

| Barreira | Aplicações |
|---|---:|
| Importações indisponíveis nos modelos automáticos | 25 |
| Imagem maior que o limite atual: CMake, qBittorrent e Audacity | 3 |
| Rejeição pelo leitor/loader/kernel: FileZilla e NASM | 2 |
| Execução parcial: Flappy | 1 |
| Roteiro observado: Hello World | 1 |

FileZilla atingiu `Import directory quota`. NASM foi rejeitado pelo loader,
com `error=7` e status do kernel 50712; não foi uma falha de execução nativa do
NASM no Windows. O Flappy esbarra em recursos auxiliares e limitações do CRT.
Ocorrências frequentes incluem `kernel32!FormatMessageW` e exports de MSVCRT
x86. Um símbolo ausente do modelo pode existir em outro pacote; o relatório
não o transforma automaticamente em API sem implementação em todo o projeto.

O runtime unificado oferece apenas o perfil Win10. Não foi avaliada conformidade
nativa dos builds exatos 7601, 19045 e 22631. Componentes extraídos de uma
instalação Windows mais nova podem exigir aquele sistema; seus resultados aqui
não demonstram regressão em comparação com Windows 10 real. A seleção por perfil
e as referências nativas precisam ser ampliadas numa futura campanha.

As 192 células são 32 aplicações × três perfis × duas configurações; não são
192 aplicações nem 192 execuções de aplicativos no convidado. Houve 64 chamadas
de avaliação ao controlador, e 128 células de perfil não oferecido. Nenhuma
categoria atingiu 50%; console chegou a 25%. A meta de 75% por perfil não foi
atingida. Contagem de funções catalogadas não equivale a aplicações funcionais.

## Desempenho e limites da observação

Cada célula executada registra tempo total, CPU, picos de working set/commit
privado e bytes de I/O **do controlador**, incluindo preparação e relatórios.
Os dados não incluem processos de RAM/helper, não medem RAM física total e não
devem ser apresentados como consumo total do sandbox. Os boots registram
separadamente RAM configurada da VM, 256 MiB, e duração do trecho do convidado.

A mediana de tempo do controlador foi aproximadamente 176 ms em Release e
172 ms em Debug, dominada pelas recusas de preparação. Os maiores picos de
commit privado do controlador foram aproximadamente 23,81 e 32,79 MiB.
São observações de uma rodada sob carga do host, sem referência nativa,
intervalos estatísticos ou comparação de desempenho entre sistemas NT.
Não medem desempenho de uma aplicação funcional típica nem garantem ausência
de vazamentos em uso prolongado.

## Pendências anteriores e prontidão para a fase 15

As suítes completas das fases antigas não foram repetidas. O resultado Windows
de [12B–13](REVISAO-WINDOWS-FASES12B-13-20261023.md) é histórico; esta campanha
não o invalida e também não o converte em compatibilidade ampla.

| Área anterior | Limite relevante preservado | Efeito na próxima fase |
|---|---|---|
| 8 e 14.2–14.12: PE, DLLs, processos | ABI, arquiteturas, imagens e APIs parciais; não é um NT completo | Exigir resultado inconclusivo quando faltar cobertura |
| 9–10 e 14.9: dispositivos/rede | Este fluxo está offline, sem fixture de rede dos roteiros | Não alegar análise de comportamento de rede real nesta campanha |
| 11, 12F e 14.10: gráficos/entrada | Ponte de janelas e chamadas tipadas, sem compatibilidade gráfica total | UI do produto deve explicar recurso indisponível |
| 12B–12E: ambiente/semântica | Comparação exata com versões NT e avaliação representativa continuam pendentes | Nenhuma alegação de invisibilidade do sandbox |
| 13: análise | Telemetria de operações instrumentadas; nem toda função local de DLL gera evento | Compatibilidade não mede detecção de malware |
| 14.13: recuperação | USER32 básico, uma vCPU; GDI/DirectX/áudio vivos e conexões abertas fora do contrato | Quarentena não pode depender de um snapshot completo de qualquer app |

**Pronto para começar a implementação da fase 15, com escopo de produto básico;
não pronto para lançamento com promessa de compatibilidade ampla.** A quarentena
pode ser desenvolvida/testada com arquivos inertes e a verificação estática,
sem depender de executar programas incompatíveis no MiniNT.

A revisão de `src/Quarantine.cpp` encontrou armazenamento XOR simples,
metadados não transacionais e um fallback de truncamento do original após falha
de exclusão. A fase 15 deve substituir esse fluxo antes de sua aceitação:
integridade, identidade do arquivo, permissões, publicação durável, conflitos,
falta de espaço e falhas precisam preservar o original. A campanha não acionou
essa quarentena nem modificou os arquivos originais dos programas.

Os próximos lotes de compatibilidade ficam registrados: argumentos e assets
explícitos; CRT/API-set/ordinais e funções centrais x86; quotas/loader para os
casos identificados; depois roteiros completos, referências e perfis NT.
Esses lotes não foram iniciados. Não há medição de eficácia antimalware,
falsos positivos, resistência a escape ou certificação de lançamento aqui.

## Reprodução e pacote

```text
python test/CompatibilityCampaignTests.py RESULTADO.json
python tools/compatibility_campaign.py freeze --selection SELECAO --bindings CAMINHOS_LOCAIS --policy POLITICA --output NOVO_PLANO --cache NOVO_CACHE
python tools/compatibility_campaign.py run --plan PLANO --cache CACHE_CONGELADO --build BUILD --config Release --output NOVA_PASTA --reference CRT_LOCAL --schema-reference SCHEMA_LOCAL
python tools/compatibility_campaign.py report --plan PLANO --release RESULTADOS_RELEASE --debug RESULTADOS_DEBUG --output RELATORIO.json
```

O cache local exige os mesmos bytes por SHA-256; versões diferentes requerem
um novo plano, sem sobrescrever a campanha histórica. Caminhos locais não vão
ao GitHub público. `test/CompatibilityCampaignTests.py` aprovou **24/24** casos
de política, identidade, denominadores, oráculos, integridade e erros de
infraestrutura. Isso valida a medição, não aprova compatibilidade dos programas.

`tools/package_compatibility_campaign.py` exige as provas atuais em Release e
Debug e esses testes. O arquivo privado contém fontes, controladores próprios
e evidências, sem EXEs de terceiros, DLLs Microsoft ou snapshots de memória.
A extração é conferida por hash; o coletor e os testes são executados a partir
do pacote, incluindo um caso funcional, uma recusa por importações e perfis
sem suporte. A fase 15 aguarda nova solicitação.
