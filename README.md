# SNESticle Aurora 1.0.0 - In development

**SNESticle Aurora** is a PlayStation 2 emulator fork maintained by **@itsveenee**.

SNESticle Aurora is based on **SNESticle Revive by @ReyFxck (Thomas R.)**, whose work brought SNESticle back into active development, and ultimately on the original **SNESticle by Icer Addis**.

**Huge thanks to @ReyFxck** for his work on SNESticle Revive and for providing the foundation from which Aurora was created. **Huge thanks to Icer Addis** for creating the original SNESticle and its codebase.

<!-- AURORA_QUICKNES_CREDIT -->
NES emulation through **QuickNES** is based on the **QuickNES core originally by Shay Green**, with the libretro core maintained by **libretro contributors**. Aurora uses `itsveenee/QuickNES_Core` as a pinned Git submodule for its PS2 integration. See [THIRD_PARTY.md](THIRD_PARTY.md) and the license notices inside the QuickNES submodule.

Mega Drive and Master System emulation through **PicoDrive** is based on the PicoDrive emulator originally by **@notaz**, with its libretro core maintained by **@irixxxx and libretro contributors**, and it is in **experimental** state for now. Aurora uses `itsveenee/picodrive` as a pinned Git submodule for its PS2 integration. See [THIRD_PARTY.md](THIRD_PARTY.md) and the license notices inside the PicoDrive submodule.

<!-- AURORA_BEETLE_PCE_FAST_README_V14 -->
PC Engine / TurboGrafx-16 emulation through **Beetle PC Engine Fast** is based on the libretro port/fork of **Mednafen PCE Fast**. Aurora uses `itsveenee/beetle-pce-fast-libretro` as a pinned Git submodule at `src/third_party/beetle-pce-fast`, with PS2-specific integration and performance changes maintained for Aurora. The current Aurora frontend integration is **experimental and HuCard-focused** (`.pce`, including the existing ZIP/GZ content paths); upstream Beetle PCE Fast also contains CD-ROM² support, but Aurora does not currently expose that disc-loading path. See [THIRD_PARTY.md](THIRD_PARTY.md) and `LICENSES/Beetle-PCE-Fast-GPL-2.0.txt`.

SNESticle Aurora focuses on 240p CRT output, multi-system integration (including QuickNES, PicoDrive and Beetle PCE Fast), real PS2 hardware, accuracy and compatibility improvements, and other hardware-specific or experimental ideas.

SNESticle Aurora code covered by the GPL remains under GNU GPLv2; separately licensed third-party components remain under their own terms. **Code license and project branding are separate.** The applicable software licenses grant rights in the code; they do **not** grant permission to use the **SNESticle Aurora** name or the project-specific **Aurora** identity/branding for an unofficial fork, modified build, redistributed binary, or derivative project. Unless separately authorized by **@itsveenee**, use a distinct project/product name and distinct branding. Factual attribution such as **“based on SNESticle Aurora”** remains welcome. See [BRANDING.md](BRANDING.md).

Project lineage and attribution are documented in [CREDITS.md](CREDITS.md). See LICENSE, [BRANDING.md](BRANDING.md), and the third-party license files for licensing details.

Keep in mind: this project uses **AI-generated code**, but the changes are tested by me, and I'm a Human according to reCAPTCHA.


## Building from Git

Clone SNESticle Aurora together with its pinned third-party cores:

```bash
git clone --recurse-submodules https://github.com/itsveenee/SNESticleAurora.git
cd SNESticleAurora
git submodule update --init --recursive
make
```

If the repository was cloned without `--recurse-submodules`, run
`git submodule update --init --recursive` afterwards.


## What's new?

**FEATURES ADDED:**

Emulation:

* NES emulation with QuickNES
* Support added for more NES mappers: 13, 16, 18, 27, 48, 64, 65, 67, 68, 72, 77, 80, 82, 92, 96, 99, 101, 105, 118, 119, 151, 153, 155, 157, 158, 159, 185, 188, 210, 216, and 552. Every licensed NES and Famicom game will boot now.
* Changed SRAM and RAM initialization for both NES and SNES. This will fix all the very few games that rely on specific initial values to work properly.
* Experimental Mega Drive / Genesis + Sega Master System / Mark III + Game Gear emulation with PicoDrive
* Experimental PC Engine / TurboGrafx-16 HuCard emulation with Beetle PCE Fast (`.pce`, ZIP/GZ content paths; up to 5 pads)
* Fixes and improvements for the 240p display modes, improved screen positioning and overscan settings for each system and graphical resolution/modes.
* Turbo B/A for NES and SMS games
* Turbo toggle (hold R2+ANY BUTTON) for SNES and MD games
* In-game soft reset (L2+SELECT)
* SNES and MD mouse emulation
* Region selector

User interface:

* Save SRAM to USB
* Browse SRAM files
* Confirmation prompt for saving and loading states
* Faster UI navigation
* Many options to enable emulation hacks and compatibility modes (exchange accuracy for performance or vice-versa)
* Option to reload the emulator's .elf (very useful for upgrading and testing new builds)

*(**NOTE**: to find the options above, go to the Video Settings and change the pages with the circle button.)*

Just for fun:

* Famiclone audio option for NES games (swap duty cycles, a known hardware bug in some Famiclones you can intentionally turn on)
* Select SRAM size (to intentionally trigger anti-piracy screens and measures) for SNES games


**FIXED:**

* Pilotwings (SNES) mode 7 rendering, also fixes other games that rely on it
* Many other graphical glitches and inaccuracies on many NES, SNES and SEGA games


**TO BE FIXED:**

* Super Mario World 2 (SNES) performance
* Speedy Gonzales in Los Gatos Banditos (SNES) performance
* Top Gear (SNES) performance
* The Lost Vikings 1 and 2 (SNES) black screen
* Addams Family (SNES) graphical glitches and timing issues
* Sonic Blast Man (SNES) wrong colors
* Any other games with performance or graphical issues


**TO BE ADDED:**

* SA-1 Emulation
* Improve Mode 7, FX1 and FX2 emulation
* Famicom Disk System support
* Other stupid (or not-so-stupid) ideas I might come up with. Thanks!
