# SNESticle Aurora

**Current release series:** 1.0.x

**First Aurora release:** 1.0.0

**Maintainer:** itsveenee

## Lineage

SNESticle Aurora is based on SNESticle Revive by ReyFxck (Thomas R.), which is based on the original SNESticle by Icer Addis.

The Aurora name identifies this project's own releases and development direction. References to SNESticle Revive are intentionally retained where they describe project history, upstream lineage or ReyFxck's work.

<!-- AURORA_BRANDING_PROJECT_V9_20260821 -->
## Name and branding

**SNESticle Aurora**, and **Aurora** when used as this emulator/project's identity, are the project-specific name and branding reserved by **Vinícius Nunes (`@itsveenee`)**. The software licenses permit the code uses they expressly grant, but they do **not** license this project identity for an unofficial fork, modified build, redistribution or derivative project. Unless separately authorized by @itsveenee, use a distinct project name and distinct branding.

Factual attribution such as **“based on SNESticle Aurora”** is welcome and does not imply endorsement. See [`../BRANDING.md`](../BRANDING.md).

This does not claim ownership of **SNESticle** standing alone or of the word **Aurora** in unrelated contexts.

## Compatibility

The historical SNESticle data-directory name used on PlayStation 2 storage devices is intentionally retained for compatibility.

Renaming the project to SNESticle Aurora does not rename existing configuration, SRAM, save-state, palette, BGM or other user-data directories.

## Runtime data and firmware

<!-- AURORA_RUNTIME_SYSTEM_V6_20260824 -->
The historical `SNESticle` data-directory name remains the compatibility root. Native SNESticle and Snes9x 2010 share raw SNES SRAM under `SNESticle/SNES`. Aurora creates `SNESticle/SYSTEM` on the active SRAM storage for user-supplied Sega CD and PC Engine CD firmware; no BIOS is included.

The PS2 frontend exposes `.cue` CD images only. CHD remains disabled until its additional libraries and runtime memory can be validated safely on real 32 MiB hardware.

## Core credits and licenses

Core lineage and redistribution terms are documented in [`../CREDITS.md`](../CREDITS.md) and [`../THIRD_PARTY.md`](../THIRD_PARTY.md). Top-level notice mirrors are stored in `../LICENSES/`, including the separate Snes9x 2010 license.

## Development relationship
SNESticle Aurora and SNESticle Revive can exchange useful changes through normal Git workflows such as cherry-picking, patches or pull requests.

Neither project is documented as being required to automatically follow the other's release schedule or merge every upstream change.

See `../CREDITS.md` for attribution details.
