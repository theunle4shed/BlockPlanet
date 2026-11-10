#ifndef TETRISPHERE_RECOMP_SUPPORT_H
#define TETRISPHERE_RECOMP_SUPPORT_H

#include "recomp.h"
#include "tetrisphere/diagnostics.h"
#include "tetrisphere/guest_trace_hooks.h"
#include "tetrisphere/close_game_menu.h"
#include "tetrisphere/pc_settings_hook.h"

extern "C" {
bool tetrisphere_controls_view_hook(std::uint8_t*,recomp_context*);
void recomp_entrypoint(std::uint8_t* rdram, recomp_context* ctx);
void func_8006FA28(std::uint8_t* rdram, recomp_context* ctx);
void func_8002A8B4(std::uint8_t* rdram, recomp_context* ctx);  // load an archive entry: a0 archive, a1 entry, a2 8
void func_8002A100(std::uint8_t* rdram, recomp_context* ctx);  // load a 3D object (s2 archive): a0 entry
void func_8002A0C0(std::uint8_t* rdram, recomp_context* ctx);  // load a scene (s3 archive): a0 entry
void func_8002CF70(std::uint8_t* rdram, recomp_context* ctx);  // build a scene: a0 memory, a1 scene, a2 objects, a3 1, sp+0x10 size out
void func_8002D93C(std::uint8_t* rdram, recomp_context* ctx);  // draw a built scene: a0 scene, a1 frames elapsed
void func_8002D93C(std::uint8_t* rdram, recomp_context* ctx);
void func_80029510(std::uint8_t* rdram, recomp_context* ctx);
void func_800295D4(std::uint8_t* rdram, recomp_context* ctx);
void func_80029674(std::uint8_t* rdram, recomp_context* ctx);
void func_80029E10(std::uint8_t* rdram, recomp_context* ctx);
void func_80028824(std::uint8_t* rdram, recomp_context* ctx);
void func_80029938(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osInitialize(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osPiRawReadIo(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osCreateThread(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osStartThread(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_wait_retraces(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osSetWatchLo(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osClearWatch(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osProbeTLB(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osExceptionDispatch(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osExceptionResume(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_osSetCompare(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_probe_prompt_atlas(std::uint8_t* rdram, recomp_context* ctx,
                                   uint32_t renderer_vram);
void tetrisphere_training_reset();
void tetrisphere_training_clear_cache();
void tetrisphere_training_replace(std::uint8_t* rdram, recomp_context* ctx,
                                  uint32_t register_index);
void tetrisphere_training_record(std::uint8_t* rdram, recomp_context* ctx,
                                 uint32_t destination);
void tetrisphere_training_render(std::uint8_t* rdram, recomp_context* ctx);
bool blockplanet_sphere_mesh_begin(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_sphere_mesh_end(std::uint8_t* rdram, recomp_context* ctx);
bool blockplanet_sphere_cell_begin(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_sphere_cell_end(std::uint8_t* rdram, recomp_context* ctx);
bool blockplanet_sphere_face_begin(std::uint8_t* rdram, recomp_context* ctx, int face);
void blockplanet_sphere_face_end(std::uint8_t* rdram, recomp_context* ctx, int face);
bool blockplanet_sphere_quadrant_begin(std::uint8_t* rdram, recomp_context* ctx, int quadrant);
void blockplanet_sphere_quadrant_end(std::uint8_t* rdram, recomp_context* ctx, int quadrant);
void blockplanet_audio_frame(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_direct_start(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_logo_scene_stepped(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_logo_object_draw(std::uint8_t* rdram, std::uint32_t scene, int object);
void blockplanet_scale_music_volume(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_scale_effects_volume(std::uint8_t* rdram, recomp_context* ctx);
bool blockplanet_draw_filter(std::uint8_t* rdram, recomp_context* ctx, uint32_t helper);
void blockplanet_hud_begin(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_hud_end(std::uint8_t* rdram, recomp_context* ctx);
bool blockplanet_skip_queue_model(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_held_magic_draw(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_magic_flight(std::uint8_t* rdram, recomp_context* ctx);
bool blockplanet_magic_box_active();
bool blockplanet_scene_object(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_title_letters(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_title_letters_end(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_title_freeze(std::uint8_t* rdram, recomp_context* ctx);
void blockplanet_title_picture(std::uint8_t* rdram, recomp_context* ctx);
bool blockplanet_title_skip_image(std::uint8_t* rdram, std::uint32_t image);
bool blockplanet_title_recent(std::uint32_t* image);
bool blockplanet_title_skip_object(std::uint32_t scene, int object);
bool blockplanet_title_object(std::uint8_t* rdram, std::uint32_t scene, int object, std::uint32_t parts);
void blockplanet_title_watch(std::uint8_t* rdram);
void blockplanet_fn_probe(std::uint8_t* rdram, recomp_context* ctx, uint32_t fn);
void blockplanet_model_draw_end(std::uint8_t* rdram, recomp_context* ctx);
void tetrisphere_rescue_pause_snapshot_hook(std::uint8_t* rdram,
                                            recomp_context* ctx);
void tetrisphere_ui_span_begin(std::uint8_t* rdram, recomp_context* ctx,
                               std::uint32_t callsite, std::uint32_t anchor);
void tetrisphere_ui_span_end(std::uint8_t* rdram, recomp_context* ctx,
                             std::uint32_t callsite);

void __osAiDeviceBusy_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osAiGetLength_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osAiSetFrequency_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osAiSetNextBuffer_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osContGetReadData_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osContStartReadData_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osCreateMesgQueue_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osCreatePiManager_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osCreateViManager_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osDpSetNextBuffer_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osEepromRead_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osEepromProbe_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osEepromWrite_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osGetCount_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osGetTime_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osInvalDCache_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osJamMesg_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osPiStartDma_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osRecvMesg_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSendMesg_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSetEventMesg_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSetThreadPri_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSetTime_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSetTimer_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSpTaskLoad_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSpTaskStartGo_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSpTaskYield_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osSpTaskYielded_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osStopThread_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osViGetNextFramebuffer_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osViGetCurrentFramebuffer_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osViSetEvent_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osViSetMode_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osViSetSpecialFeatures_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osViSwapBuffer_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osWritebackDCacheAll_recomp(std::uint8_t* rdram, recomp_context* ctx);
void osWritebackDCache_recomp(std::uint8_t* rdram, recomp_context* ctx);
}

#endif
