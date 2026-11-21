#include "tetrisphere/rt64_context.h"
#include <tuple>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include "tetrisphere/frame_step.h"
#include "tetrisphere/data_directory.h"
#include "tetrisphere/brick_textures.h"
#include "ui_system.h"
#include "tetrisphere/diagnostic_log.h"
#include "tetrisphere/dl_ui_tags.h"
#include "tetrisphere/f3d_flat_shade.h"
#include "tetrisphere/menu_medal_trace.h"
#include "tetrisphere/pause_capture_scene.h"
#include "tetrisphere/perf_metrics.h"
#include "tetrisphere/run_identity.h"
#include "tetrisphere/run_options.h"
#include "tetrisphere/runtime.h"
#include "tetrisphere/recomp_support.h"

#ifdef _WIN32
// ultramodern's public renderer header opts into WIN32_LEAN_AND_MEAN before
// RT64's DXC declarations are reached. Supply the COM types DXC requires.
#include <ObjIdl.h>
#include <OleAuto.h>
#include <Unknwn.h>
#endif

#include "hle/rt64_application.h"
#include "common/rt64_tmem_hasher.h"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <utility>

namespace {
// The guest copy finishes after its source display list has been submitted.
// The wide renderer consumes this event in workload order; the CPU RAM copy
// remains authoritative for the native renderer and for 4:3.
std::atomic<std::uint64_t> rescue_snapshot_source_workload{0};
std::mutex rescue_snapshot_mutex;
struct RescueSnapshotEvent {
    std::uint32_t source = 0;
    std::uint32_t destination = 0;
    std::uint64_t workload = 0;
    std::uint64_t generation = 0;
};
std::deque<RescueSnapshotEvent> rescue_snapshot_events;
std::uint64_t rescue_snapshot_generation = 0;
std::mutex ui_tag_mutex;
tetrisphere::DlUiTagQueue ui_tag_queue;
thread_local tetrisphere::DlUiTagBatch ui_active_batch;
thread_local std::uint32_t ui_current_tag = 0;
struct UiProducerScope {
    std::uint32_t callsite = 0;
    std::uint32_t first = 0;
    std::uint32_t anchor = 0;
};
thread_local std::vector<UiProducerScope> ui_producer_scopes;
}

// Guest producer hooks record the commands emitted by a verified UI callsite.
// A Center span can explicitly override an ancestor display-list tag.
extern "C" bool tetrisphere_rt64_ui_tag_record(std::uint8_t* rdram,
                                                std::uint32_t first,
                                                std::uint32_t past_last,
                                                std::uint32_t anchor) {
    if (anchor < 1 || anchor > 7) return false;
    const std::lock_guard<std::mutex> lock(ui_tag_mutex);
    return ui_tag_queue.record(first & 0x00FFFFFFu, past_last & 0x00FFFFFFu,
        static_cast<tetrisphere::DlUiAnchor>(anchor), rdram, 9437184);
}

extern "C" void tetrisphere_rt64_ui_tag_update(
    std::uint32_t command, const std::uint32_t* parents, std::size_t count) {
    ui_current_tag = static_cast<std::uint32_t>(
        ui_active_batch.lookup(command, parents, count));
}

extern "C" std::uint32_t tetrisphere_rt64_ui_tag_current() {
    return ui_current_tag;
}

extern "C" void tetrisphere_ui_span_begin(std::uint8_t* rdram,
                                           recomp_context* ctx,
                                           std::uint32_t callsite,
                                           std::uint32_t anchor) {
    static_cast<void>(ctx);
    if (anchor < 1 || anchor > 7 || ui_producer_scopes.size() >= 32) return;
    const std::uint32_t first =
        MEM_W(0, S32(0x800F22B4u)) & 0x00FFFFFFu;
    ui_producer_scopes.push_back({callsite, first, anchor});
}

extern "C" void tetrisphere_ui_span_end(std::uint8_t* rdram,
                                         recomp_context* ctx,
                                         std::uint32_t callsite) {
    static_cast<void>(ctx);
    if (ui_producer_scopes.empty() ||
        ui_producer_scopes.back().callsite != callsite) return;
    const UiProducerScope scope = ui_producer_scopes.back();
    ui_producer_scopes.pop_back();
    const std::uint32_t past_last =
        MEM_W(0, S32(0x800F22B4u)) & 0x00FFFFFFu;
    if (past_last <= scope.first ||
        !tetrisphere_rt64_ui_tag_record(rdram, scope.first, past_last, scope.anchor)) return;
    static std::atomic<unsigned> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) < 8) {
        std::fprintf(stderr,
                     "{\"event\":\"rt64_probe_ui_tag_span\","
                     "\"callsite\":%u,\"first\":%u,"
                     "\"past_last\":%u,\"anchor\":%u}\n",
                     callsite, scope.first, past_last, scope.anchor);
    }
}

