# Fase 14 — Scanner, regras e inteligência de ameaças

Atualização da fila em 29/10/2026: a [14.13](PHASE-14.13.md) substitui o
encaminhamento legado descrito nesta entrega histórica por pacotes inscritos
no runtime próprio. A ausência de pacote fica inconclusiva, sem fallback.

Entrega de 23/10/2026. Implementação e aceitação Windows concluídas. A validação Linux desta revisão é parcial.
Esta etapa evolui os scanners existentes. A fase 15 não foi iniciada.

## Resultado e alcance

`guardian-scan` usa o mesmo motor de `av scan`, `av clean`, do monitor e da tela
de varredura Windows. No Linux, `guardianav scan` também encaminha ao motor
compartilhado. A interface mostra suspeitas e análises inconclusivas; cancelar,
falhar ao abrir uma pasta ou escolher um arquivo individual produz estado real.

Os resultados têm quatro classificações:

| Resultado | Significado |
| --- | --- |
| `detected` | Assinatura local, marcador inofensivo de teste ou regra YARA explicitamente configurada como detecção. |
| `suspicious` | Heurística ou relato de provedor externo; requer avaliação. Não inicia quarentena automática. |
| `unknown` | Nenhuma correspondência dentro do alcance declarado. **Não significa arquivo seguro.** |
| `inconclusive` | Não foi possível completar a análise. Erro, cancelamento, limite ou formato sem suporte fica visível. |

`complete` é independente da classificação. Um arquivo pode ter uma detecção
conhecida e também conter partes não analisadas. SHA-256 identifica o conteúdo;
SHA-1 e MD5 são auxiliares e não autorizam um arquivo por conta própria.

O motor lê uma cópia limitada e verifica estabilidade/tamanho. O worker recebe
somente essa cópia e confere seu SHA-256. Regras, parsers e descompactação têm
prazo e limite de memória em outro processo. No Windows, Job Object encerra a
árvore de processos ao cancelar ou estourar o prazo. Esse worker de parsing tem
a identidade do usuário: o isolamento de execução de programas é feito pela VM.

## Formatos e regras

- Assinaturas locais `sha256`/`substr`, com validação estrita da base, mais o
  fragmento EICAR de teste já existente. A busca percorre todo o conteúdo aceito,
  inclusive depois do antigo prefixo de 4 MiB.
- YARA real, compilado a partir de fontes fixadas. Pacotes JSON contêm fonte,
  identificador de licença e aviso. São aceitos MIT, BSD-3-Clause, Apache-2.0 e
  CC0-1.0; licença ausente/desconhecida, erro de compilação e `include` externo
  produzem análise inconclusiva. Os módulos disponíveis são `pe`, `math`, `time` e
  `string`; outros imports sem suporte falham de modo explícito. A declaração não substitui a revisão humana da
  procedência/licença das regras importadas. Não há carregamento de regra binária.
- PE: cabeçalhos e intervalos, arquitetura, seções, entropia, DLLs importadas e
  alerta de seção gravável/executável. Não verifica assinatura Authenticode nem
  determina legitimidade de um instalador. A compatibilidade PE da VM continua
  sujeita aos limites documentados nas fases anteriores.
- Scripts são inspecionados como texto, incluindo o caso UTF-16LE simples. A
  combinação de execução dinâmica com codificação/download é uma heurística;
  nenhum script é avaliado no host.
- ZIP, TAR, GZIP/TGZ e CAB no subconjunto configurado; MSIX/APPX e seus bundles
  são percorridos como contêineres ZIP. Cada membro é analisado em memória, sem
  extrair caminhos no host. Links, caminhos de travessia/absolutos, duplicatas,
  criptografia, corrupção e compressão não disponível são recusados.
- MSI/MSP, RAR/7z/XZ/BZ2/ISO e payloads reconhecidos de NSIS/Inno permanecem
  explicitamente inconclusivos. Não existe promessa de desempacotar qualquer
  instalador nem de detectar todo empacotador.

Padrões: arquivo/membro de até 32 MiB, expansão total de 64 MiB, até 256 membros,
profundidade 3 e análise estática de 5 segundos. Há 1 segundo adicional para
iniciar/encerrar o worker. O processo tem 384 MiB; análise no MiniNT permite o
auxiliar WHP próprio, com orçamento total de 512 MiB. A expansão é contada
novamente em cada nível, evitando contornar o orçamento com arquivos aninhados.
Limites maiores são validados, não são ilimitados. A CLI limita cada relatório a
10.000 entradas ou 32 MiB de resultados e informa truncamento; a enumeração tem profundidade máxima 64.

## Configuração e uso

Exemplo offline: [scanner.example.json](../config/scanner.example.json).
Pacote YARA próprio e inofensivo:
[scanner-rules.example.json](../config/scanner-rules.example.json).
Para usá-lo, acrescente `rulePackage` com seu caminho ao arquivo de configuração.
Caminhos relativos são resolvidos a partir da pasta da configuração.

```powershell
.\guardian-scan.exe scan "C:\pasta de teste" --config scanner.json --json
.\guardian-scan.exe queue --config scanner.json
.\guardian-scan.exe sandbox-next --config scanner.json
```

