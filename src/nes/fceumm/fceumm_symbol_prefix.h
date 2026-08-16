#ifndef AURORA_FCEUMM_SYMBOL_PREFIX_H
#define AURORA_FCEUMM_SYMBOL_PREFIX_H

*(AURORA_FCEUMM_SYMBOL_PREFIX_V1)
 *
 * QuickNES and FCEUmm are both linked statically into the PS2 ELF.
 * Namespace FCEUmm's libretro ABI so a retro_* reference can never bind
 * to QuickNES's libretro.o (or vice versa).
 */

#define retro_init                       fceumm_retro_init
#define retro_deinit                     fceumm_retro_deinit
#define retro_api_version                fceumm_retro_api_version
#define retro_get_system_info            fceumm_retro_get_system_info
#define retro_get_system_av_info         fceumm_retro_get_system_av_info
#define retro_set_controller_port_device fceumm_retro_set_controller_port_device
#define retro_reset                      fceumm_retro_reset
#define retro_run                        fceumm_retro_run
#define retro_serialize_size             fceumm_retro_serialize_size
#define retro_serialize                  fceumm_retro_serialize
#define retro_unserialize                fceumm_retro_unserialize
#define retro_cheat_reset                fceumm_retro_cheat_reset
#define retro_cheat_set                  fceumm_retro_cheat_set
#define retro_load_game                  fceumm_retro_load_game
#define retro_load_game_special          fceumm_retro_load_game_special
#define retro_unload_game                fceumm_retro_unload_game
#define retro_get_region                 fceumm_retro_get_region
#define retro_get_memory_data            fceumm_retro_get_memory_data
#define retro_get_memory_size            fceumm_retro_get_memory_size
#define retro_set_environment            fceumm_retro_set_environment
#define retro_set_video_refresh          fceumm_retro_set_video_refresh
#define retro_set_audio_sample           fceumm_retro_set_audio_sample
#define retro_set_audio_sample_batch     fceumm_retro_set_audio_sample_batch
#define retro_set_input_poll             fceumm_retro_set_input_poll
#define retro_set_input_state            fceumm_retro_set_input_state

#endif
