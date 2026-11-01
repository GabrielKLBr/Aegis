# 14.10G — Reproduction PCM, WinMM and WASAPI

Delimited delivery for **Windows x64, NT profile 10.0.19045**, in `--audio` mode. The application and adapters of DLLs remain in MiniNT. A restricted helper performs native playback operations WinMM and WASAPI. This delivery does not represent the entire multimedia stack of Windows.

## Contract and resources

The `guardian-kernel-audio.gk` kernel announces `GuardianAudio/7/guest`. Protocol 7 maintains the previous transport sizes and adds canonical audio operations. The manifest links `guardian-audio-host.exe` kernel, volume, profile and SHA-256. The mode and DLLs of 14.10F protocol 6 remain separate; the audio mode graphical adapters are recompiled with protocol 7.

- **PCM:** 16-bit integers, mono/stereo, 22050, 44100 or 48000 Hz.
  Only a virtual output, corresponding to the default host output.
- **WinMM:** open, prepare/write/unprepare buffers, conclusion
  asynchronous `WHDR_DONE`, pause/restart, position, reset and closing. A guest thread consults the native conclusion and updates the guest headers. Only `CALLBACK_NULL`; no native callback is forwarded.
- **WAV:**`PlaySoundW` synchronous with RIFF/WAVE PCM in memory or file of
  guest. The file is read and interpreted in the guest. Aliases, features, loops, asynchronous playback and host default sounds are not implemented.
- **WASAPI:** enumerator, virtual collection and endpoint, activation of
  `IAudioClient`, shared rendering, copied buffers, padding, start/stop/reset and native `IAudioClock` clock. The preferred virtual output format is PCM stereo 48 kHz; it is not the device's private format. PCM conversion is made by Windows audio engine. Exclusive, events, loopback, host properties and custom sessions are not released. Interfaces follow SDK headers and ABI.
- **Failures:** boot errors and device are propagated. It is possible
  close and open another client; there is no transparent recovery or device exchange notification. Physical disconnection is not part of automated acceptance. `GetDevicePeriod`/`GetStreamLatency` and notifications remain `E_NOTIMPL` in the matrix.

IDs include session and are associated with the guest process. Threads of the same process can feed an output; closing the thread that opened the output collects its features. Native audio pointers and handsets never cross the transport. Buffers are copied in blocks of up to 512 bytes, with sequential writing and no reuse after upload. The helper only releases WinMM memory after completion or reset; an irrecoverable failure in this cleaning closes the auxiliary, instead of releasing a buffer still used by the driver.

Limits: four streams, 32 native buffers, eight WinMM headers per stream, 256 KiB per buffer, 1 MiB simultaneous data and 16 MiB sent per session. The requested WASAPI buffer is between 20 and 500 ms; its real size comes from the native API. Clock and padding reflect the device without promising equal latency or full audiovisual synchronization.

The helper keeps token restricted, low integrity, Job of a process and 256 MiB, two inherited handles and the deadlines of 14.10F. Job closes the auxiliary with the supervisor. This keeps the containment limitations documented in 14.10F: it is neither AppContainer nor an escape proof. There is no capture of microphone/camera/loopback. Sound output is a allowed effect on the host; validation does not record the output or ensure physical perception of sound. The independent corpus uses silence.

## Matrix and compatibility limits

[audio-api-matrix.json](../config/audio-api-matrix.json) distinguishes native operations, local state, refusals and to-ed. Exports do not equal the implementation of all modes of an API. Inherited COM references are not a measure of universal compatibility. WASAPI objects of this cut remain in the creator thread; marshalling between apartments is not implemented.

Media Foundation, video and compressed media codecs remain pending, as well as XAudio2, DirectSound, MIDI, capture, notifications and global volume control. Absent imports are rejected before boot. The inherited graphical operations keep the limits of 14.10F. Other NT profiles are not announced in this batch. There is no new malware classifier on this delivery.

## Reproducible validation

`AudioClient.cpp` uses standard SDK import and interfaces. The same EXE runs natively and on MiniNT: playback, completion, cancellation, pause, position, buffers/padding and WASAPI watch, as well as WAV in memory and file. A separate variant checks policy refusals.

The independent test of the project **Whisky** is compiled without changing the source fixed in [audio-corpus.json](../config/audio-corpus.json). The receipt records the hash of the source and binary. The same EXE native/guest reproduces 600 ms of silent PCM. External source and EXE are not redistributed in the package.

`AudioBrokerTests.cpp` covers foreign meeting/Case, order of blocks, buffers in use, quotas, reset, thread closure, and auxiliary failure. The tools check mode, manifest, imports and auxiliary link. Final acceptance only performs the four `audio_*` tests, in Release and Debug, and repeats the 14.10G tests in the extracted package. At user's request, it does not require repeating the batteries of the previous phases. Graphic comparisons and previous regression attempts were preserved as historical, without being presented as the final acceptance of this batch.

An old attempt recorded intermittent failure of 12D timing; two isolated repetitions passed with the same sources and binaries. Another round was interrupted by lack of memory in the host. These results were not converted into approvals nor implied that the failure of 12D was corrected.

Configure `GUARDIAN_BUILD_AUDIO=ON` and `GUARDIAN_AUDIO_CORPUS` with the fixed independent source folder. Compile `guardian_audio_runtime` and `guardian-desktop` for Release/Debug. Run `ctest -R '^audio_'` with the legal local references configured. To generate a volume, use `tools/audio_runtime.py --build BUILD --config Release --reference CRT --schema-reference SCHEMA --output NOVO --asset tone.wav`. Start `guardian-desktop.exe` with the five usual paths of kernel/volume/manifest/ binding/exit and `--audio --seconds 30`.

Sources are posted in Aegis; builds only in private Aegis-builds. Stop after this delivery, without starting at 14.11 or other batch of media.
