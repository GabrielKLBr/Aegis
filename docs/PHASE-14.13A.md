# 14.13A — Preparação automática e integração do runtime

Continuação delimitada da 14.13 em 29/10/2026. O perfil permanece NT
10.0.19045, com os contratos parciais de DLLs já descritos nas fases anteriores.
Esta entrega não inicia a aceitação ampla da 14.14.

## Comportamento

Um EXE sem cadastro individual pode usar um dos modelos locais provisionados.
O controlador lê seu PE32/PE32+, confere importações normais e adiadas, nomes,
ordinais e encaminhamentos de exports. Seleciona um modelo compatível com a
arquitetura, reconstrói o volume em memória com os bytes exatos do EXE e refaz
os vínculos de integridade. O catálogo original permanece sem alterações.

Os três modelos padrão cobrem o recorte de frameworks/CRT/GDI x64, USER32 básico
x64 e compat32. O primeiro inclui as dependências selecionadas de MSVCP,
MSVCRT, WinMM, shell e controles. Isso disponibiliza DLLs para resolução;
não torna suas APIs completas. Uma função pode existir em outro pacote e não
estar disponível no modelo selecionado. A lista `unresolved`, limitada a 128
entradas, identifica os requisitos indisponíveis. Importações dinâmicas ainda
dependem da validação durante a execução.

Não há compilação, Python ou busca de DLLs no Windows durante a análise.
Também não há execução do EXE nem carregamento de suas DLLs no host. Os únicos
helpers iniciados continuam sendo os executáveis conhecidos junto ao controlador,
com o hash esperado. Arquivos auxiliares do aplicativo e DLLs particulares não
são copiados automaticamente da pasta escolhida; exigem um pacote preparado.

Na página Sandbox do app:

- **Analisar um arquivo** prepara e executa pelo serviço;
- o progresso mostra conexão, preparação, conferência e execução;
- **Cancelar** interrompe a conexão e encerra o job com os processos da sessão;
- **Salvar ponto** inicia um EXE com USER32 básico e salva após 12 requisições
  nativas, ou ao atingir a janela de cinco segundos;
- **Retomar ponto** reutiliza o último ponto salvo, ou permite escolher
  `recovery.json`. O payload necessário é guardado como `payload.bin` junto dele.

O botão **Bundle preparado** mantém a operação explícita anterior da 14.10G.
Ele não é um fallback para EXEs que falharam no runtime unificado.

Salvar e retomar são operações próprias do protocolo do serviço. A restauração
automática conserva o modelo usado originalmente, inclusive quando outro modelo
também resolveria as importações do EXE. Permanecem obrigatórias as identidades
do controlador, catálogo, aplicação, política e snapshot.

O instalador copia o catálogo para a pasta versionada protegida dos serviços,
com limites de membros e tamanho. Sem `sandboxRuntime` explícito, o controlador
usa `runtime` ao lado do executável. A configuração explícita continua tendo
precedência. O serviço roda o worker restrito na sessão do usuário.

O pedido externo aceita até 60 segundos, mas a execução do kernel é limitada
a 30 segundos e informa `executionBudgetMs`. Salvar/retomar usam temporariamente
até 1536 MiB por processo e 2048 MiB por job para acomodar as cópias da RAM de
256 MiB durante a serialização. As análises normais conservam suas cotas.

## Preparação local

Depois de compilar as dependências desta entrega, provisione um diretório novo:

```text
python tools/provision_runtime.py --build BUILD --config Release --registry BUILD/Release/runtime --work WORK --reference CRT_LOCAL --schema-reference SCHEMA_LOCAL
```

As referências Microsoft devem ser locais e obtidas legalmente. O catálogo
gerado não vai ao repositório público nem ao arquivo de build desta entrega.
O instalador existente usa o catálogo ao lado do pacote do serviço. Novas
versões dos helpers ou kernels exigem reprovisionamento.

```text
guardian-runtime --input PROGRAMA.exe --runtime-root CATALOGO --output NOVA_SESSAO
guardian-engine-service --sandbox PROGRAMA.exe
guardian-engine-service --checkpoint PROGRAMA.exe
guardian-engine-service --resume CAMINHO/recovery.json
```

## Validação do lote

`tools/validate_runtime_integration.py` verifica somente os fluxos alterados e
as dependências utilizadas: **20/20 Release e 20/20 Debug**. Inclui provisionamento,
PE malformado, reconstrução sob ASan, x86 e CRT x64 sem cadastro individual,
checkpoint/restauração de GUI, integridade, travessia de caminhos, recusa de
checkpoint GDI, prazo, fila do scanner e programas independentes.
São 1500 mutações e 192 truncamentos por configuração no teste do leitor PE.

`tools/validate_runtime_services.py` verifica **11/11** casos nos serviços
instalados em Release: hashes atuais, catálogo/helpers, restrições do worker,
pacote automático, progresso, salvar/retomar, cancelamento, controles
independentes, antivírus e estabilidade dos fontes/binário. Os dois serviços
foram efetivamente atualizados. A interface foi compilada em Release e Debug;
não houve uma bateria automatizada de cliques em toda a interface.

`tools/package_runtime_integration.py` exige essas provas atuais e gera o
arquivo para o repositório privado de builds. O pacote extraído passa novamente
pelos 20 casos do lote em ambas as configurações, com referências locais.
As suítes completas das fases antigas não foram repetidas.

## Programas reais e próximos bloqueios

Os EXEs locais foram mantidos sem modificações. Sua identificação exata consta
dos SHA-256 nos resultados privados. Compatibilidade parcial não conta como
execução concluída:

| Programa | Resultado desta rodada | Próximo bloqueio observado |
|---|---|---|
| Hello World em C independente | Concluiu no MiniNT x86 | Corpus pequeno, compilação descrita na 14.12 |
| 7-Zip, `7z.exe` | Recusado antes da execução | Ordinais de OLEAUT32, APIs de token, console, arquivos e processos |
| Bloco de Notas, `notepad.exe` | Recusado antes da execução | Diálogos, menus, controles, contratos API-set e impressão |
| Flappy Window 1.5 | Carregou e executou parcialmente; inconclusivo | `settings.txt` ausente do volume e limitações do CRT, incluindo `HeapSetInformation` |

Ordem proposta para o trabalho seguinte: empacotamento explícito dos arquivos
auxiliares e bloqueios do CRT observados; resolução de contratos/ordinais e APIs
centrais apontados pelo corpus; continuidade da 14.12 para x86 e GDI; expansão
do corpus e perfis na 14.14. Cada nova etapa requer sua própria aceitação.

GDI/DirectX/áudio vivos, timers ou sends pendentes e rede conectada continuam
fora do contrato de checkpoint. Um resultado completo continua significando
execução concluída dentro da cobertura declarada, sem certificar que o arquivo
é seguro ou que todas as funções de DLL foram observadas.
