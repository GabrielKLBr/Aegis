# Fase 12E — ambiente de uso e dados sintéticos

A fase 13 foi autorizada posteriormente em 22/10/2026: [registro](PHASE-13.md). Os resultados e limites desta entrega da 12E são preservados abaixo.

**Estado em 22/10/2026:** implementação concluída e aceita na matriz Linux/KVM
nativa: **56 Debug, 56 Release e 34 ASan/UBSan**, todos aprovados, sem ignorados.
O produto permanece Windows/MSVC/WHP; não foi usado Wine ou QEMU.
A aceitação nativa Windows desta revisão e as pendências das fases 12B/12C
continuam abertas. Por isso, o resultado técnico mantém `phaseComplete: false`
e `prerequisitesComplete: false`; a aceitação local da 12E não encerra esses
requisitos. A fase 13 não foi iniciada.

O código está na pasta original do HD. A base anterior está preservada em
`build-phase12e/baseline-20261021/original-source.zip`, com hashes de 460 arquivos
em `baseline.json`. Os builds desta fase ficam fora do NTFS em
`/home/gabri/Projetos/GuardianAV-12E-build-20261021`.

## Perfil versionado

O perfil `GuardianProfile/1` usa uma semente de 32 bits, conteúdo v1 e três
cenários x64: Win7/7601, Win10/19045 e Win11/22631. Sua conta sintética
`lab-SEMENTE` corresponde ao token real do convidado: usuário 100, grupo 10.
Não há senhas, login Windows ou contas copiadas do host. A página de perfil é
somente de leitura, vinculada ao NT do hardware e validada na restauração.
Hardware e identidade do perfil são sintéticos e reproduzíveis.

O catálogo instalado contém apenas **Guardian Docs 1**, um executável próprio
que roda em CPL3 e lê texto/CSV por APIs do convidado. O executável fica no
catálogo imutável do kernel, com hash dos bytes reais; não se cria um falso
`.exe` no volume de arquivos de 512 bytes. Arquivos de catálogo e registro
declaram esse armazenamento explicitamente. Não se declara Office, navegador
ou outro programa que o perfil não execute.

Os documentos são `C:\welcome.txt`, `C:\budget.csv` e `C:\notes.txt`.
Um atalho próprio `GuardianShortcut/1` abre o texto pela associação registrada.
Esse formato não é um arquivo Windows `.lnk`. Associações `.txt` e `.csv`
resolvem para o leitor realmente executado por `CreateProcessA` no convidado.
O histórico é próprio do leitor; não representa navegação de um browser.

Todo o conteúdo inicial é gerado dentro do convidado a partir da semente e
da versão do cenário. O perfil só recebe parâmetros tipados; não recebe
contas, documentos, credenciais, diretórios pessoais ou navegação do host.
Não se adiciona canal de importação nesta fase; transferências não suportadas
continuam recusadas pelos canais existentes.

## Datas e evolução

Datas de criação, acesso e escrita passam a ser persistidas em uma extensão
identificada do cabeçalho do volume. O tamanho dos nós, dos slots e dos
comandos existentes permanece igual. Volumes antigos com a extensão zerada
continuam legíveis; datas antigas desconhecidas são retornadas como zero.

`GetFileTime` e `SetFileTime` cobrem arquivos com handles do subconjunto já
suportado, resolução de um segundo e datas representáveis em segundos Unix positivos de
32 bits. Precisão/valores especiais não suportados são recusados. Ponteiro
nulo ou FILETIME zero mantém o campo, seguindo a documentação de
[SetFileTime](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfiletime)
e [GetFileTime](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletime).
Datas iniciais são ajustadas explicitamente para compor o cenário sintético;
leituras/escritas posteriores atualizam as datas pelo relógio real do convidado.
O relógio do host permanece intacto.

O marcador final do provisionamento é confirmado junto com arquivos e registro.
O histórico registra aberturas reais do leitor, mantém uma janela limitada e
um contador acumulado. O perfil persistente conserva notas e evolução em boots,
clones e snapshots; o descartável recomeça em um boot novo. A reprodução da
base inicial exige a mesma semente, cenário, versão e imagem do catálogo.

## Validação e entrega

A revisão estável `acceptance-20261021-full-02` passou na matriz completa:

| Configuração | CTests aprovados | Falhas / ignorados |
|---|---:|---:|
| Debug | 56 | 0 / 0 |
| Release | 56 | 0 / 0 |
| ASan/UBSan | 34 | 0 / 0 |
| Total | 146 | 0 / 0 |

Os testes específicos da 12E verificaram:

- Núcleo: 89 verificações por configuração, incluindo versão/semente,
  repetição, datas reais, direitos, valores não suportados e recuperação de
  commit interrompido.
- Boots em Debug e Release: seis pares NT/semente, duas execuções de cada
  base, 12 retomadas e 12 rejeições de alteração do perfil por configuração.
  O volume inicial inteiro foi comparado. No total, são 24 bases novas,
  24 retomadas e 24 rejeições de perfil alterado.
