# Aegis — fontes no GitHub e automação Git

Aegis significa **Advanced Endpoint Guardian for Inspection & Sandboxing**.
O repositório público é <https://github.com/GabrielKLBr/Aegis>.
O nome do projeto no GitHub muda; os executáveis e contratos `guardian-*`
mantêm seus nomes para preservar compatibilidade.

### Separação aprovada em 23/10/2026

O Aegis público recebe os fontes completos, incluindo o runtime MiniNT e os
perfis existentes, sem substituir implementações por retornos nulos.
As builds históricas ficam em **https://github.com/GabrielKLBr/Aegis-builds**, privado,
como arquivos da branch, sem criar GitHub Releases. As validações Windows e a
fase 14 estão documentadas nos respectivos registros de entrega.

Para exportar seleções explícitas de builds, preservando os originais:

```text
python tools/aegis_git.py prepare-builds --output PASTA_NOVA --entry fase14=PASTA_DA_ENTREGA
python tools/aegis_git.py --root PASTA_NOVA publish-builds --repo SUA_CONTA/Aegis-builds --create
```

Pode repetir `--entry ROTULO=CAMINHO` para cada diretório ou pacote existente.
A exportação gera ZIPs com manifesto SHA-256 dos pacotes e arquivos internos;
recusa links, formatos conhecidos de credenciais e discos/estado pessoal de VM.
Pacotes acima de 90 MiB precisam ser separados em seleções menores. O envio
confere o manifesto e exige que o destino seja privado, inclusive antes do push.
Não transforma automaticamente um repositório público em privado. Para atualizar
uma exportação posterior, preparar outra pasta e integrar seus pacotes/manifesto
ao clone privado antes do comando, omitindo `--create`.

As builds são registros históricos: não são recompiladas para fingir que houve
nova aceitação. Fases sem binário preservado devem aparecer como ausentes no
inventário. Não se incluem caches/intermediários do compilador nem imagens de SO.

## Conteúdo publicado

Fontes C/C++, assembly, Python, configuração, documentação e imagens da
interface. Não entram builds, releases, pacotes, snapshots, discos virtuais,
executáveis, DLLs, bibliotecas compiladas ou kernels compilados. Até os binários
antigos em `test/` ficam de fora; seus fontes e inventários permanecem.
Os arquivos locais originais e entregas anteriores não são apagados.

`.gitignore` exclui artefatos locais. O assistente usa uma lista explícita de
fontes, recusa links/arquivos binários disfarçados e verifica alguns formatos
conhecidos de credenciais sem imprimir seus valores. Essa verificação é uma
barreira adicional; não constitui detecção completa de qualquer segredo.
Antes de enviar uma branch, também verifica seus commits anteriores para não
publicar um binário que tenha sido adicionado e posteriormente apagado.
Não envia tags automaticamente, não cria releases, não força push e não faz merge.

## Assistente Python

Requisitos: **Python 3.10+**, Git no PATH e autenticação GitHub da sua conta.
Funciona no Windows e Linux; não precisa instalar pacotes Python.

```text
python tools/aegis_git.py
```

O menu oferece status/diff, seleção e clone de repositórios, criação de
repositório, seleção/criação de branch, atualização por fast-forward,
commit dos fontes, push, consulta/checkout de PR, criação de PR em rascunho,
auditoria e geração de revisões de testes do sandbox.

Para publicar esta pasta em um repositório novo da sua própria conta:

```text
python tools/aegis_git.py publish --repo SUA_CONTA/Aegis --create --public --message "Publica fontes do Aegis"
```

Para commits/push posteriores, omitir `--create`; ele não reutiliza silenciosamente
um repositório existente. `--public` é necessário para criar público;
a criação padrão é privada. Se `origin` apontar para outro destino, a publicação
é interrompida sem substituir o remoto. A preparação e o commit locais acontecem
antes da criação remota; se a rede falhar, o commit permanece recuperável.

Também há comandos `audit`, `status`, `stage`, `push` e `create-repo`.
`--root PASTA` antes do comando seleciona outra pasta. Para alterar um repositório,
selecione sua raiz. O menu não troca de branch com mudanças locais pendentes.
A seleção de fontes é voltada à estrutura deste projeto; para outro projeto,
revise a lista em `tools/aegis_git.py` antes de usar commit/publicação.

A autenticação procura `GH_TOKEN`, `GITHUB_TOKEN`, GitHub CLI já autenticado
ou o gerenciador de credenciais do Git. A opção **Autenticar** aceita um token
oculto mantido apenas na memória daquela sessão. O assistente não grava tokens
nos fontes, na URL do remoto, em argumentos de comandos ou em relatórios.
Repositórios/PRs precisam das permissões correspondentes na sua conta.
O primeiro commit também precisa de nome/e-mail já configurados no Git.

