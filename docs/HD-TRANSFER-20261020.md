# Transfer from revision 12B to HD — 20/10/2026

**Result:** revision validated on Linux copied and checked in the original hard drive folder, with read and write enabled. Phase 12B remains open.

## Location and assembly

- Volume NTFS: UUID `01DB04DC26902A20`, `/dev/sda1` device in this session.
- Project original: `/media/gabri/01DB04DC26902A203/Desktop Folders (DO NOT ERASE)/AntiVirus`.
- The drive letter on Windows must be checked; the folder within the volume
  Go on. `Desktop Folders (DO NOT ERASE)/AntiVirus`.
- Linux working copy: `/home/gabri/Projetos/GuardianAV-12B-complete-20261020`.

The user authorized the disassembly and writing, including the disposal of the previous hibernation state. The normal mount continued to refuse writing because of metadata in Windows cache. The attempt with `remove_hiberfile` also refused to mount. After administrator authentication, `ntfsfix --clear-dirty` checked MFT/MFTMirr, corrected the required indicators, restarted the `$LogFile` diary and checked the alternative boot sector. The next mount with `udisksctl` confirmed `rw`.

The Linux mount point suffix changed in this session. Volume identification was made by UUID before recording.

## Backup and conference

Before replacing the files, the `/home/gabri/Projetos/GuardianAV-original-antes-copia-20261020-231646.zip` backup has been created and verified. It preserves the 409 existing files that would be reached by the package. Its manifest also identifies the 96 new paths. It is not a complete image of the hard drive or of all project build directories.

SHA-256 do backup:

```text
e260bb48619c336e37c54b99c843bda3e432426581143a00f8c3b5f3515240c3
```

The original font package was checked by checksum before copying:

```text
3f22ca9a77f012e91924b8e80e9a645eb1626ea98f911087214fe301bd3f3ae8
```

Of the 505 files in the package, 126 were recorded (30 replacements and 96 new ones), and 379 were already identical. The subsequent conference confirmed all 505, including the 368 hashes of code registered by full-04 validation. The copy record is [`original-hd-transfer-20261020.json`](../build-phase12b/original-hd-transfer-20261020.json). The subsequent documentary updates to the copy were also synchronized.

Previous historical directories and Windows evidences have been preserved. Linux and font packages, checksums, backup and manifest have been copied to `build-phase12b/delivery-20261020` in the original project. ZIP fonts preserves documentation from the moment before the transfer; this record documents the later update of the original folder.

## Continued 12B

The code revision continues to be full-04: 47 Debug, 47 Release and 30 ASan/UBSan tests passed on Linux without Wine. The download does not add a Windows run or closes the phase.

The Windows/MSVC/WHP acceptance of the same revision and the integration of new resources to PE profiles remain pending. The full scope requested by the user remains in effect. The 12C phase has not been initiated.

For Windows acceptance, in a Visual Studio 2022 x64 terminal, in the original project folder, using a new output folder:

```powershell
python tools\validate_phase12b.py --output build-phase12b\windows-full-20261020 --generator "Visual Studio 17 2022"
```

The implementation report and the limits are in [PHASE-12B-RESOURCES.md](PHASE-12B-RESOURCES.md).