- Oracle independente em Python: bytes dos documentos, conta, associações,
  catálogo contra o PE realmente compilado e datas no volume. Foram 514
  verificações em Debug e 584 em Release, incluindo consultas de progresso;
  a diferença na contagem depende da duração das VMs.
- Abertura dos três documentos por processos reais do leitor; notas e
  histórico preservados em reinício, clone e snapshot, rollback correto e
  perfil descartável reiniciado sem dados da sessão anterior.
- Canário pessoal criado apenas no host ausente da RAM e do volume convidado;
  arquivo de controle do host intacto. A importação de base pessoal pela
  criação do perfil foi recusada. Este teste cobre esses canais e canários,
  sem representar uma prova geral de ausência de todo dado pessoal possível.

Os 405 arquivos de entrada de código permaneceram idênticos durante e após a
matriz. O SHA-256 de `source-inputs.json` é
`21ec810bb30a745b919eaa5a9191875ce6d27c419e3c7b907a757ed44cdf522c`.
A primeira tentativa foi substituída antes de terminar, após a checagem Windows
detectar uma colisão do macro `Yield` na ordem de inclusão de um cabeçalho do
teste. Nenhum resultado parcial dessa tentativa foi contado na aceitação.
A revisão final passou na checagem cruzada de sintaxe de dez unidades Windows.
Essa checagem não executa `.exe` e não equivale à compilação MSVC ou à execução
WHP nativa, que continuam pendentes.

Os builds e a matriz começaram em 21/10; os diretórios e pacotes conservam essa
data. Na pasta original do HD:

- Evidências selecionadas: `build-phase12e/acceptance-20261021-full-02/`, com
  resultado, inventário, logs, JUnit e relatórios próprios de cada configuração.
- Revisão das alterações e sintaxe Windows: `build-phase12e/review/`.
- Fontes: `build-phase12e/delivery-20261021/GuardianAV-12E-fontes-20261021.zip`.
- Linux: `build-phase12e/delivery-20261021/GuardianAV-12E-linux-20261021.tar.gz`,
  com dez ferramentas ELF, vinte kernels e os programas próprios como dados
  do convidado. O kernel inclui o catálogo usado durante a execução.
- Integridade e verificação da entrega: `SHA256SUMS.txt`, `delivery.json` e
  `package-verification.json` no mesmo diretório de entrega. A verificação
  adicional extrai o pacote, executa o núcleo e inicia o perfil pelo programa
  entregue, conferindo a abertura dos três documentos.

### Uso

```text
guardian-vm --root CATALOGO create-environment escritorio guardian-kernel-environment.gk --nt win11 --seed 42
guardian-vm --root CATALOGO environment escritorio
guardian-vm --root CATALOGO start escritorio
guardian-vm --root CATALOGO status escritorio
guardian-vm --root CATALOGO restart escritorio
```

Adicionar `--disposable` na criação para recomeçar a cada boot. Os perfis
existentes não recebem dados automaticamente. A semente e o cenário ficam
vinculados à VM; para outra base, criar outra VM. Alterar a versão NT do
hardware para divergir do perfil é recusado. Os comandos de sessão, snapshot,
clone e restauração da 12D continuam disponíveis.

O console do boot mostra `12E DOC OPEN` três vezes e `12E DONE visits=N opens=3`. A abertura de texto e a validação de CSV ocorrem no processo leitor,
com a identidade da conta do convidado. O histórico mantém os últimos 12
registros e o total de visitas; as notas recebem uma linha por visita, até o
limite real de 512 bytes do arquivo. Falta de espaço ou documento inválido
é falha explícita da operação.

Reproduzir a matriz em um diretório novo:

```text
python tools/validate_phase12e.py --output DIRETORIO_NOVO --llvm LLVM_NATIVO --openssl-root OPENSSL_NATIVO
```

No Windows, usar o terminal x64 do Visual Studio e omitir os caminhos Linux.
O validador exige MSVC para a aceitação Windows e conserva as comparações e
pendências das subfases anteriores. A fase 13 não faz parte desta autorização.


## Limites preservados

O perfil de aplicação usa uma vCPU e o volume existente de 32 nós, com 16 KiB
de dados e arquivos de até 512 bytes. Os recursos ampliados do contrato próprio
`resources` da 12B não foram integrados a este perfil. Não há desktop Windows,
Office, browser ou compatibilidade geral com programas de terceiros. As três
versões NT são cenários do subconjunto implementado. O leitor, o atalho e o
histórico são próprios e seus formatos ficam explícitos. Importação de contas,
credenciais, pastas pessoais e navegação não foi acrescentada.

A conclusão desta entrega é a implementação e a aceitação Linux dos perfis
sintéticos da 12E. A próxima fase depende de nova solicitação, sem encerrar
as validações Windows e os requisitos anteriores ainda pendentes.
