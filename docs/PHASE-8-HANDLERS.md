# Fase 8 — Preparação da busca de tratadores x64

**Continuação em 18/10:** a busca foi integrada ao despacho CPL3, continuação,
unwind não local e DLLs dinâmicas. A matriz Linux passou. O documento abaixo
preserva a preparação anterior; a aceitação Windows desta revisão exige agora
40 CTests por configuração e continua pendente. Estado e comandos atuais:
[PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md).

Referência: 18/10/2026. Plataforma do produto: **Windows x64, MSVC e WHP**.
Estado: **preparação implementada, validação nativa pendente**.
A fase 8 continua em andamento; a fase 9 não foi iniciada. O último marco
validado no Windows continua sendo [PHASE-8-UNWIND.md](PHASE-8-UNWIND.md).

## Alteração

`guest/Unwind.h` expõe `InspectHandler`. A operação restaura virtualmente um
chamador e identifica o tratador de exceção ou de término em `UNWIND_INFO` v1.
Devolve endereço do tratador, dados da linguagem, base da alocação fixa da
função (establisher frame) e identidade da função primária. Os endereços são
metadados do convidado; nenhuma rotina é chamada.

Aceita EHANDLER, UHANDLER ou ambos e segue cadeias até a função primária,
limitadas a 32 entradas. Não publica tratador durante o prólogo inicial,
epílogo, função folha ou quando o tipo solicitado não corresponde às flags.
Um registrador de frame ainda não estabelecido por SET_FPREG não é usado como
base da pilha. O percurso ignora os slots de operandos ao procurar essa operação.

O trailer do tratador respeita o preenchimento para alinhamento de quatro bytes.
A rotina deve ficar dentro da mesma imagem e passar pela consulta de mapeamento
executável fornecida pelo backend. Essa consulta é obrigatória na nova API.
Os dados de linguagem são opcionais: seu endereço pode apontar imediatamente
após a imagem, sem conceder permissão de leitura naquele endereço.

Todas as leituras continuam passando pelo backend, com limites de imagem e
pilha. Falha, metadados incompatíveis ou limite excedido preservam tanto o
contexto quanto a saída de metadados. Uma operação bem-sucedida sem tratador
zera os metadados. O resultado `End` também preserva ambas as saídas.

`Step`, `Capture` e o loader mantêm a recusa anterior a registros/imagens com
tratadores de linguagem. A nova inspeção ainda não está ligada às traps nem a
uma API de usuário. Não há nova declaração de suporte a SEH, exceções C++,
recuperação de faults, retorno de handlers ou unwind não local.

O formato segue [tratamento de exceções x64 da Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170)
e [RtlVirtualUnwind](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlvirtualunwind).
O formato dos dados de linguagem permanece responsabilidade do futuro tratador.

## Verificação nesta sessão

O ambiente disponível é Linux. Somente os componentes C++ independentes do
sistema operacional foram compilados diretamente; o CMake do produto continua
exclusivo do Windows. WHP, MSVC, MASM e o oráculo do Windows não foram executados.

| Verificação isolada | Resultado local |
| --- | --- |
| Novos testes, sem otimização e com AddressSanitizer/UndefinedBehaviorSanitizer | 3.059 verificações aprovadas |
| Novos testes, otimizados | 3.059 verificações aprovadas |
| Cenários principais de inspeção | 155 aprovados |
| Pontos de falha de leitura injetada nesses cenários | 980 aprovados |
| Regressão portátil do motor anterior, nas duas compilações | 1.045 verificações, 59 cenários, 391 pontos de falha e controle adicional de sete pontos |
| Compilação freestanding isolada, sem exceções C++/RTTI | aprovada; sem símbolos externos no objeto GCC produzido |
| Comparação com Windows, imagem do kernel e boots WHP | **pendentes** |

Os controles adicionais cobrem trailer sem códigos, dados opcionais no fim da
imagem, exatamente 32 entradas de cadeia e recusa de uma 33ª entrada. Há recusas
de endereço não executável, flags incompatíveis, trailer truncado e falha tardia
sem publicar contexto parcial.

Os testes anteriores receberam apenas condicionais de compilação para permitir
essa verificação isolada. No Windows continuam chamando `RtlVirtualUnwind` e
mantêm os marcadores históricos. Fora do Windows imprimem expressamente que
a comparação nativa não foi executada.

Evidência desta sessão: `build-phase8-handlers-precheck/validation.json` e logs
na mesma pasta. Esses resultados não substituem a aceitação no Windows.

## Repetir a validação no Windows

Na pasta do projeto, com Visual Studio 2022 C++ x64, Windows SDK, CMake 4+,
Python 3.9+ e WHP disponível:

```powershell
python tools\validate_phase8_handlers.py
```

O verificador exige Windows x64 e cria uma árvore nova com nome
`build-phase8-handlers-windows-<data>-<id>`. Compila todos os alvos em Debug e
Release, audita o convidado e exige os **38 CTests**, sem falhas ou ignorados.
Inclui as provas WHP anteriores, a comparação dos 59 cenários antigos e 153
comparações novas de contexto, seleção de tratador, dados e establisher frame.
Essas 153 comparações estão implementadas no teste, mas ainda não executadas.

O manifesto `phase8-handlers-windows-validation.json` registra falhas e só
indica validação aprovada se as duas configurações cumprirem o contrato. Os
hashes das fontes são conferidos antes e depois. Logs e discos históricos de
`build-codex` não são usados como destino desta execução.

Contrato: `config/phase8-handlers-acceptance.json`.
Cópia dos seis arquivos anteriores:
`snapshots/codex-before-phase8-handlers-20261018/`, com manifesto SHA-256.

## Continuação

Primeiro executar a validação nativa e corrigir eventuais divergências. Depois
integrar a busca ao despacho em CPL3, definir o contexto completo e validar
chamada, retorno e continuação de handlers sem aceitar endereços/permissões
indevidos. Os limites de pilha devem vir do vínculo PID/TID no kernel, como na
captura anterior, e não de campos modificáveis no TEB.

Continuam pendentes unwind não local, carga/descarga dinâmica de DLLs, ampliação
das falhas de inicialização/execução/término e auditoria final por aplicação.
Esta preparação não conclui a fase 8 nem autoriza iniciar a fase 9.