O arquivo de configuração é opcional. A configuração pessoal, quando existe,
fica em `%LOCALAPPDATA%\Aegis\scanner.json` no Windows ou em
`$XDG_STATE_HOME/aegis/scanner.json` no Linux (fallback:
`~/.local/state/aegis/scanner.json`). Os frontends Windows também a leem.
`state` pode escolher uma pasta dedicada para cache/fila; por padrão fica ao lado
da configuração pessoal do Aegis, em `scanner-v1`. Pasta não vazia sem marcador do scanner é
recusada. Windows usa ACL para usuário/SYSTEM; Linux usa permissão 0700.

Saídas de `guardian-scan`: 0 sem correspondência/unknown; 1 detecção/suspeita;
2 uso ou configuração inválida; 3 análise incompleta. O console Windows legado
preserva 2 para ameaça/suspeita e usa 3 para erro/inconclusivo. Não interprete
saída 0 como atestado de segurança.

## SQLite, cache e fila

SQLite persiste resultados, validade, cotas e backoff. O cache estático inclui
SHA-256, nome/extensão, fontes do motor, plataforma/compilador/configuração,
lock de dependências, regras, assinaturas
e limites; arquivos alterados e mudanças de política não reutilizam o parecer.
Somente análise estática completa é armazenada. Erros externos não viram cache
negativo. Há TTL, até 4.096 registros e limite de páginas do banco.

`queueUnknown: true` guarda uma cópia imutável por SHA-256 e uma entrada deduplicada
para o sandbox. `sandbox-next` reivindica uma tarefa em transação, verifica a cópia
e chama o **MiniNT/WHP já existente**, em CPL3, sem executar o arquivo como
processo Windows. O guest altera seu sistema de arquivos/registro virtuais.
O relatório identifica `MiniNT-WHP-legacy`, imports ausentes, falhas e eventos.
O PEB/linha de comando usa `C:\Samples\sample.exe`, sem expor o caminho privado
do host. A cadeia é limitada ao buffer dos parâmetros do processo; um PE próprio
compara todos os caracteres dentro da VM e falha se receber outro caminho.
Execução sem alerta continua `unknown`, nunca “segura”. A fila é local e manual;
ativá-la não inicia execução dinâmica automaticamente.

O carregador legado recebeu limites para tamanho de arquivo, cabeçalhos,
seções, diretórios, blocos/alvos de relocação, nomes, thunks e escritas na IAT.
Aritmética usa intervalos sem overflow; tabelas inválidas não chegam à execução.
TLS, delay-imports e inicialização gerenciada são recusados explicitamente.
Os testes incluem IAT/ILT e relocações fora da imagem, além de um programa que
confere uma relocação DIR64 válida durante a execução no convidado.

O adaptador aceita PEs compatíveis com os imports limitados desse backend. Não
equivale à execução de aplicativos Windows completos no kernel recente, nem
substitui o pipeline de nove domínios da fase 13. No Linux, esse encaminhamento
externo é explicitamente indisponível; não há fallback para execução no host.

A fila tem até 128 tarefas retidas e 512 MiB de cópias. Ao atingir o limite, a
nova análise informa o erro. Tarefas interrompidas têm lease e passam a
inconclusivas; não são marcadas como concluídas silenciosamente. Não há exclusão
automática de histórico/cópias nesta etapa. Diretórios temporários têm limite e
limpeza dos nomes fixos criados pelo próprio job.

## Inteligência externa

Offline e todos os provedores desabilitados são os padrões. Para consultar,
configure `offline: false`, habilite o provedor e reconheça suas condições em
`termsAccepted`. Chaves vêm de `keyEnv`, nunca do relatório ou SQLite.
O código usa somente endpoints fixos HTTPS, sem redirecionamento e com validação
de certificado, resposta limitada e conferência do hash retornado.
Não existe endpoint de upload, download de malware ou envio automático de amostra.

| Provedor | Operação | Tratamento |
| --- | --- | --- |
| VirusTotal | GET `/api/v3/files/{sha256}`, `x-apikey` | Estatísticas relatadas indicam suspeita; ausência/zero não libera arquivo. Exige licença Premium adequada ao uso deste conector de produto/hash-only. |
| MalwareBazaar | POST `query=get_info&hash=...`, `Auth-Key` | Hash reportado indica suspeita; `hash_not_found` é desconhecido. |
| CIRCL hashlookup | GET `/lookup/sha256/{sha256}` | Presença em base de arquivos conhecidos é informação, não whitelist. |

As cotas por minuto/dia são persistidas e transacionais. 429/503 ativam backoff;
não há repetição automática de requisições. Cache possui TTL configurável.
Falha de autenticação, JSON inválido, hash divergente e indisponibilidade são
expostos no campo `intelligence`, sem substituir evidência local. Offline pode
usar cache de reputação ainda válido, sem chamar a rede.

Windows usa WinHTTP. Linux usa libcurl nativo >=7.85 quando os arquivos de
desenvolvimento e pkg-config estão disponíveis; sem eles, compila offline e
informa indisponibilidade se for solicitada uma consulta. Não utiliza Wine.