## Revisões do sandbox e segurança

Retornar vazio em todas as APIs quebraria o comportamento dos programas e
criaria inconsistências observáveis. A proteção se apoia em isolamento,
metadados sintéticos coerentes, permissões e preservação dos erros reais.
Nenhuma revisão promete tornar o sandbox indetectável. Os perfis sintéticos
existentes, com seed e identidade coerentes, estão em [PHASE-12E.md](PHASE-12E.md).

O gerador cria **novas versões de scripts de regressão**, sem modificar sozinho
o kernel ou fabricar resultados. Mudanças de implementação continuam como
commits/branches revisáveis, com resultados antes e depois.

```text
python tools/aegis_sandbox_revisions.py generate --seed 42
python tools/sandbox_revisions/sandbox_regression_v0001.py --manager CAMINHO/guardian-vm --kernel CAMINHO/guardian-kernel-analysis.gk --output DIRETORIO_NOVO
```

No Windows, usar `guardian-vm.exe`. Sem `--seed`, o gerador escolhe uma seed
nova. Cada script gerado contém número de revisão, seed e hash do template;
a mesma seed/revisão/template reproduz os mesmos bytes. Versões anteriores
não são sobrescritas. A seed varia a ordem dos testes, não a implementação ou
a identidade do sistema convidado. Os scripts são fontes e podem ir para uma PR;
os relatórios devem ficar em `build-*` ou fora da árvore de fontes.

Os seis cenários usam somente aplicações próprias: comportamento de referência,
recusa, simulação, programa benigno, saturação da coleta e rede offline.
Verificam autoria/causalidade, erros reais, conteúdo inalterado após recusa,
efeitos da simulação, classificação, perda visível, exportação e um arquivo
privado de controle. A ausência do canário é verificada nos relatórios; isso não
prova ausência de qualquer vazamento em todos os canais possíveis.

Falhas retornam erro, encerram com código diferente de zero e preservam
`result.json` com diagnóstico quando o diretório de evidências foi criado.
Não há respostas vazias para ocultar falhas, bypass de proteções do host,
execução de malware externo ou reescrita automática de código sensível.

## Recriar os dados compilados de teste no Linux

O clone contém só fontes e imagens de documentação. Para testes que analisam
arquivos PE, construir os exemplos próprios e criar uma pasta externa:

```text
python tools/build_guest_linux.py --llvm LLVM_NATIVO --config Release --output DIRETORIO_GUEST
python tools/prepare_source_fixtures.py --from-guest DIRETORIO_GUEST --output DIRETORIO_FIXTURES_NOVO
cmake -S . -B build-linux-source -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DGUARDIAN_NATIVE_FIXTURE_DIR=DIRETORIO_FIXTURES_NOVO
cmake --build build-linux-source --parallel 4
ctest --test-dir build-linux-source --output-on-failure
```

Esses comandos não executam PE no host nem usam Wine. São entradas próprias
recompiladas para testes de parser; não substituem as referências MSVC congeladas
citadas em validações históricas. Para testes KVM, acrescentar
`-DGUARDIAN_TEST_KVM=ON -DGUARDIAN_KVM_GUEST_DIR=DIRETORIO_GUEST` e apontar também
o diretório dos drivers conforme [LINUX-KVM.md](LINUX-KVM.md).
Os validadores atuais aceitam `--native-fixtures DIRETORIO_FIXTURES_NOVO`.
Referências Windows e kernels legados de comparação não estão no GitHub;
devem ser produzidos/fornecidos explicitamente para esses testes específicos.

O Windows continua usando MSVC/WHP e os scripts de build existentes. Os exemplos
históricos `test/imgtest.dll`, `liba.dll` e `libb.dll` podem ser recompilados
dos respectivos `.c`; as imagens do kernel/PE principal são produzidas pelo build
do convidado. Não se deve baixar binários desconhecidos para preencher esses dados.

## Validação desta publicação

Em 22/10/2026: 18 testes das ferramentas Python, quatro testes de parser sobre
uma cópia contendo somente fontes e os seis cenários da revisão gerada passaram
no Linux/KVM. Os testes de ferramentas usam Git local real e API simulada,
incluindo recusa de binários/segredos, histórico com build apagado, destino remoto,
publicação preparada antes da criação remota e falhas sem supressão.

Isso valida a automação e a nova opção de fixtures; não declara uma nova execução
completa dos 153 testes da fase 13 nem aceitação nativa Windows dos scripts.
A aceitação anterior e seus limites permanecem em [PHASE-13.md](PHASE-13.md).
Nenhuma fase posterior foi iniciada.

API usada: documentação oficial de [repositórios](https://docs.github.com/en/rest/repos/repos)
e [pull requests](https://docs.github.com/en/rest/pulls/pulls).
