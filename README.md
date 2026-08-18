# SNESticle Aurora 1.0.0 - In development

**SNESticle Aurora** is a PlayStation 2 emulator fork maintained by **@itsveenee**.

SNESticle Aurora is based on **SNESticle Revive by @ReyFxck (Thomas R.)**, whose work brought SNESticle back into active development, and ultimately on the original **SNESticle by Icer Addis**.

**Huge thanks to @ReyFxck** for his work on SNESticle Revive and for providing the foundation from which Aurora was created. **Huge thanks to Icer Addis** for creating the original SNESticle and its codebase.

<!-- AURORA_QUICKNES_CREDIT -->
NES emulation through **QuickNES** is based on the **QuickNES core originally by Shay Green**, with the libretro core maintained by **libretro contributors**. Aurora uses `itsveenee/QuickNES_Core` as a pinned Git submodule for its PS2 integration. See [THIRD_PARTY.md](THIRD_PARTY.md) and the license notices inside the QuickNES submodule.

SNESticle Aurora focuses on 240p CRT output, NES/QuickNES integration and experiments, real PS2 hardware, accuracy and compatibility improvements, and other hardware-specific or experimental ideas.

**Any changes made in SNESticle Aurora are also available to anyone** who wants to cherry-pick, pull, or otherwise incorporate them into SNESticle Revive or any other fork as they see fit.

Project lineage and attribution are documented in [CREDITS.md](CREDITS.md). See LICENSE and the third-party license files for licensing details.

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

* QuickNES for NES games
* Support added for more NES mappers: 13, 16, 18, 27, 48, 64, 65, 67, 68, 72, 77, 80, 82, 92, 96, 99, 101, 105, 118, 119, 151, 153, 155, 157, 158, 159, 185, 188, 210, 216, and 552. Every licensed NES and Famicom game will boot now.
* Fixes and improvements for the 240p display modes, improved screen positioning and overscan settings for each system and graphical resolution/modes.
* "Dirty fix" for font rendering in 240p *(a better fix should be implemented in the future)*
* Changed SRAM and RAM initialization for both NES and SNES. This will fix all the very few games that rely on specific initial values to work properly.
* Turbo B/A for NES games
* Region selector (to intentionally trigger region-lock screens) for SNES games
* Select SRAM size (to intentionally trigger anti-piracy screens and measures) for SNES games
* Browse SRAM files
* In-game soft reset (L2+SELECT)
* Famiclone audio option (swap duty cycles)
* Option to reload the emulator's .elf (very useful for upgrading and testing)
* Emulation hacks and compatibility menu options (disable layers, change rendering options)

*(**NOTE**: to find the options above, go to the Video Settings and change the pages with the circle button.)*

**FIXED:**

* Many graphical glitches and inaccuracies on many NES and SNES games (testing still being done)
* Pilotwings (SNES) Mode 7 rendering

**TO BE FIXED:**

* Super Mario World 2 (SNES) performance
* Top Gear (SNES) performance
* Speedy Gonzales in Los Gatos Banditos (SNES) performance
* The Lost Vikings 1 and 2 (SNES) black screen
* Addams Family (SNES) graphical glitches and timing issues
* Sonic Blast Man (SNES) wrong colors
* Any other games with performance or graphical issues

**TO BE ADDED:**

* Save SRAM to USB
* Save and load state confirmation prompt
* SA-1 Emulation
* Improve Mode 7, FX1 and FX2 emulation
* Turbo button toggle for SNES
* Famicom Disk System support
* Other stupid (or not-so-stupid) ideas I might come up with. Thanks!
