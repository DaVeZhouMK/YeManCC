# Installation and source builds

[Back to the English overview](../README.en.md) · [简体中文](getting-started.zh-CN.md)

## Install

1. Download the complete **YeManCC.zip** runtime package from [Releases](https://github.com/DaVeZhouMK/YeManCC/releases).
2. Extract it to `C:\SOFT\YeMan`. Keep `YeManCC` and `PowerControl` next to each other, with the packaged subdirectories intact.
3. Run `C:\SOFT\YeMan\YeManCC\YeManCC.exe`.

```text
C:\SOFT\YeMan\
├─ YeManCC\
│  ├─ YeManCC.exe
│  ├─ update-manifest.json
│  └─ CustomSteamLibrary\
└─ PowerControl\
   ├─ fan-host-v2\
   ├─ handheldcompanion-runtime\
   └─ pawnio\
```

The complete package includes the custom game library under `YeManCC/CustomSteamLibrary`. Use the runtime ZIP for installation; source archives are for development and review.

The updater validates the package manifest and preserves user data. Back up configuration before upgrading. The package manifest and release notes define the components required by each version.

## Build from source

The current development baseline uses Windows x64, Node.js 22, pnpm 11.16.0, .NET 10 SDK, Visual Studio C++ Build Tools, and the Windows SDK. Repository configuration and lockfiles define the exact requirements.

```powershell
pnpm install --frozen-lockfile
pnpm run build
pnpm run package
```

Use a Git checkout of `main` and preserve lockfiles and bundled dependency bytes. Formal releases build from their release tags; source ZIPs are for review and archival. Building and packaging do not constitute installation or hardware validation.

## Maintenance references

In the development mainline, Steam maintenance starts with `STEAM-UPGRADE.md`, `STEAM-RUNNING-LINK.md`, and `docs/steam-integration-map.json`. Follow the maintenance rules included with the repository.

Sleep, game input, fans, and monitoring require separate validation. A screen turning off, a process returning, or an interface reporting success does not by itself confirm in-game recovery.
