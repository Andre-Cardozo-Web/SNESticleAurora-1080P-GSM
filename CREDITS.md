# SNESticle Aurora — Credits and Project Lineage

## SNESticle Aurora

**SNESticle Aurora** is maintained by **itsveenee**.

Aurora begins its own release series at **1.0.0**.

The Aurora project has its own name, releases, experiments and development direction while preserving the history and attribution of the projects on which it is based.

<!-- AURORA_BRANDING_CREDITS_V9_20260821 -->
## Name and branding

The **SNESticle Aurora** name, and **Aurora** when used as the identity of this emulator/project, are project-specific names and branding reserved by **Vinícius Nunes (`@itsveenee`)**. Rights granted under the applicable software licenses concern the code and do **not** grant a license to reuse that project identity for an unofficial fork, modified build, redistributed binary or derivative project. Unless separately authorized by @itsveenee, use a distinct name and branding. Factual provenance references such as **“based on SNESticle Aurora”** remain welcome. See [BRANDING.md](BRANDING.md).

This claim is limited to the **SNESticle Aurora / project-specific Aurora identity**. It does not claim ownership of **SNESticle** standing alone or of third-party names and marks.

## SNESticle Revive

SNESticle Aurora is based on **SNESticle Revive**, maintained by **ReyFxck (Thomas R.)**.

Huge thanks to ReyFxck for the work involved in bringing SNESticle back into active development, modernizing the PS2 project, and creating the base from which Aurora was developed.

Upstream project:

`ReyFxck/SNESticleRevive`

## Original SNESticle

The original **SNESticle** and its original codebase are by **Icer Addis**.

Huge thanks to Icer Addis for creating the original emulator.

Historical copyright notice:

`Copyright (c) 1997-2004 Icer Addis`

## Sharing changes between projects

Changes developed for SNESticle Aurora may be cherry-picked, pulled, adapted or otherwise incorporated into SNESticle Revive or other forks as permitted by the applicable licenses.

Likewise, Aurora may independently incorporate useful work from related projects when appropriate, without implying that the projects share a release schedule, version series or development direction.

## QuickNES

The NES QuickNES integration in Aurora uses a pinned fork of the
QuickNES/libretro codebase:

- **Original QuickNES / Nes_Emu:** Shay Green
- **Libretro QuickNES core:** libretro contributors
- **Aurora PS2 integration fork:** `itsveenee/QuickNES_Core`
- **Aurora integration and PS2-specific glue:** Vinícius Nunes (`@itsveenee`)

The QuickNES submodule retains its own license and source-file notices.
Those notices remain authoritative and must be preserved.

## Third-party components

SNESticle Aurora includes or integrates third-party components including InfoNES, QuickNES, FCEUmm, miniz, libxmp-lite and others.

Their original copyright notices, authorship information and license files remain authoritative and must be preserved.

See the repository LICENSE and the license files shipped with the corresponding third-party components.

<!-- AURORA_PICODRIVE_CURRENT_CREDITS -->
## PicoDrive

Experimental Mega Drive / Genesis, Master System, Game Gear and 32X support
uses PicoDrive (notaz and contributors; current development/fork contributors).
Aurora's PS2 bridge/integration is maintained in `itsveenee/SNESticleAurora`.
See `THIRD_PARTY.md` and `LICENSES/PicoDrive-COPYING.txt`.
