# Transferência da revisão 12B para o HD — 20/10/2026

**Resultado:** revisão validada no Linux copiada e conferida na pasta original
do HD, com leitura e escrita habilitadas. A fase 12B continua aberta.

## Local e montagem

- Volume NTFS: UUID `01DB04DC26902A20`, dispositivo `/dev/sda1` nesta sessão.
- Projeto original: `/media/gabri/01DB04DC26902A203/Desktop Folders (DO NOT ERASE)/AntiVirus`.
- A letra da unidade no Windows deve ser conferida; a pasta dentro do volume
  continua `Desktop Folders (DO NOT ERASE)/AntiVirus`.
- Cópia de trabalho Linux: `/home/gabri/Projetos/GuardianAV-12B-complete-20261020`.

O usuário autorizou a desmontagem e a escrita, incluindo o descarte do estado
de hibernação anterior. A montagem normal continuou recusando escrita por causa
de metadados em cache do Windows. A tentativa com `remove_hiberfile` também
recusou a montagem. Após autenticação de administrador, `ntfsfix --clear-dirty`
conferiu MFT/MFTMirr, corrigiu os indicadores necessários, reiniciou o diário
`$LogFile` e conferiu o setor de boot alternativo. A montagem seguinte com
`udisksctl` confirmou `rw`.

O sufixo do ponto de montagem Linux mudou nesta sessão. A identificação do
volume foi feita pelo UUID, antes da gravação.

## Backup e conferência

Antes de substituir os arquivos, foi criado e verificado o backup
`/home/gabri/Projetos/GuardianAV-original-antes-copia-20261020-231646.zip`.
Ele preserva os 409 arquivos existentes que seriam alcançados pelo pacote.
Seu manifesto identifica também os 96 caminhos novos. Não é uma imagem completa
do HD nem de todos os diretórios de build do projeto.

SHA-256 do backup:

```text
e260bb48619c336e37c54b99c843bda3e432426581143a00f8c3b5f3515240c3
```

O pacote original de fontes foi conferido pelo checksum antes da cópia:

```text
3f22ca9a77f012e91924b8e80e9a645eb1626ea98f911087214fe301bd3f3ae8
```

Dos 505 arquivos no pacote, 126 foram gravados (30 substituições e 96 novos),
e 379 já eram idênticos. A conferência posterior confirmou todos os 505,
incluindo os 368 hashes de código registrados pela validação full-04.
O registro de cópia é
[`original-hd-transfer-20261020.json`](../build-phase12b/original-hd-transfer-20261020.json).
As atualizações documentais posteriores à cópia também foram sincronizadas.

Os diretórios históricos e as evidências Windows anteriores foram preservados.
Os pacotes Linux e de fontes, os checksums, o backup e seu manifesto foram
copiados para `build-phase12b/delivery-20261020` no projeto original.
O ZIP de fontes conserva a documentação do momento anterior à transferência;
este registro documenta a atualização posterior da pasta original.

## Continuação da 12B

A revisão de código continua sendo a full-04: 47 testes Debug, 47 Release e
30 ASan/UBSan passaram no Linux, sem Wine. A transferência não acrescenta
uma execução Windows nem encerra a fase.

Continuam pendentes a aceitação Windows/MSVC/WHP da mesma revisão e a
integração dos recursos novos aos perfis PE. O escopo completo solicitado pelo
usuário permanece em vigor. A fase 12C não foi iniciada.

Para a aceitação Windows, em um terminal x64 do Visual Studio 2022, na pasta
original do projeto, usando uma pasta de saída nova:

```powershell
python tools\validate_phase12b.py --output build-phase12b\windows-full-20261020 --generator "Visual Studio 17 2022"
```

O relatório de implementação e os limites estão em
[PHASE-12B-RECURSOS.md](PHASE-12B-RECURSOS.md).