extern "C" void tetrisphere_rescue_pause_snapshot_hook(std::uint8_t* rdram,
                                                        recomp_context* ctx) {
    static_cast<void>(ctx);
    std::int16_t scene_gate = 0;
    std::memcpy(&scene_gate, rdram + (0x000E121Cu ^ 2u), sizeof(scene_gate));
    if (!tetrisphere::pause_capture_scene_eligible(scene_gate)) return;
    const std::int64_t context = MEM_W(0, S32(0x801028CCu));
    const std::uint32_t source = MEM_W(0x1910, context) & 0x00FFFFFFu;
    const std::uint32_t destination = MEM_W(0, S32(0x800DFF00u)) & 0x00FFFFFFu;
    if (!tetrisphere::transition_capture_buffers_eligible(source,destination)) return;
    const auto source_workload = rescue_snapshot_source_workload.load(std::memory_order_acquire);
    {
        const std::lock_guard<std::mutex> lock(rescue_snapshot_mutex);
        if (rescue_snapshot_events.size() == 32) {
            std::fprintf(stderr, "{\"event\":\"rt64_rescue_snapshot_queue_overflow\",\"action\":\"reject_newest\"}\n");
            return;
        }
        rescue_snapshot_events.push_back({source, destination, source_workload,
                                          ++rescue_snapshot_generation});
    }
    if (const char* probe = std::getenv("TETRISPHERE_RT64_PROBE");
        probe != nullptr && probe[0] == '1' && probe[1] == '\0') {
        std::fprintf(stderr,
                     "{\"event\":\"rt64_probe_rescue_snapshot_hook\","
                     "\"source\":%u,\"destination\":%u,\"scene\":%d,\"caller\":%u,\"source_workload\":%llu}\n",
                     source, destination, int(scene_gate),std::uint32_t(ctx->r31),
                     static_cast<unsigned long long>(source_workload));
    }
}

extern "C" bool tetrisphere_rt64_pause_snapshot_peek(std::uint32_t* source,
                                                       std::uint32_t* destination,
                                                       std::uint64_t* workload,
                                                       std::uint64_t* generation) {
    const std::lock_guard<std::mutex> lock(rescue_snapshot_mutex);
    if (rescue_snapshot_events.empty()) return false;
    const auto& event = rescue_snapshot_events.front();
    *source = event.source;
    *destination = event.destination;
    *workload = event.workload;
    *generation = event.generation;
    return true;
}

extern "C" void tetrisphere_rt64_pause_snapshot_ack(std::uint64_t generation) {
    const std::lock_guard<std::mutex> lock(rescue_snapshot_mutex);
    if (!rescue_snapshot_events.empty() &&
        rescue_snapshot_events.front().generation == generation) {
        rescue_snapshot_events.pop_front();
    }
}

