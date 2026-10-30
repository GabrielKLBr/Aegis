# Fase 14.13 — integração, análise e recuperação

## Escopo da entrega

O scanner e a operação de análise de EXE do worker de serviço usam a entrada
`microvm::runtime::Run`. A fila deixa de usar MicroSandbox/NT legado. O mesmo
caminho atende `guardian-runtime.exe`, com manifesto, perfil, módulos, políticas,
telemetria e encerramento vinculados ao pacote selecionado por SHA-256.

O alvo desta entrega é **NT 10.0.19045**, com três famílias já existentes:
USER32 x64 básico, núcleo PE32/x86 e runtime x64 de frameworks/áudio. A entrada
é unificada; os kernels e protocolos apropriados a cada família continuam
separados. Não há um kernel universal com todas as compatibilidades anunciadas.
As pendências da 14.11 e da 14.12 não são promovidas a suporte completo.

Cada EXE precisa de um pacote de dependências previamente preparado e inscrito
no registro local. Não existe busca automática de DLLs pelo disco do host.
Arquivo sem pacote, import incompatível, arquitetura errada, falha e prazo
excedido produzem resultado **inconclusivo**. Nunca há fallback para executar o
EXE no Windows nem para o executor legado.

## Pacotes e análise

`tools/unified_runtime.py` prepara o bundle usando os construtores existentes e
inscreve seu SHA-256 em `index.json`. O registro vincula manifesto, binding,
volume, kernel e hash do auxiliar. A inscrição exige referências locais legais
quando o construtor x64 precisa de CRT/schema Microsoft. O programa não é
executado pelo construtor.

O worker verifica os bytes recebidos do scanner contra a raiz do volume e a
identidade NT. Mantém arquivos do pacote abertos para leitura, impedindo sua
substituição durante a sessão. O código do convidado permanece em CPL3 da VM.
O pacote não escolhe um executável de host arbitrário: só o auxiliar conhecido
ao lado do programa instalado pode ser iniciado, e seu hash precisa coincidir.

O resultado registra perfil, arquitetura, identidade do pacote, módulos e
política. Eventos mantêm PID/TID, autoria, intenção/resultado, status e efeitos
em arquivos/registro virtuais. `virtualFiles` descreve alterações observadas,
não enumera todo o sistema de arquivos. Funções que executam inteiramente dentro
de DLLs não recebem cobertura automática; não se anuncia interceptação de toda
API ou detecção universal de malware.

`unknown` significa que nenhuma regra encontrou algo no alcance observado.
Execução incompleta, perda de cobertura ou falha do auxiliar impedem esse
resultado completo. Os relatórios completos ficam em uma pasta de sessão
separada; a fila guarda o resultado. A retenção da fila é limitada a 64 pastas
de sessão, exigindo arquivamento explícito antes de crescer além disso.

## Checkpoints reais de USER32 básico

O checkpoint conserva RAM, estado do processador, mapeamentos, processos,
módulos externos, pilhas, cotas, VFS/registro em memória, relógio e telemetria.
O volume imutável e a página de identidade NT são conferidos na restauração.
A política e a identidade de todos os arquivos do pacote também precisam
coincidir, incluindo o hash do executável controlador. O arquivo de checkpoint tem checksum interno e SHA-256 no recibo;
esses hashes verificam integridade, não constituem assinatura de um emissor.

Para USER32 básico, um canal exclusivo do supervisor captura janelas/controles,
texto, geometria, estilos, estado de botão/seleção, foco, IDs opacos, eventos e
contadores. Essas operações não pertencem à lista de comandos aceitos da VM.
O broker do host as bloqueia mesmo se um convidado tentar forjar o pedido.

A retomada cria uma VM candidata e um novo auxiliar restrito. Reconstrói apenas
as janelas pertencentes à sessão, preserva os IDs usados pelo convidado e
restaura a memória; nenhum HWND ou ponteiro Windows é serializado para a VM.
Ao gravar o checkpoint, as janelas originais são encerradas. Uma restauração
recusada não altera a sessão original nem o checkpoint.

