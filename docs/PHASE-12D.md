# Fase 12D — sessões longas, reinicializações e interação

**Atualização Windows de 23/10/2026:** a matriz atual passou em MSVC/WHP: 80 testes distintos Debug, com retestes documentados, e 80/80 Release em uma suíte completa, sem ignorados. [Correções e evidências](REVISAO-WINDOWS-FASES12B-13-20261023.md). Os registros Linux abaixo são históricos; as pendências de escopo e de comparação entre versões continuam abertas.

**Estado em 21/10/2026:** implementação e aceitação Linux aprovadas na revisão
estável `acceptance-20261021-full-01`: **53/53 Debug, 53/53 Release e 33/33
ASan/UBSan**, total de 139 execuções CTest, sem falhas ou testes ignorados.
Os 392 arquivos de código permaneceram estáveis durante toda a matriz. A autorização explícita desta subfase sucede a 12C.
O produto continua Windows/MSVC/WHP; a compilação e os testes locais são
Linux/KVM nativos, sem Wine ou QEMU. A 12E foi autorizada posteriormente: [PHASE-12E.md](PHASE-12E.md).

O estado anterior à 12D está preservado em
`build-phase12d/baseline-20261021/original-source.zip`, com hashes em
`baseline.json`. Os builds temporários ficam em
`/home/gabri/Projetos/GuardianAV-12D-build-20261021`; código, documentação,
entrega e evidências selecionadas ficam na pasta original do projeto no HD.

## Comportamento entregue

- Duração de 100 ms até sete dias por sessão, com pontos de recuperação a cada
  100–30.000 ms de execução. Atingir a duração suspende a VM e libera o processo
  executor; `resume` abre outra sessão a partir do ponto salvo. O limite antigo
  continua disponível para catálogos anteriores, que mantêm sua configuração.
- `suspend` salva memória, processadores, disco e estado dos dispositivos e
  encerra o executor. `pause` mantém o executor residente. A retomada em outro
  processo e a clonagem conservam as aplicações, esperas e posição do roteiro.
  As gravações continuam atômicas e sujeitas às cotas da VM.
- `restart` faz um boot novo: o volume persistente conserva arquivos/registro,
  e o programa recupera seu progresso gravado. A RAM e o uptime recomeçam;
  um disco descartável segue descartável. Para conservar RAM, usar
  suspensão/retomada ou restaurar um snapshot completo.
- Roteiros de até 256 eventos contêm horário, tecla ASCII, clique, movimento,
  perda de foco ou fechamento. São importados para a configuração e
  incluídos no snapshot; mudar a política não permite restaurar silenciosamente
  uma sessão antiga. Entrada é encaminhada ao dispositivo gráfico e às janelas
  do convidado. Não existem comandos para selecionar janelas/aplicativos do host.
- O visualizador nativo oferece interação manual e suspensão/retomada.
  A reabertura redesenha o framebuffer salvo e limpa o foco do convidado;
  filas de entrada do sistema hospedeiro não são persistidas. Os filtros de
  eventos da própria janela permanecem ativos. Scripts não simulam entrada
  pelo sistema operacional do host.

## Tempo e efeito na análise

O único modo disponível é `real`. Cada trecho usa o relógio monotônico real;
QPC, ticks, UTC virtual e prazos de `Sleep` conservam a semântica da 12C.
Enquanto a VM está suspensa, ou o host grava um checkpoint, o tempo ativo do
convidado fica congelado. Uma retomada preserva o uptime/âncora UTC e os
prazos pendentes. Restaurar um snapshot anterior restaura esse mesmo estado,
inclusive seu relógio. O relógio do host nunca é alterado.

O orçamento usa o tempo de execução do convidado, excluindo a cópia/salvamento
de RAM. A expiração de um trecho solicita pausa, sem tratar o cancelamento como
falha ou como conclusão do programa. O limite acumulado de snapshots passa de
uma hora para dez anos, com verificações de overflow/ordem; a duração por sessão
continua limitada a sete dias. Não foi realizado ensaio contínuo de sete dias.

