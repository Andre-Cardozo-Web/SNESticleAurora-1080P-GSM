# SNESticleRevive — 240p / Experimental Fork

A fork of ReyFxck/SNESticleRevive focused on 240p CRTs, NES/QuickNES experiments, and my own hardware-specific quirks.

Changes that prove useful or broadly applicable will be pushed to the main version when appropriate, while **updates from the main version will always be merged back into this fork.**

**Any changes made in this fork are also available to anyone** who wants to cherry-pick, pull, or otherwise incorporate them into the main version or any other fork as they see fit.

Original project and credits belong to the respective authors. See LICENSE for licensing and attribution details. **HUGE thanks** to @ReyFxck for his effort put into bringing this emulator back to life!

Keep in mind: this fork uses **AI-generated code** but it's always tested by me, and I'm a Human according to reCAPTCHA.

---

**FEATURES ADDED:**

* QuickNES for NES games
* Fixes and improvements for the 240p display modes
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

* Save and load states in QuickNES

**TO BE ADDED:**

* Individual and improved screen positioning and overscan options for each system and graphical resolution/modes.
* Other stupid (or not-so-stupid) ideas I might come up with. Thanks!