São aceitos checkpoints offline de USER32 básico, x86 e x64. **GDI, DirectX,
áudio, apresentador por pixels, conexões abertas, timers USER32 ativos e sends
pendentes não têm restauração neste lote** e são recusados. A família de
frameworks continua executável sem checkpoint. A exportação normal de estado
de segurança tem 64 KiB; ela não substitui o checkpoint de execução completo.

## Uso

Preparar/inscrever um programa x86:

```powershell
python -B tools/unified_runtime.py --build BUILD --config Release --application APP.exe --registry REGISTRO --work PREPARACAO_NOVA
```

Para x64, fornecer `--reference CRT_LOCAL --schema-reference SCHEMA_LOCAL`;
`--family user32` seleciona USER32 básico, e `auto` seleciona a família x64 de
frameworks. Os imports precisam estar atendidos pelo pacote escolhido.

Na configuração do scanner, acrescentar `"sandboxRuntime": "CAMINHO_REGISTRO"`.
Os caminhos relativos são resolvidos em relação à configuração:

```powershell
guardian-scan.exe scan APP.exe --config scanner.json --json
guardian-scan.exe sandbox-next --config scanner.json --json
guardian-runtime.exe --input APP.exe --runtime-root REGISTRO --output SESSAO_NOVA
guardian-runtime.exe --input APP_GUI.exe --runtime-root REGISTRO --output PAUSA_NOVA --checkpoint-after 12
guardian-runtime.exe --input APP_GUI.exe --runtime-root REGISTRO --output RETOMADA_NOVA --restore PAUSA_NOVA/recovery.json
```

`--checkpoint-after` conta pedidos USER32 concluídos; o ponto depende do
aplicativo. Saída zero com `checkpointCreated` significa checkpoint gravado,
enquanto a análise pausada permanece inconclusiva. A execução retomada precisa
concluir seu próprio cenário para produzir `complete: true`.

## Validação e reprodução

**Windows/WHP em 29/10/2026:** Release **23/23** e Debug **23/23**, com
fontes/binários estáveis. O verificador ASan aprovou nove checagens dirigidas
e 2.000 mutações por configuração. A matriz inclui o trajeto real da fila e
retomadas com confirmação de texto, clique e estado de arquivo/registro.

`tools/validate_unified.py` cobre somente esta fase e dependências utilizadas:
entrada da fila, ausência de fallback, console independente, família WinRT,
checkpoints/retomada x86 e x64 com entrada dirigida ao auxiliar, preservação de
arquivo/registro e autoria, perfil/arquitetura/volume adulterados, auxiliar de
host não autorizado, checkpoint de outra entrada/política, corrupção, prazo,
política de módulos e checkpoint GDI recusado. O verificador de estado nativo
usa ASan e 2.000 mutações que não são executadas como código.

```powershell
cmake --build BUILD --config Release --target guardian-runtime guardian_unified_probe guardian-scan guardian-engine-service guardian-checkpoint-tests
python -B tools/validate_unified.py --build BUILD --config Release --reference CRT_LOCAL --schema-reference SCHEMA_LOCAL --corpus CACHE_LOCAL --asan-runtime ASAN_LOCAL --output PROVA_NOVA
```

Repetir em Debug. A preparação dos binários próprios das famílias utilizadas
continua necessária em um build novo. O empacotador exige provas estáveis
atuais das duas configurações. O ZIP extraído é validado no mesmo escopo e
conferido antes/depois; snapshots de RAM, referências Microsoft e fonte/binário
do corpus externo não são redistribuídos. Os testes não atualizam os serviços
instalados nem exigem prompts de administrador.

## Continuação

Snapshots com recursos GDI/DirectX/áudio, transporte conectado e diálogos nativos
complexos continuam pendentes. Compatibilidade ampla, proporção de aplicativos
e eficácia de detecção exigem os respectivos corpus; esta entrega não declara
essas metas atingidas. A fase 14.14 não é iniciada automaticamente.