`session.json` registra duração, checkpoints, tempo ativo, tempo decorrido no
host e motivo da saída. `graphics.json` registra horário previsto/efetivo de
cada evento e atraso máximo, permitindo medir o efeito de pausas e da gravação
sobre a análise. Os tempos de host cobrem o loop da execução e seus
checkpoints; preparo/importação/restauração anteriores ao loop ficam fora
dessa medida. O arquivo de sessão descreve a execução mais recente; os
relatórios de aceitação preservam os trechos inicial e retomado. Nenhum evento é antecipado para encurtar a execução.
Aceleração e outros modos experimentais de tempo virtual não são implementados;
pedir `--time-mode` diferente de `real` é recusado explicitamente.

## Uso

No Linux, usar os executáveis ELF e os kernels do pacote 12D. No Windows,
usar os mesmos comandos com os executáveis compilados nativamente por MSVC.
Estes exemplos usam apenas a aplicação gráfica própria e arquivos do catálogo:

```text
guardian-vm --root CATALOGO create exemplo guardian-kernel-graphics.gk graphics --ram-mib 32
guardian-vm --root CATALOGO session-config exemplo --duration-ms 120000 --checkpoint-ms 5000 --script config/session-example.txt --time-mode real
guardian-vm --root CATALOGO start exemplo
guardian-vm --root CATALOGO suspend exemplo
guardian-vm --root CATALOGO resume exemplo
guardian-vm --root CATALOGO status exemplo
```

Uma nova duração pode ser configurada depois de suspender, conservando o
roteiro. Ativar o modo de sessão ou trocar o roteiro de uma VM antiga requer
uma VM nova sem checkpoints/snapshots, evitando uma restauração incompatível.
Os horários do roteiro são absolutos desde o início do boot; a retomada segue
a posição salva e um boot novo reinicia o roteiro. O arquivo de exemplo termina
em 60 segundos, mesmo quando o orçamento configurado é maior.

```text
guardian-gui guardian-kernel-graphics.gk SAIDA --interactive --duration-ms 120000 --checkpoint-ms 5000
guardian-gui guardian-kernel-graphics.gk SAIDA --interactive --resume --duration-ms 120000
```

A duração expirada preserva `SAIDA/session.gvs`. Para roteiro, substituir
`--interactive` por `--script config/session-example.txt`, informando o mesmo
roteiro ao retomar. A conclusão normal remove o checkpoint desse visualizador.
Cada diretório de saída admite somente um executor por vez.

## Validação

A matriz reexecuta os 132 CTests da 12C na revisão da 12D e acrescenta sete
execuções específicas desta subfase (três Debug, três Release e uma Sanitize).
As configurações inteiras foram compiladas e testadas uma vez sobre a mesma
revisão final, sem reaproveitar os resultados de desenvolvimento.

| Configuração Linux | Testes aprovados | Ignorados |
| --- | ---: | ---: |
| Debug/KVM | 53/53 | 0 |
| Release/KVM | 53/53 | 0 |
| ASan/UBSan, núcleo sem KVM | 33/33 | 0 |

Os testes da 12C também repetiram 1.590 observações convidadas (795 por
configuração KVM), sem diferenças no contrato esperado. As três referências
NT nativas permanecem ausentes; esses resultados não são uma comparação com
medições do Windows. A revisão Windows passou apenas pela checagem de sintaxe
cruzada de dez unidades C++, sem executar um `.exe`, sem MSVC e sem WHP.
A identificação de compilação dessa checagem usa valores substitutos apenas
para permitir a leitura das unidades pelo compilador de sintaxe.

Os três testes novos cobrem:

1. Políticas/limites, rejeição de eventos malformados, prazos sem antecipação,
   posição do roteiro após restore, relógio regressivo e foco manual na reabertura.
2. Programa PE próprio em CPL3: progresso persistido antes de `Sleep(1000)`,
   snapshot no meio da espera, restauração em executor novo, pausa de 250 ms no
   host sem concluir a espera, e boot novo recuperando o progresso do volume.
