# Correções Windows da fase 11 — 19/10/2026

**As oito falhas foram corrigidas. A suíte completa aprovou 52/52 testes em
Debug e 52/52 em Release, sem ignorados, em Windows 11 x64/MSVC/WHP real.**

| Configuração | Antes | Depois | Falhas atuais | Tempo da suíte final |
| --- | ---: | ---: | ---: | ---: |
| Debug | 44/52 | 52/52 | 0 | 446,15 s |
| Release | 44/52 | 52/52 | 0 | 297,43 s |

O verificador oficial retornou `passed` / `awaiting_linux_acceptance`.
A revisão corrigida está aceita no Windows; sua revalidação Linux ainda está
pendente porque os hashes de fontes mudaram. A fase 12 não foi iniciada.

## Correções

- **Rede e gráficos:** os comandos I/O agora usam somente os bits transferidos
  pela instrução: 8, 16 ou 32. Bits altos de RAX não viram comandos adicionais.
- **RAM de várias VMs:** cada VM tem um processo auxiliar próprio que mantém
  sua memória. O monitor acessa uma visão compartilhada e usa
  `WHvMapGpaRange2` com o processo correto. O cabeçalho de inicialização fica
  fora da RAM visível ao convidado. Os handles herdados pelo auxiliar são
  limitados aos objetos dessa inicialização; o processo termina com seu dono.
- **Interrupção pendente no snapshot:** uma interrupção externa com IF desligado
  ou sob sombra de STI permanece na fila do monitor. O snapshot conserva esse
  estado; a entrega acontece quando a CPU sinaliza que pode recebê-la.
- **Restauração de CPU:** o resumo de atividade interna do Hyper-V é somente
  leitura. O carregador valida esse campo e restaura os registradores graváveis,
  sem tentar escrever o resumo de atividade.
- **Publicação de arquivos:** conflitos transitórios de compartilhamento/acesso
  recebem tentativas limitadas. A geração anterior permanece intacta se a
  substituição falhar, e o arquivo temporário é recolhido.

Voltaram a passar, em ambas as configurações: `guest_session_snapshot`,
`vm_catalog`, `vm_manager`, `guest_network_boot`, `vm_network`,
`guest_graphics_boot`, `graphics_application` e `vm_graphics`.

## Verificações

Foram preservados os critérios e prazos dos 52 testes. A compilação final
partiu de uma árvore nova; Release foi preparado enquanto Debug executava,
e as suítes foram sequenciais. Nenhuma fonte mudou durante a aceitação.

Além da matriz anterior, foram acrescentadas provas de I/O com bits altos
não nulos, RAM e permissões independentes entre VMs, rejeição de atividade de
CPU não restaurável e substituição de arquivo permanentemente bloqueada.
Os testes demonstram preservação da geração antiga e recuperação após liberar
o bloqueio. O snapshot aprova 234 verificações por configuração. Permaneceram
aprovadas as comparações Microsoft de 153 handlers e 59 frames de unwind por
configuração, os testes de falha/recovery, rede e oráculos gráficos independentes.

O teste de rede executa quatro convidados reais simultâneos; o gerenciador
exercita duas VMs, queda de worker e recuperação. Os auxiliares de memória
foram encerrados ao final. Três lançamentos malformados do auxiliar também
foram recusados com os códigos esperados.

## Requisitos e limites

Os programas Windows que usam o monitor precisam de `guardian-whp-memory.exe`
na mesma pasta. O CMake já constrói essa dependência. A API de memória usada
requer Windows 10 20H2 ou mais recente e WHP; a execução desta rodada foi em
Windows 11, build 26300.

Os checkpoints Windows aceitos param com CPU executável em fronteiras do
perfil suportado. Snapshots com suspensão de startup, halt ou idle são
recusados explicitamente; esses estados precisam de um contrato adicional.
Os limites de aplicações próprias, GUI por software, UDP e um vCPU por VM
continuam descritos em [PHASE-11.md](PHASE-11.md).

Referências dos contratos:
[WHvMapGpaRange2](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange2),
[registradores de CPU](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvsetvirtualprocessorregisters)
e [resumo de atividade somente leitura](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/datatypes/hv_register_name).

## Evidências e recuperação

- [Aceitação oficial](../build-phase11/validation-20261019-122920-145a4969/validation.json).
- [Manifesto das correções](../build-phase11/validation-20261019-122920-145a4969/windows-fixes-summary.json).
- [Patch revisável](../build-phase11/validation-20261019-122920-145a4969/changes.patch).
- [JUnit Debug](../build-phase11/validation-20261019-122920-145a4969/host/tests-Debug.xml) e [JUnit Release](../build-phase11/validation-20261019-122920-145a4969/host/tests-Release.xml).
- [Diagnóstico original das oito falhas](VALIDACAO-WINDOWS-FASE11-20261019.md), preservado.

Os originais estão em `snapshots/codex-before-windows-phase11-fixes-20261019/`.
Foram conferidos os hashes de 31 relatórios, logs e pacotes anteriores, sem
alterações. A execução pode ser repetida com `python tools/validate_phase11.py`.
Nenhuma configuração de boot, NX ou segurança do host foi alterada.
