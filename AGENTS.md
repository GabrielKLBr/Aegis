# Instruções permanentes do projeto

## Entregas por fase e GitHub

Por solicitação do usuário em 24/10/2026, ao concluir cada nova fase ou subfase:

1. Finalizar a implementação, executar as verificações pertinentes e registrar
   resultados e limitações. Não declarar uma fase concluída por um teste isolado.
2. Revisar as alterações e publicar um commit identificado pela fase em
   `GabrielKLBr/Aegis`, usando `tools/aegis_git.py publish`. O pedido do usuário
   já autoriza esse commit e seu envio; não pedir confirmação novamente.
3. Verificar que o commit local chegou à branch remota. Nunca usar force push,
   reescrever o histórico ou descartar alterações de outras tarefas.
4. Encerrar a entrega e parar. O commit automático não autoriza começar a
   próxima fase; aguardar nova solicitação do usuário.

O repositório Aegis recebe os fontes completos, incluindo o runtime do sandbox,
conforme a decisão posterior do usuário. Não substituir código por stubs para
publicação. Não incluir segredos, arquivos pessoais, ISOs, discos de VM ou builds
no repositório público. Publicações de builds usam apenas o repositório privado
`GabrielKLBr/Aegis-builds`, pelo fluxo `publish-builds` já existente.

Se testes, autenticação ou o envio falharem, preservar o trabalho e registrar
o ponto pendente com clareza; não afirmar que houve publicação. Instruções
posteriores do usuário podem alterar esta preferência.