3. Catálogo com durações sucessivas, suspensão/retomada, clonagem, reboots,
   rejeição de política alterada, sessão gráfica real de 31 segundos, oracle
   independente dos pixels e restauração do visualizador manual nativo. No
   Linux, esse ensaio do visualizador usa SDL com vídeo de teste sem tela; o
   teste de eventos verifica a filtragem da própria janela. Não é uma sessão
   manual humana no Windows.

### Medições da execução real

| Configuração | Evento final no convidado (s) | Host, soma dos dois loops (s) | Atraso máximo de entrada (ms) | Retomada da espera, host (ms) |
| --- | ---: | ---: | ---: | ---: |
| Debug | 31.000967 | 39.144 | 45.802 | 957.303 |
| Release | 31.000622 | 37.734 | 19.665 | 955.269 |

O evento de fechamento previsto para 31 segundos nunca foi antecipado. A
imagem final das duas configurações coincidiu com o oracle independente:
oito pixels e o hash integral do framebuffer, com SHA-256 do PPM
`4d26b5738133e4946a3d86aa6b7f246c25561d0a77df9f0c4c14ad4ded3c5c9b`.
A espera de um segundo foi pausada após um trecho de 200 ms, ficou 250 ms
sem executor e retomou com o prazo ainda pendente. O programa conferiu o
próprio tempo; o boot novo recuperou o progresso persistido antes da espera.
Os tempos de host acima incluem cópias/checkpoints do loop, conforme descrito
na seção de tempo; não incluem os preparos anteriores ao loop.

### Evidências e entrega

- Resultado consolidado: `build-phase12d/acceptance-20261021-full-01/result.json`.
- Inventário dos 392 arquivos: `source-inputs.json` no mesmo diretório,
  SHA-256 `e51fe47fcb1daf5274d774c9379fb0738f1171ccba295367ae7b7fae069c39f2`.
- Logs de configuração, compilação, JUnit e relatórios JSON dos convidados
  estão copiados para essa pasta no HD. As imagens de RAM dos ensaios ficam
  somente no diretório externo de build.
- Diferenças e checagem Windows: `build-phase12d/review/changes.diff`,
  `changes.json` e `windows-syntax-review.json`.
- Código: `build-phase12d/delivery-20261021/GuardianAV-12D-fontes-20261021.zip`.
- Linux: `build-phase12d/delivery-20261021/GuardianAV-12D-linux-20261021.tar.gz`.
  Contém os executáveis ELF Release testados, 19 kernels próprios, drivers,
  roteiro de exemplo e instruções. Os dados do convidado não têm permissão
  de execução no pacote Linux.
- Integridade: `SHA256SUMS.txt` e `delivery.json` no diretório de entrega.

Reproduzir a matriz completa em uma pasta nova:

```text
python tools/validate_phase12d.py --output DIRETORIO_NOVO --llvm LLVM_NATIVO --openssl-root OPENSSL_NATIVO
```

No Windows, executar em terminal x64 do Visual Studio, omitindo `--llvm` e
`--openssl-root`. O validador exige MSVC nativo. As referências nativas da 12C
podem ser anexadas com `--reference ARQUIVO`, uma por versão NT real.

## Limites e encerramento

Os perfis continuam os da base: o aplicativo PE e o gráfico são contratos
separados; isto não implementa USER32/GDI do Windows, um Windows completo ou
retomada transparente de programas arbitrários. O programa de espera usa o
volume legado; a integração PE com SMP/volume escalável da 12B segue pendente.
O caminho SMP também recebe suspensão por prazo e o limite acumulado ampliado.
Continuam valendo as cotas de RAM/armazenamento, oito snapshots nomeados por
VM e o limite de 64 históricos gráficos por VM do catálogo. Checkpoints dentro
da mesma sessão reutilizam o histórico; não criam um novo a cada trecho.

A aceitação Windows/MSVC/WHP desta revisão e as referências Windows da 12C
continuam necessárias. As pendências da 12B e as anteriores não são encerradas
por essa autorização. A conclusão formal conjunta permanece aberta até essa
validação. Parar na 12D; nenhuma implementação da 12E foi incluída.