Referências verificadas em 23/10/2026:
[VirusTotal — arquivo](https://docs.virustotal.com/reference/file-info),
[condições Public/Premium](https://docs.virustotal.com/reference/public-vs-premium-api),
[MalwareBazaar](https://bazaar.abuse.ch/api/),
[CIRCL](https://www.circl.lu/services/hashlookup/),
[YARA C API](https://yara.readthedocs.io/en/latest/capi.html).
As condições comerciais devem ser confirmadas pelo operador para o uso escolhido.

## Compilação e dependências

O utilitário opcional `tools/validate_phase14.py` registra JUnit, códigos de saída,
hashes de fontes/binários e recusa alterações durante o teste (Python >=3.11).

O CMake obtém **fontes**, confere SHA-256 e compila YARA 4.5.8, libarchive 3.8.10,
zlib 1.3.2, SQLite 3.53.4 e nlohmann/json 3.12.0. URLs, versões, licenças e hashes
estão em [ScannerDependencies.json](../cmake/ScannerDependencies.json); avisos
acompanham a instalação em `share/aegis/licenses`. Não são baixados executáveis
nem amostras. Um cache de fontes previamente verificado pode ser informado em
`GUARDIAN_SCANNER_DEPS`; esse override pressupõe uma árvore local confiável.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Mantenha `guardian-scan-worker` ao lado de `guardian-scan`, `av` e `avui`. No
Windows o encaminhamento à VM também precisa de `guardian-whp-memory.exe`.
`cmake --install` inclui os executáveis do scanner, o auxiliar, a base e avisos.

## Aceitação

Matriz da revisão final, com fontes conferidas antes/depois dos testes:

| Ambiente | Escopo | Resultado |
| --- | --- | --- |
| Windows 11 10.0.26300, MSVC 19.44, Debug | `scanner_core`, `scanner_pipeline`, `scanner_facade` | 3/3, sem ignorados |
| Windows, Release | Suíte completa do projeto, incluindo os três testes novos | 83/83, sem ignorados (470,37 s) |
| Ubuntu 24.04 / WSL2, GCC 13.3, Debug | Motor/worker, pipeline e CLI Linux | 2/2 + 1/1, sem ignorados |

O teste Windows executa somente PEs próprios no MiniNT: saída normal, verificação
do caminho virtual no PEB, relocação válida e recusa de tabelas malformadas.
O convidado em loop é encerrado dentro do orçamento. O canário do host permanece
inalterado. Os testes de arquivos cobrem assinaturas/hashes, YARA, conteúdo além
de 4 MiB, scripts, arquivos aninhados, links, travessia, corrupção e limites.
SQLite é exercitado com TTL, persistência, concorrência, cotas e backoff.
A fachada Windows é testada com arquivo individual, leitura inválida, detecção
e cancelamento. Testes de provedores usam transporte simulado; **consultas reais
com credenciais não foram realizadas**.

A validação Linux é parcial: scanner/CLI nativos, sem Wine; não houve execução de
PE externo no KVM nem revalidação de toda a suíte Linux/Release/sanitizers.
O ambiente disponível não tinha libcurl de desenvolvimento; a entrega Linux
exercitada foi offline. A aceitação de MiniNT externo é Windows/WHP.

Evidências locais: `build-phase14/acceptance-Debug-hardened/`,
`build-phase14/acceptance-Release-hardened/`,
`build-phase14/acceptance-Linux-hardened/` e
`build-phase14/linux-scanner-hardened.xml`. Cada aceitação inclui códigos reais,
JUnit, hashes de 441 entradas de fonte e dos executáveis relevantes.
Os ajustes anteriores da revisão Windows 12B–13 foram preservados; há snapshot
da base anterior em `build-phase14/baseline-verified-20261023.zip`.

O utilitário `tools/validate_phase14.py` permite repetir o escopo do scanner;
com `--all`, executa a suíte completa do build. Exemplo:

```powershell
python tools/validate_phase14.py --build build --config Release --all --output build-phase14/nova-aceitacao
```

Entrega local (fora do Git):
`dist/aegis-phase14-windows-x64-20261023/`, com `avui.exe`, `av.exe`,
`guardian-scan.exe`, worker, auxiliar WHP, configuração, avisos de licença,
documentação e evidências. Há ZIP e SHA-256 ao lado da pasta. `manifest.json`
registra os arquivos e as fontes aceitas. A entrega é verificada com uma
varredura offline de um documento inofensivo usando os executáveis copiados.

Nesta entrega, abra `avui.exe` para a interface ou consulte `LEIA-ME.txt` para a
CLI. A CLI moderna emite JSON; a configuração pessoal não é alterada pela cópia.
A fase 15 permanece sem início.

Nenhum teste exige malware real, chave de API, alteração de BIOS/NX ou driver
do host. A base distribuída é demonstrativa; esta fase não estabelece cobertura
de ameaças de um produto comercial nem compatibilidade Windows completa.
Continuam abertas as pendências de PE/SMP e referências NT específicas das
fases anteriores, conforme a revisão Windows de 23/10/2026.
