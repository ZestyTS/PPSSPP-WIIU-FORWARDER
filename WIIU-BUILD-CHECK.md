# Standalone source build check - 2026-08-30

Source tested: 58e96ce, with a clean working tree. Runtime code matches 62f03ca.
The new Build-TiramisuRuntime.ps1 entrypoint successfully configured an empty
build directory, compiled the ELF, converted it, preserved SDA values and
passed the existing official readrpl format/import/CRC checks.

- Variant: TiramisuControl
- Builder: ghcr.io/wiiu-env/devkitppc:20230621
- Builder digest: sha256:173f418076dbe51bf1b52d8ca24bde13e1ed26fc7ef7c88ee04fc24981b82238
- Converter: devkitpro/devkitppc
- Converter digest: sha256:44cb1a920e1ec3ec7c06767493c3b85f8d643d6137cc4661f0201895ac6e4967
- ELF SHA-256: 8335a19a0337aee2d2da619c4104a88860e84e4ea2740c0d5d948444edcfde82
- RPX SHA-256: 2ab2de23c63e35af6b65a9d0b5e5d5b7d34b44f212c75cc75f188174e54f8f64
- SDA: 0x10548050; SDA2: 0x10008000

The old source emits five compiler warnings: an SFMT default, an STL memmove
diagnostic, an unimplemented JIT block-exit warning, and two enum comparisons.
They were not suppressed or described as resolved during repository export.

This is a new validation-only binary, not a hardware-approved replacement.
Existing tester/release ZIPs were not changed. Retest on Tiramisu before
publishing it as a replacement runtime. Aroma remains unsupported.
