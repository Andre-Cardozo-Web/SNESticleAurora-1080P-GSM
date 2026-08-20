# Third-party components

SNESticle Aurora contains or integrates code from third-party projects. Their original license files and source-file notices remain authoritative.

## QuickNES

Aurora integrates QuickNES as the pinned Git submodule:

`src/third_party/quicknes`

Attribution:

- Original QuickNES / Nes_Emu: **Shay Green**
- Libretro QuickNES core: **libretro contributors**
- Aurora PS2 integration fork: **itsveenee/QuickNES_Core**

The QuickNES source tree contains its own license and source-file notices.
These files must be preserved when redistributing the QuickNES code.

## Other components

Aurora also contains or integrates third-party components inherited from SNESticle/SNESticle Revive, including InfoNES, miniz, libxmp-lite and PS2SDK-related libraries. Refer to each component's bundled license and source-file notices.

<!-- AURORA_PICODRIVE_CURRENT_LICENSES -->
## PicoDrive

The experimental Sega integration builds against the pinned Git submodule
`itsveenee/picodrive`. PicoDrive remains a separate third-party component and
its source tree is not patched by Aurora; ROM bytes are supplied through the
standard libretro `RETRO_ENVIRONMENT_GET_GAME_INFO_EXT` path.

The pinned component's own `COPYING` text is mirrored verbatim at
`LICENSES/PicoDrive-COPYING.txt`. Keep all upstream copyright/license headers
intact. Review the pinned PicoDrive terms before redistributing a linked
PicoDrive-enabled binary.

**GPLv2 compatibility note:** The PicoDrive COPYING file prohibits sale or commercial activity and requires complete source code for modified redistributions. Aurora currently links PicoDrive statically into the same ELF. Because those are additional restrictions beyond GPLv2, do not redistribute a PicoDrive-enabled combined Aurora binary unless you have confirmed separate permission or another lawful licensing basis for the combination. Source-only components remain subject to their respective licenses.

## m5x7 font

`assets/font/m5x7.ttf` is the **m5x7** font by **Daniel Linssen**, released under **CC0 1.0 Universal**. Attribution is not required by CC0, but is appreciated by the author.
