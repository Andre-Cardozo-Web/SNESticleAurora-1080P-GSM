# SNESticle Aurora 1.0.0

**SNESticle Aurora** is a PlayStation 2 emulator fork maintained by **itsveenee**.

Aurora is based on **SNESticle Revive by ReyFxck (Thomas R.)**, whose work brought SNESticle back into active development, and ultimately on the original **SNESticle by Icer Addis**.

**Huge thanks to ReyFxck** for his work on SNESticle Revive and for providing the foundation from which Aurora was created. **Huge thanks to Icer Addis** for creating the original SNESticle and its codebase.

Aurora focuses on 240p CRT output, NES/QuickNES integration and experiments, real PS2 hardware, accuracy and compatibility improvements, and other hardware-specific or experimental ideas.

**Any changes made in SNESticle Aurora are also available to anyone** who wants to cherry-pick, pull, or otherwise incorporate them into SNESticle Revive or any other fork as they see fit.

Project lineage and attribution are documented in [CREDITS.md](CREDITS.md). See LICENSE and the third-party license files for licensing details.

Keep in mind: this project uses **AI-generated code**, but the changes are tested by me, and I'm a Human according to reCAPTCHA.

---

**FEATURES ADDED:**

* QuickNES for NES games
* Fixes and improvements for the 240p display modes, improved screen positioning and overscan settings for each system and graphical resolution/modes.
* "Dirty fix" for font rendering in 240p *(a better fix should be implemented in the future)*
* Changed SRAM and RAM initialization for both NES and SNES. This will fix all the very few games that rely on specific initial values to work properly.
* Turbo B/A for NES games
* Region selector (to intentionally trigger region-lock screens) for SNES games
* Select SRAM size (to intentionally trigger anti-piracy screens and measures) for SNES games
* Browse SRAM files
* In-game soft reset (L2+SELECT)
* Famiclone audio option (swap duty cycles)
* Option to reload the .elf

*(**NOTE**: to find the options above, go to the Video Settings and change to page 2 with the circle button.)*

**TO BE FIXED:**

* Top Gear (SNES) is very slow
* Super Mario Bros. 2 (SNES, inside Super Mario All Stars) courtain transition effect not working

**TO BE ADDED:**

* Other stupid (or not-so-stupid) ideas I might come up with. Thanks!
