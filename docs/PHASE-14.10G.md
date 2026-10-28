# 14.10G — Reprodução PCM, WinMM e WASAPI

Entrega delimitada para **Windows x64, perfil NT 10.0.19045**, em modo
`--audio`. A aplicação e os adaptadores das DLLs permanecem no MiniNT.
Um auxiliar restrito executa as operações nativas de reprodução WinMM e
WASAPI. Esta entrega não representa toda a pilha multimídia do Windows.

## Contrato e recursos

O kernel `guardian-kernel-audio.gk` anuncia `GuardianAudio/7/guest`.
O protocolo 7 mantém os tamanhos do transporte anterior e acrescenta
operações de áudio canônicas. O manifesto vincula kernel, volume, perfil e
SHA-256 de `guardian-audio-host.exe`. O modo e as DLLs do protocolo 6 da
14.10F continuam separados; os adaptadores gráficos do modo áudio são
recompilados com o protocolo 7.

- **PCM:** inteiros de 16 bits, mono/estéreo, 22050, 44100 ou 48000 Hz.
  Apenas uma saída virtual, correspondente à saída padrão do host.
- **WinMM:** abrir, preparar/escrever/despreparar buffers, conclusão
  assíncrona `WHDR_DONE`, pausa/reinício, posição, reset e fechamento.
  Um thread do convidado consulta a conclusão nativa e atualiza os headers
  do convidado. Apenas `CALLBACK_NULL`; nenhum callback nativo é encaminhado.
- **WAV:** `PlaySoundW` síncrono com RIFF/WAVE PCM em memória ou arquivo do
  convidado. O arquivo é lido e interpretado no convidado. Aliases, recursos,
  loops, reprodução assíncrona e sons padrão do host não são implementados.
- **WASAPI:** enumerador, coleção e endpoint virtuais, ativação de
  `IAudioClient`, renderização compartilhada, buffers copiados, padding,
  início/parada/reset e relógio nativo `IAudioClock`. O formato preferido da
  saída virtual é PCM estéreo 48 kHz; não é o formato privado do dispositivo.
  Conversão PCM é feita pelo mecanismo de áudio do Windows. Exclusivo,
  eventos, loopback, propriedades do host e sessões personalizadas não são
  liberados. As interfaces seguem os headers e a ABI do SDK.
- **Falhas:** erros de inicialização e dispositivo são propagados. É possível
  fechar e abrir outro cliente; não existe recuperação transparente nem
  notificação de troca do dispositivo. Desconexão física não faz parte da
  aceitação automatizada. `GetDevicePeriod`/`GetStreamLatency` e notificações
  permanecem `E_NOTIMPL` na matriz.

IDs incluem sessão e são associados ao processo convidado. Threads do mesmo
processo podem alimentar uma saída; o encerramento do thread que abriu a saída
recolhe seus recursos. Ponteiros e handles de áudio nativos nunca cruzam o
transporte. Buffers são copiados em blocos de até 512 bytes, com escrita
sequencial e sem reutilização depois do envio. O auxiliar só libera memória
WinMM depois da conclusão ou do reset; uma falha irrecuperável nessa limpeza
encerra o auxiliar, em vez de liberar um buffer ainda usado pelo driver.

Limites: quatro streams, 32 buffers nativos, oito headers WinMM por stream,
256 KiB por buffer, 1 MiB de dados simultâneos e 16 MiB enviados por sessão.
O buffer WASAPI solicitado fica entre 20 e 500 ms; seu tamanho real vem da
API nativa. Relógio e padding refletem o dispositivo, sem prometer igualdade
de latência ou sincronização audiovisual completa.

O auxiliar mantém token restrito, integridade baixa, Job de um processo e
256 MiB, dois handles herdados e os prazos da 14.10F. O Job encerra o auxiliar
com o supervisor. Isso mantém as limitações de contenção documentadas na
14.10F: não é AppContainer nem uma prova de impossibilidade de escape.
Não há captura de microfone/câmera/loopback. A saída de som é um efeito
permitido no host; a validação não grava a saída nem garante percepção física
do som. O corpus independente usa silêncio.

## Matriz e limites de compatibilidade

[audio-api-matrix.json](../config/audio-api-matrix.json) distingue operações
nativas, estado local, recusas e pendências. Exports não equivalem à
implementação de todos os modos de uma API. Referências COM herdadas não são
uma medida de compatibilidade universal. Os objetos WASAPI deste recorte
permanecem no thread criador; marshaling entre apartments não é implementado.

Media Foundation, vídeo e codecs de mídia comprimida continuam pendentes,
assim como XAudio2, DirectSound, MIDI, captura, notificações e controle global
de volume. Imports ausentes são rejeitados antes do boot. As operações
gráficas herdadas mantêm os limites da 14.10F. Outros perfis NT não são
anunciados neste lote. Não há novo classificador de malware nesta entrega.

## Validação reproduzível

`AudioClient.cpp` usa imports e interfaces normais do SDK. O mesmo EXE é
executado nativamente e no MiniNT: reprodução, conclusão, cancelamento,
pausa, posição, buffers/padding e relógio WASAPI, além de WAV em memória e
arquivo. Uma variante separada verifica as recusas de política.

O teste independente do projeto **Whisky** é compilado sem alterar o fonte
fixado em [audio-corpus.json](../config/audio-corpus.json). O recibo registra
o hash do fonte e do binário. O mesmo EXE nativo/convidado reproduz 600 ms de
PCM silencioso. Fonte e EXE externos não são redistribuídos no pacote.

`AudioBrokerTests.cpp` cobre sessão/processo estrangeiros, ordem dos blocos,
buffers em uso, cotas, reset, encerramento de thread e falha do auxiliar.
As ferramentas verificam modo, manifesto, imports e vínculo do auxiliar.
A aceitação final executa somente os quatro testes `audio_*`, em Release e
Debug, e repete os testes da 14.10G no pacote extraído. Por solicitação do
usuário, não exige repetir as baterias das fases anteriores. Comparações
gráficas e tentativas de regressão anteriores foram preservadas como histórico,
sem serem apresentadas como a aceitação final deste lote.

Uma tentativa antiga registrou falha intermitente de temporização da 12D;
duas repetições isoladas passaram com os mesmos fontes e binários. Outra
rodada foi interrompida por falta de memória no host. Esses resultados não
foram convertidos em aprovações nem implicam que a falha da 12D foi corrigida.

Configure `GUARDIAN_BUILD_AUDIO=ON` e `GUARDIAN_AUDIO_CORPUS` com a pasta
do fonte independente fixado. Compile `guardian_audio_runtime` e
`guardian-desktop` para Release/Debug. Execute `ctest -R '^audio_'` com as
referências locais legais configuradas. Para gerar um volume, use
`tools/audio_runtime.py --build BUILD --config Release --reference CRT
--schema-reference SCHEMA --output NOVO --asset tone.wav`. Inicie
`guardian-desktop.exe` com os cinco caminhos usuais de kernel/volume/manifesto/
binding/saída e `--audio --seconds 30`.

Fontes são publicados no Aegis; builds somente no Aegis-builds privado.
Parar após esta entrega, sem iniciar a 14.11 ou outro lote de mídia.