namespace tetrisphere {
namespace {

std::mutex live_filters_mutex;
std::optional<tetrisphere::PresentationFilter> pending_presentation;
std::optional<bool> pending_texture;
std::mutex pending_bricks_mutex;
std::optional<std::string> pending_bricks;  // Graphics > Bricks, for the render thread


std::array<std::uint8_t, 0x40> rom_header{};
std::array<std::uint8_t, 0x1000> dmem{};
std::array<std::uint8_t, 0x1000> imem{};
std::uint32_t mi_intr = 0;
std::uint32_t dpc_start = 0;
std::uint32_t dpc_end = 0;
std::uint32_t dpc_current = 0;
std::uint32_t dpc_status = 0;
std::uint32_t dpc_clock = 0;
std::uint32_t dpc_bufbusy = 0;
std::uint32_t dpc_pipebusy = 0;
std::uint32_t dpc_tmem = 0;
std::atomic_uint64_t display_list_count = 0;
std::atomic_uint64_t present_count = 0;
std::optional<double> internal_resolution_scale;
tetrisphere::RunOptions run_options;

void check_interrupts() {}

ultramodern::renderer::SetupResult setup_result(RT64::Application::SetupResult value) {
    using Host = ultramodern::renderer::SetupResult;
    using Rt = RT64::Application::SetupResult;
    switch (value) {
        case Rt::Success: return Host::Success;
        case Rt::DynamicLibrariesNotFound: return Host::DynamicLibrariesNotFound;
        case Rt::InvalidGraphicsAPI: return Host::InvalidGraphicsAPI;
        case Rt::GraphicsAPINotFound: return Host::GraphicsAPINotFound;
        case Rt::GraphicsDeviceNotFound: return Host::GraphicsDeviceNotFound;
    }
    std::abort();
}

// ui/ beside the executable, or BLOCKPLANET_UI_ASSETS.
std::filesystem::path ui_assets_directory() {
    if (const char* override = std::getenv("BLOCKPLANET_UI_ASSETS"); override != nullptr && *override != '\0')
        return std::filesystem::u8path(override);
    return tetrisphere::executable_directory() / "ui";
}

class Context final : public ultramodern::renderer::RendererContext {
public:
    Context(std::uint8_t* rdram, ultramodern::renderer::WindowHandle window,
            bool developer_mode) {
        RT64::Application::Core core{};
#ifdef _WIN32
        core.window = window.window;
#else
        core.window = window;
#endif
        core.checkInterrupts = check_interrupts;
        core.HEADER = rom_header.data();
        core.RDRAM = rdram;
        research_rdram_ = rdram;
        core.DMEM = dmem.data();
        core.IMEM = imem.data();
        core.MI_INTR_REG = &mi_intr;
        core.DPC_START_REG = &dpc_start;
        core.DPC_END_REG = &dpc_end;
        core.DPC_CURRENT_REG = &dpc_current;
        core.DPC_STATUS_REG = &dpc_status;
        core.DPC_CLOCK_REG = &dpc_clock;
        core.DPC_BUFBUSY_REG = &dpc_bufbusy;
        core.DPC_PIPEBUSY_REG = &dpc_pipebusy;
        core.DPC_TMEM_REG = &dpc_tmem;
        const auto* vi = ultramodern::renderer::get_vi_regs();
        core.VI_STATUS_REG = const_cast<std::uint32_t*>(&vi->VI_STATUS_REG);
        core.VI_ORIGIN_REG = const_cast<std::uint32_t*>(&vi->VI_ORIGIN_REG);
        core.VI_WIDTH_REG = const_cast<std::uint32_t*>(&vi->VI_WIDTH_REG);
        core.VI_INTR_REG = const_cast<std::uint32_t*>(&vi->VI_INTR_REG);
        core.VI_V_CURRENT_LINE_REG = const_cast<std::uint32_t*>(&vi->VI_V_CURRENT_LINE_REG);
        core.VI_TIMING_REG = const_cast<std::uint32_t*>(&vi->VI_TIMING_REG);
        core.VI_V_SYNC_REG = const_cast<std::uint32_t*>(&vi->VI_V_SYNC_REG);
        core.VI_H_SYNC_REG = const_cast<std::uint32_t*>(&vi->VI_H_SYNC_REG);
        core.VI_LEAP_REG = const_cast<std::uint32_t*>(&vi->VI_LEAP_REG);
        core.VI_H_START_REG = const_cast<std::uint32_t*>(&vi->VI_H_START_REG);
        core.VI_V_START_REG = const_cast<std::uint32_t*>(&vi->VI_V_START_REG);
        core.VI_V_BURST_REG = const_cast<std::uint32_t*>(&vi->VI_V_BURST_REG);
        core.VI_X_SCALE_REG = const_cast<std::uint32_t*>(&vi->VI_X_SCALE_REG);
        core.VI_Y_SCALE_REG = const_cast<std::uint32_t*>(&vi->VI_Y_SCALE_REG);

        // BlockPlanet's menu layer draws through RT64's hooks, which must be
        // registered before RT64 starts.
        // The menu grid's tile texture is read from the ROM (ui_grid.h).
        blockplanet::ui::set_rom(tetrisphere::runtime_rom_bytes());
        blockplanet::ui::install(ui_assets_directory());
        RT64::ApplicationConfiguration config{};
        config.appId = "tetrisphere-m1";
        config.useConfigurationFile = false;
        app_ = std::make_unique<RT64::Application>(core, config);
        app_->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
        app_->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
        app_->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        app_->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
        // --developer: RT64's inspector on F1 (it then takes keyboard and mouse
        // input itself, through an SDL event filter, while it is open).
        app_->userConfig.developerMode = developer_mode;
        if (run_options.msaa_samples) {
            switch (*run_options.msaa_samples) {
                case 0: app_->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None; break;
                case 2: app_->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::MSAA2X; break;
                case 4: app_->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::MSAA4X; break;
                case 8: app_->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::MSAA8X; break;
            }
        }
        if (run_options.presentation_filter) {
            switch (*run_options.presentation_filter) {
                case PresentationFilter::Nearest:
                    app_->userConfig.filtering = RT64::UserConfiguration::Filtering::Nearest; break;
                case PresentationFilter::Linear:
                    app_->userConfig.filtering = RT64::UserConfiguration::Filtering::Linear; break;
                case PresentationFilter::Pixel:
                    app_->userConfig.filtering = RT64::UserConfiguration::Filtering::AntiAliasedPixelScaling; break;
            }
        }
        if (run_options.three_point_filter)
            app_->userConfig.threePointFiltering = *run_options.three_point_filter;
        // Apply the host settings before RT64 creates its render targets.
        host_graphics_ = ultramodern::renderer::get_graphics_config();
        if (!apply_rt64_graphics_config(host_graphics_,
                                        app_->userConfig, internal_resolution_scale)) {
            diagnostic_log().error("rt64_graphics_config_invalid");
        }
        // Console is Blocksphere's default. In this mode RT64 cannot interpolate
        // frames and its present thread takes the workload lock, so BlockPlanet
        // lets players compare the other modes with --present-mode.
        switch (run_options.present_mode.value_or(PresentMode::Console)) {
            case PresentMode::Console:
                app_->enhancementConfig.presentation.mode =
                    RT64::EnhancementConfiguration::Presentation::Mode::Console; break;
            case PresentMode::SkipBuffering:
                app_->enhancementConfig.presentation.mode =
                    RT64::EnhancementConfiguration::Presentation::Mode::SkipBuffering; break;
            case PresentMode::Early:
                app_->enhancementConfig.presentation.mode =
                    RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly; break;
        }
        // RT64 renders every frame a second time at native resolution and
        // waits for the GPU to copy it into RDRAM. BlockPlanet lets players
        // turn that off with --framebuffer-readback=off.
        app_->emulatorConfig.framebuffer.renderToRAM =
            run_options.framebuffer_readback.value_or(true);
        setup_result = ::tetrisphere::setup_result(app_->setup(0));
        if (setup_result == ultramodern::renderer::SetupResult::Success) sharp_bricks_init();
        // Research: BLOCKPLANET_TEXTURE_DUMP=dir has RT64 dump each texture's
        // TMEM under its replacement hash (for the sharp bricks).
        if (const char* dump = std::getenv("BLOCKPLANET_TEXTURE_DUMP");
            dump != nullptr && setup_result == ultramodern::renderer::SetupResult::Success) {
            std::filesystem::create_directories(dump);
            app_->state->dumpingTexturesDirectory = std::filesystem::u8path(dump);
        }
        if (setup_result == ultramodern::renderer::SetupResult::Success) {
            const unsigned requested_samples = run_options.msaa_samples
                ? static_cast<unsigned>(*run_options.msaa_samples == 0
                    ? 1 : *run_options.msaa_samples) : 0;
            if (requested_samples != 0 &&
                app_->userConfig.msaaSampleCount() != requested_samples) {
                std::fprintf(stderr,
                             "{\"event\":\"rt64_msaa_fallback\",\"requested_samples\":%u,"
                             "\"effective_samples\":%u}\n",
                             requested_samples, app_->userConfig.msaaSampleCount());
            }
            std::fprintf(stdout,
                         "{\"event\":\"rt64_effective_filters\",\"msaa_samples\":%u,"
                         "\"presentation_filter\":%d,\"three_point_texture_filter\":%s,"
                         "\"present_mode\":\"%s\",\"framebuffer_readback\":%s}\n",
                         app_->userConfig.msaaSampleCount(),
                         static_cast<int>(app_->userConfig.filtering),
                         app_->userConfig.threePointFiltering ? "true" : "false",
                         present_mode_name(run_options.present_mode.value_or(PresentMode::Console)),
                         app_->emulatorConfig.framebuffer.renderToRAM ? "true" : "false");
            std::fflush(stdout);
        }
        chosen_api = ultramodern::renderer::GraphicsApi::Vulkan;
        if (setup_result != ultramodern::renderer::SetupResult::Success) {
            diagnostic_log().error("rt64_setup_failed");
            app_.reset();
        } else {
            diagnostic_log().gpu(app_->device->getDescription().name);
            // Developer tools: the game's frame steps pause RT64's picture on
            // each frame (frame_step.h), as its own Pause (F4) does.
            RT64::Application* app = app_.get();
            frame_step::set_picture_pauser([app](bool pause) { app->state->debuggerInspector.paused = pause; });
            // ...and the free camera moved by a joypad, as RT64's own mouse
            // controls move it (rt64_camera_controller.cpp): its speed a fifth
            // of the scene's depth a second.
            frame_step::set_camera_driver([app](const frame_step::CameraMove& m) {
                auto& camera = app->state->debuggerInspector.camera;
                if (m.scene < 0) camera.enabled = false;
                if (m.scene > 0) {
                    // On, or the next 3D scene of the frame on screen: its view
                    // taken over from its first projection, as RT64's checkbox
                    // does for the first scene (enableFreeCamera).
                    auto& queue = *app->workloadQueue;
                    const auto& frame = queue.gameFrames[queue.curFrameIndex % queue.gameFrames.size()];
                    const std::size_t count = frame.perspectiveScenes.size();
                    if (count > 0) {
                        const std::uint32_t next = camera.enabled ? (camera.sceneIndex + 1) % count : 0;
                        const auto& scene = frame.perspectiveScenes[next];
                        if (!scene.projections.empty()) {
                            const auto& p = scene.projections.front();
                            app->state->debuggerInspector.enableFreeCamera(queue.workloads[p.workloadIndex], p.fbPairIndex, p.projectionIndex);
                            camera.sceneIndex = next;
                        }
                    }
                    std::fprintf(stdout, "{\"event\":\"free_camera_scene\",\"scene\":%u,\"scenes\":%zu,\"on\":%s}\n", camera.sceneIndex, count,
                                 camera.enabled ? "true" : "false");
                    std::fflush(stdout);
                }
                if (!camera.enabled) return false;
                const float speed = (camera.farPlane - camera.nearPlane) / 5.0f;
                if (m.right != 0.0f || m.up != 0.0f || m.forward != 0.0f)
                    app->state->cameraController.movePerspective(camera, {m.right * speed, m.up * speed, -m.forward * speed});
                if (m.yaw != 0.0f || m.pitch != 0.0f) app->state->cameraController.rotatePerspective(camera, m.yaw, m.pitch);
                return true;
            });
            std::fprintf(stdout,
                         "{\"event\":\"rt64_device_ready\",\"run_id\":\"%s\",\"api\":\"vulkan\"}\n",
                         run_id());
            std::fflush(stdout);
        }
    }

    ~Context() {
        frame_step::set_picture_pauser(nullptr);
        frame_step::set_camera_driver(nullptr);
    }

    bool valid() override { return app_ != nullptr; }
    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override {
        if (app_ == nullptr) return false;
        // These controls still need their own RT64 integration. Do not report
        // them as applied just because the supported fields were copied.
        if (old_config.hr_option != new_config.hr_option ||
            old_config.api_option != new_config.api_option ||
            old_config.msaa_option != new_config.msaa_option ||
            old_config.hpfb_option != new_config.hpfb_option ||
            old_config.developer_mode != new_config.developer_mode) return false;

        auto updated = app_->userConfig;
        if (!apply_rt64_graphics_config(new_config, updated,
                                        internal_resolution_scale)) return false;
        const bool framebuffer_change =
            updated.resolution != app_->userConfig.resolution ||
            updated.resolutionMultiplier != app_->userConfig.resolutionMultiplier ||
            updated.aspectRatio != app_->userConfig.aspectRatio ||
            updated.extAspectRatio != app_->userConfig.extAspectRatio ||
            updated.aspectTarget != app_->userConfig.aspectTarget ||
            updated.extAspectTarget != app_->userConfig.extAspectTarget ||
            updated.downsampleMultiplier != app_->userConfig.downsampleMultiplier;
        const bool rate_change =
            updated.refreshRate != app_->userConfig.refreshRate ||
            updated.refreshRateTarget != app_->userConfig.refreshRateTarget;
        if (framebuffer_change || rate_change) {
            app_->userConfig = updated;
            app_->updateUserConfig(framebuffer_change);
        }
        host_graphics_ = new_config;
        return true;
    }
    void enable_instant_present() override {
        app_->enhancementConfig.presentation.mode =
            RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
        app_->updateEnhancementConfig();
    }
    // Research: BLOCKPLANET_DL_RECT_LOG="x0,y0,x1,y1" (the 320 x 240 frame)
    // logs each texture rectangle and each textured triangle batch's texture
    // drawn in the frame's display list whose rectangle overlaps that area,
    // with the texture's address and the list address of the command, once
    // per distinct (texture, place). To find a 2D or 3D draw by where it is.
    std::uint8_t* research_rdram_ = nullptr;
    void research_rect_log(const OSTask* task) {
        static const std::array<int, 4> area = [] {
            std::array<int, 4> a{-1, -1, -1, -1};
            if (const char* v = std::getenv("BLOCKPLANET_DL_RECT_LOG")) std::sscanf(v, "%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3]);
            return a;
        }();
        if (area[0] < 0 || research_rdram_ == nullptr) return;
        const std::uint8_t* m = research_rdram_;
        auto word = [&](std::uint32_t at) { std::uint32_t v; std::memcpy(&v, m + (at & 0x007FFFFCu), 4); return v; };
        static std::set<std::tuple<std::uint32_t, int, int>> seen;
        std::uint32_t image = 0;
        int budget = 200000;
        const auto walk = [&](auto&& self, std::uint32_t at, int depth) -> void {
            for (; budget-- > 0; at += 8u) {
                const std::uint32_t w0 = word(at), w1 = word(at + 4u);
                const std::uint32_t op = w0 >> 24;
                if (op == 0xFDu) image = w1;
                if (op == 0xE4u || op == 0xE5u) {
                    const int x1 = int((w0 >> 12) & 0xFFFu) / 4, y1 = int(w0 & 0xFFFu) / 4;
                    const int x0 = int((w1 >> 12) & 0xFFFu) / 4, y0 = int(w1 & 0xFFFu) / 4;
                    if (x1 >= area[0] && x0 <= area[2] && y1 >= area[1] && y0 <= area[3] &&
                        seen.insert({image, x0 / 8, y0 / 8}).second)
                        std::fprintf(stdout, "{\"event\":\"dl_rect\",\"frame\":%llu,\"image\":\"0x%08X\",\"at\":\"0x%08X\",\"depth\":%d,\"x0\":%d,\"y0\":%d,\"x1\":%d,\"y1\":%d}\n",
                                     (unsigned long long)display_list_count.load(), image, at | 0x80000000u, depth, x0, y0, x1, y1);
                    at += 8u;  // the texture coordinates follow (two more words)
                    if (op == 0xE4u && (word(at) >> 24) == 0xB4u) at += 8u;
                }
                if (op == 0x06u && depth < 8) {
                    self(self, w1 & 0x00FFFFFFu, depth + 1);
                    if (((w0 >> 16) & 0xFFu) == 1u) return;
                }
                if (op == 0xB8u) return;
            }
        };
        walk(walk, task->t.data_ptr & 0x00FFFFFFu, 0);
        std::fflush(stdout);
    }

    // Research: BLOCKPLANET_DL_TEX_LOG=1 logs each distinct texture set-up
    // used for triangles in a frame (the image, its load, the render tile and
    // its size, the palette), once each: to find the bricks' textures and
    // rebuild their TMEM for RT64's replacement hashes.
    void research_tex_log(const OSTask* task) {
        static const bool on = std::getenv("BLOCKPLANET_DL_TEX_LOG") != nullptr;
        if (!on || research_rdram_ == nullptr) return;
        const std::uint8_t* m = research_rdram_;
        auto word = [&](std::uint32_t at) { std::uint32_t v; std::memcpy(&v, m + (at & 0x007FFFFCu), 4); return v; };
        static std::set<std::string> seen;
        std::uint32_t timg0 = 0, timg1 = 0, load0 = 0, load1 = 0, tile0 = 0, tile1 = 0, size0 = 0, size1 = 0, tlut0 = 0, tlut1 = 0, tlut_img = 0, other = 0;
        std::uint32_t load_img = 0, load_settile0 = 0, load_settile1 = 0;
        int budget = 300000;
        std::uint32_t segments[16] = {};
        auto resolve = [&](std::uint32_t a) { return (segments[(a >> 24) & 0xFu] + (a & 0x00FFFFFFu)) & 0x00FFFFFFu; };
        const auto walk = [&](auto&& self, std::uint32_t at, int depth) -> void {
            for (; budget-- > 0; at += 8u) {
                const std::uint32_t w0 = word(at), w1 = word(at + 4u);
                const std::uint32_t op = w0 >> 24;
                switch (op) {
                case 0xBC:  // G_MOVEWORD: a segment's base
                    if ((w0 & 0xFFu) == 6u) segments[((w0 >> 8) & 0xFFFFu) / 4u & 0xFu] = w1 & 0x00FFFFFFu;
                    break;
                case 0xFD: timg0 = w0, timg1 = resolve(w1) | 0x80000000u; break;
                case 0xF5:
                    if (((w1 >> 24) & 7u) == 7u) load_settile0 = w0, load_settile1 = w1;  // the load tile
                    else if (((w1 >> 24) & 7u) == 0u) tile0 = w0, tile1 = w1;
                    break;
                case 0xF3: case 0xF4: load0 = w0, load1 = w1, load_img = timg1; break;
                case 0xF0: tlut0 = w0, tlut1 = w1, tlut_img = timg1; break;
                case 0xF2: if (((w1 >> 24) & 7u) == 0u) size0 = w0, size1 = w1; break;
                case 0xBF: case 0xB1: {
                    char key[300];
                    std::snprintf(key, sizeof key, "%08X %08X|%08X %08X|%08X %08X|%08X %08X|%08X %08X|%08X %08X %08X|%08X %08X",
                                  timg0, load_img, load_settile0, load_settile1, load0, load1, tile0, tile1, size0, size1,
                                  tlut0, tlut1, tlut_img, other, 0u);
                    if (seen.insert(key).second)
                        std::fprintf(stdout, "{\"event\":\"dl_tex\",\"frame\":%llu,\"key\":\"%s\"}\n",
                                     (unsigned long long)display_list_count.load(), key);
                    break;
                }
                default: break;
                }
                if (op == 0x06u && depth < 8) {
                    self(self, resolve(w1), depth + 1);
                    if (((w0 >> 16) & 0xFFu) == 1u) return;
                }
                if (op == 0xB8u) return;
            }
        };
        walk(walk, task->t.data_ptr & 0x00FFFFFFu, 0);
        std::fflush(stdout);
    }

    // Sharp bricks (include/tetrisphere/brick_textures.h): a replacement pack
    // in the data folder, filled as the bricks are first drawn. Off with
    // BLOCKPLANET_BRICKS=original.
    bool bricks_ready_ = false;
    std::filesystem::path bricks_dir_;
    std::set<std::uint64_t> bricks_done_;
    ::tetrisphere::bricks::Style bricks_style_ = ::tetrisphere::bricks::Style::Sharp;
    std::string bricks_current_;
    // The style's pack: "original" none; "sharp" and "crafted" each their own
    // folder in the data folder, filled as the bricks are drawn.
    // BLOCKPLANET_BRICKS overrides the setting, for testing.
    void sharp_bricks_apply(std::string style) {
        if (const char* forced = std::getenv("BLOCKPLANET_BRICKS")) style = forced;
        if (style != "original" && style != "sharp" && style != "crafted") style = "crafted";
        if (style == bricks_current_) return;
        bricks_current_ = style;
        bricks_done_.clear();
        if (style == "original") {
            if (bricks_ready_) app_->textureCache->loadReplacementDirectories({});
            bricks_ready_ = false;
        } else {
            bricks_style_ = style == "crafted" ? ::tetrisphere::bricks::Style::Crafted : ::tetrisphere::bricks::Style::Sharp;
            std::error_code error;
            bricks_dir_ = ::tetrisphere::application_data_directory() / "texture-packs" / (style == "crafted" ? "crafted-bricks" : "sharp-bricks");
            std::filesystem::create_directories(bricks_dir_, error);
            const auto database = bricks_dir_ / "rt64.json";
            if (!error && !std::filesystem::exists(database)) {
                std::ofstream out(database);
                out << "{\"configuration\": {\"configurationVersion\": 3, \"hashVersion\": 5}, \"textures\": []}\n";
            }
            bricks_ready_ = !error && app_->textureCache->loadReplacementDirectory(RT64::ReplacementDirectory(bricks_dir_));
        }
        std::fprintf(stdout, "{\"event\":\"brick_style\",\"style\":\"%s\",\"ready\":%s}\n", style.c_str(), bricks_ready_ ? "true" : "false");
        std::fflush(stdout);
    }
    void sharp_bricks_init() { sharp_bricks_apply("crafted"); }
    void sharp_bricks_frame(const OSTask* task) {
        {
            std::optional<std::string> style;
            {
                std::lock_guard lock(pending_bricks_mutex);
                style = std::exchange(pending_bricks, std::nullopt);
            }
            if (style) sharp_bricks_apply(*style);
        }
        if (!bricks_ready_ || research_rdram_ == nullptr) return;
        // Every 15th frame: new brick colours come with a stage or a new
        // piece, and are sharpened within a quarter of a second. Walking every
        // frame cost 0.46 ms a frame on the test machine (Rescue 1:1).
        static unsigned frame = 0;
        if (frame++ % 15u != 0u) return;
        const std::uint8_t* m = research_rdram_;
        const auto read32 = [m](std::uint32_t at) { std::uint32_t v; std::memcpy(&v, m + (at & 0x007FFFFCu), 4); return v; };
        ::tetrisphere::bricks::find_draws(read32, task->t.data_ptr, [&](const ::tetrisphere::bricks::Draw& d) {
            RT64::LoadTile tile{};
            tile.fmt = d.tile.fmt;
            tile.siz = d.tile.siz;
            tile.line = d.tile.line;
            tile.tmem = d.tile.tmem;
            tile.palette = d.tile.palette;
            const std::uint64_t hash = RT64::TMEMHasher::hash(d.tmem.data(), tile, d.width, d.height, d.tlut, RT64::TMEMHasher::CurrentHashVersion);
            if (!bricks_done_.insert(hash).second) return;
            // The style's version is in the name, so a changed style is made
            // again rather than an older file reused (Crafted 2: square, bevelled; 3:
            // the crystal drawn as a block with circles; 4: the circles without rims; 5: outlines from the
            // palette indices, so palettes without a darker outline get one; 6: the highlight sheet left as the original;
            // 7: the crystal tile (first called rubble) drawn as a cut gem with shards; 8: in fixed colours).
            static const bool mark = std::getenv("BLOCKPLANET_BRICK_MARK") != nullptr;  // research
            const int version = bricks_style_ == ::tetrisphere::bricks::Style::Crafted ? (mark ? 99 : 8) : 1;
            char name[48];
            std::snprintf(name, sizeof name, "%016llx-v%d.dds", static_cast<unsigned long long>(hash), version);
            const auto path = bricks_dir_ / name;
            const bool made = !std::filesystem::exists(path);
            if (made) {
                const auto bytes = ::tetrisphere::bricks::replacement(d, bricks_style_);
                std::ofstream out(path, std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            const bool added = app_->textureCache->addReplacement(hash, name, RT64::ReplacementShift::Auto);
            std::fprintf(stdout, "{\"event\":\"sharp_brick\",\"hash\":\"%016llx\",\"palette\":%u,\"made\":%s,\"added\":%s}\n",
                         static_cast<unsigned long long>(hash), unsigned(d.tile.palette), made ? "true" : "false", added ? "true" : "false");
            std::fflush(stdout);
        });
    }

    void send_dl(const OSTask* task) override {
        ScopedPerfTimer frame_timer(perf_metrics().display_list);
        {
            // The walk's cost, logged once after 600 frames (sharp bricks).
            const auto start = std::chrono::steady_clock::now();
            sharp_bricks_frame(task);
            static double total_us = 0;
            static int frames = 0;
            total_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            if (bricks_ready_ && ++frames == 600)
                std::fprintf(stdout, "{\"event\":\"sharp_bricks_cost\",\"frames\":600,\"average_us\":%.1f}\n", total_us / 600.0);
        }
        research_rect_log(task);
        research_tex_log(task);
        // Only this render thread mutates RT64's configuration.
        std::optional<PresentationFilter> filter;
        std::optional<bool> texture;
        {
            std::lock_guard lock(live_filters_mutex);
            filter=std::exchange(pending_presentation,std::nullopt);
            texture=std::exchange(pending_texture,std::nullopt);
        }
        if (filter || texture) {
            if (filter) switch (*filter) {
            case PresentationFilter::Nearest: app_->userConfig.filtering=RT64::UserConfiguration::Filtering::Nearest; break;
            case PresentationFilter::Linear: app_->userConfig.filtering=RT64::UserConfiguration::Filtering::Linear; break;
            case PresentationFilter::Pixel: app_->userConfig.filtering=RT64::UserConfiguration::Filtering::AntiAliasedPixelScaling; break;
            }
            if (texture) app_->userConfig.threePointFiltering=*texture;
            app_->updateUserConfig(false);
            std::fprintf(stdout,"{\"event\":\"pc_filters_applied\",\"filter\":%d,\"three_point\":%s}\n",
                         static_cast<int>(app_->userConfig.filtering),app_->userConfig.threePointFiltering ? "true":"false");
            std::fflush(stdout);
        }
        std::int16_t scene_gate = 0;
        std::memcpy(&scene_gate, app_->core.RDRAM + (0x000E121Cu ^ 2u),
                    sizeof(scene_gate));
        const bool next_menu_path = scene_gate >= 0;
        if (!scene_seen_ || next_menu_path != menu_path_) {
            scene_seen_ = true;
            menu_path_ = next_menu_path;
            std::fprintf(stdout,
                         "{\"event\":\"guest_scene_hint\",\"run_id\":\"%s\","
                         "\"raw\":%d,\"menu_path\":%s}\n",
                         run_id(), static_cast<int>(scene_gate),
                         menu_path_ ? "true" : "false");
            std::fflush(stdout);
            if (host_graphics_.ar_option == ultramodern::renderer::AspectRatio::Manual) {
                std::fprintf(stdout,
                             "{\"event\":\"graphics_effective_aspect\","
                             "\"run_id\":\"%s\",\"mode\":\"%s\"}\n",
                             run_id(), "16:9");
                std::fflush(stdout);
            }
        }
        app_->state->rsp->reset();
        app_->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFFu,
                                       task->t.ucode_data & 0x3FFFFFFu, true);
        if (display_list_count.load() == 0) {
            const auto segment_zero = app_->state->rsp->segments[0];
            const bool extended = app_->state->extended.extendRDRAM;
            if (segment_zero != 0 || extended) {
                diagnostic_log().error("rt64_memory_mode_invalid");
                std::fprintf(stderr,
                             "{\"event\":\"fatal_rt64_memory_mode\","
                             "\"segment_zero\":\"0x%08X\",\"extended\":%s}\n",
                             segment_zero, extended ? "true" : "false");
                std::exit(70);
            }
            std::fprintf(stdout,
                         "{\"event\":\"rt64_memory_mode\","
                         "\"segment_zero\":\"0x00000000\",\"extended\":false,"
                         "\"rdram_backing_bytes\":9437184}\n");
            std::fflush(stdout);
        }
        {
            // Compare producer bytes before the rendering fixes temporarily
            // mutate RDRAM commands; the producer fingerprint is pre-patch.
            {
                const std::lock_guard<std::mutex> lock(ui_tag_mutex);
                ui_active_batch = ui_tag_queue.take(task->t.data_ptr & 0x00FFFFFFu,
                                                    task->t.data_size,
                                                    app_->core.RDRAM, 9437184);
            }
            F3DMedalAlphaPatch medal_alpha(app_->core.RDRAM,
                                           9437184, task->t.data_ptr,
                                           task->t.data_size);
            F3DFlatShadePatch flat_shade(app_->core.RDRAM, 9437184,
                                         task->t.data_ptr,
                                         task->t.data_size);
            // Research (2026-11-20, Training's flashing arrow):
            // BLOCKPLANET_ARROW_LOG=1 logs the triangle commands drawn after a
            // load from the arrow's flipbook (bricks::arrow_triangles). Blanking
            // them here did not hide the arrow (RT64 still drew it, and nothing
            // else), so the arrow is to be left out at the game's own draw.
            if (static const bool arrow_log = std::getenv("BLOCKPLANET_ARROW_LOG") != nullptr; arrow_log) {
                std::uint8_t* m = app_->core.RDRAM;
                const auto read32 = [m](std::uint32_t at) { std::uint32_t v; std::memcpy(&v, m + (at & 0x007FFFFCu), 4); return v; };
                static int arrow_logged = 0;
                const auto found = ::tetrisphere::bricks::arrow_triangles(read32, task->t.data_ptr);
                if (!found.empty() && arrow_logged++ < 3) {
                    std::fprintf(stdout, "{\"event\":\"arrow_triangles\",\"count\":%zu,\"first\":\"%08X\",\"list\":\"%08X\"}\n",
                                 found.size(), found.front(), task->t.data_ptr);
                    std::fflush(stdout);
                }
            }
            if (!ui_active_batch.spans.empty()) {
                static std::atomic<unsigned> logged{0};
                if (logged.fetch_add(1, std::memory_order_relaxed) < 8) {
                    std::fprintf(stderr,
                        "{\"event\":\"rt64_probe_ui_tag_task\","
                        "\"first\":%u,\"size\":%u,\"generation\":%llu,"
                        "\"spans\":%zu}\n",
                        task->t.data_ptr & 0x00FFFFFFu, task->t.data_size,
                        static_cast<unsigned long long>(ui_active_batch.task_generation),
                        ui_active_batch.spans.size());
                }
            }
            ui_current_tag = 0;
            frame_step::gate([this] { update_screen(); });  // developer tools: frozen, or a frame at a time
            {
                ScopedPerfTimer rt64_timer(perf_metrics().display_list_rt64);
                app_->processDisplayLists(app_->core.RDRAM,
                                          task->t.data_ptr & 0x3FFFFFFu, 0, true);
            }
            ui_active_batch = {};
            ui_current_tag = 0;
            rescue_snapshot_source_workload.store(app_->state->workloadId,
                                                  std::memory_order_release);
            if (const auto sample = medal_trace_.observe(menu_path_, flat_shade.count())) {
                std::fprintf(stdout,
                             "{\"event\":\"menu_medal_trace\",\"run_id\":\"%s\","
                             "\"sequence\":%zu,\"patched_triangles\":%zu}\n",
                             run_id(), sample->sequence, sample->patched_triangles);
                std::fflush(stdout);
            }
            static std::size_t max_reported_flat_shade = 0;
            if (flat_shade.count() > max_reported_flat_shade) {
                max_reported_flat_shade = flat_shade.count();
                std::fprintf(stdout,
                             "{\"event\":\"f3d_flat_shade_patch\",\"triangles\":%zu,\"list_size\":%u}\n",
                             flat_shade.count(),
                             static_cast<unsigned>(task->t.data_size));
            }
        }
        const auto count = ++display_list_count;
        if (count <= 3) {
            std::fprintf(stdout,
                         "{\"event\":\"rt64_display_list\",\"run_id\":\"%s\",\"sequence\":%llu,"
                         "\"ucode\":\"0x%08X\",\"data\":\"0x%08X\"}\n",
                         run_id(), static_cast<unsigned long long>(count),
                         static_cast<unsigned>(task->t.ucode),
                         static_cast<unsigned>(task->t.data_ptr));
            std::fflush(stdout);
        }
    }
    void send_dummy_workload(std::uint32_t framebuffer) override {
        app_->state->listProcessBegin();
        app_->state->rdp->setColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 320,
                                        framebuffer);
        app_->state->rdp->setOtherMode(0x382C30, 0);
        app_->state->rdp->fillRect(0, 0, 320 << 2, 240 << 2);
        app_->state->fullSync();
        app_->state->listProcessEnd();
    }
    void update_screen() override {
        {
            ScopedPerfTimer screen_timer(perf_metrics().screen_update);
            app_->updateScreen();
        }
        if (display_list_count.load() != 0) {
            const auto count = ++present_count;
            if (count <= 3) {
                std::fprintf(stdout,
                             "{\"event\":\"rt64_present\",\"run_id\":\"%s\",\"sequence\":%llu}\n",
                             run_id(), static_cast<unsigned long long>(count));
                std::fflush(stdout);
            }
        }
    }
    void shutdown() override { app_->end(); }
    std::uint32_t get_display_framerate() const override {
        return app_->presentQueue->ext.sharedResources->swapChainRate;
    }
    float get_resolution_scale() const override {
        return 1.0f;
    }

private:
    std::unique_ptr<RT64::Application> app_;
    ultramodern::renderer::GraphicsConfig host_graphics_{};
    bool scene_seen_ = false;
    bool menu_path_ = true;
    MenuMedalTrace medal_trace_{[] {
        const char* value = std::getenv("TETRISPHERE_TRACE_MENU_MEDALS");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }()};
};

} // namespace

void set_rt64_internal_resolution_scale(std::optional<double> multiplier) {
    internal_resolution_scale = multiplier;
}

void set_rt64_live_filters(std::optional<PresentationFilter> filter,std::optional<bool> three_point) {
    std::lock_guard lock(live_filters_mutex);
    if (filter) pending_presentation=filter;
    if (three_point) pending_texture=three_point;
}

namespace { std::atomic<bool> training_arrow{false}; }
void set_training_arrow(bool shown) { training_arrow.store(shown); }
bool training_arrow_shown() { return training_arrow.load(); }

void set_rt64_brick_style(const std::string& style) {
    std::lock_guard lock(pending_bricks_mutex);
    pending_bricks = style;
}

void set_rt64_run_options(const RunOptions& options) {
    run_options = options;
}

std::unique_ptr<ultramodern::renderer::RendererContext> create_rt64_context(
    std::uint8_t* rdram, ultramodern::renderer::WindowHandle window,
    bool developer_mode) {
    return std::make_unique<Context>(rdram, window, developer_mode);
}

} // namespace tetrisphere
