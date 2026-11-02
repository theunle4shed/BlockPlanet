#pragma once

// Draws RmlUi through RT64's graphics layer (plume), for BlockPlanet's menu
// layer. Written for BlockPlanet; see docs/UI.md.
//
// All calls happen on RT64's presentation thread, inside its draw hook:
// begin_frame, then RmlUi's Context::Render (which calls the RenderInterface
// functions below), then end_frame.

#include <RmlUi/Core/RenderInterface.h>

#include "plume_render_interface.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

namespace blockplanet::ui {

class Renderer final : public Rml::RenderInterface {
public:
    Renderer(plume::RenderInterface* render_interface, plume::RenderDevice* device);
    ~Renderer() override;

    void begin_frame(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer);
    void end_frame();

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void SetTransform(const Rml::Matrix4f* transform) override;

private:
    struct Geometry {
        std::unique_ptr<plume::RenderBuffer> vertices;
        std::unique_ptr<plume::RenderBuffer> indices;
        std::uint32_t vertex_bytes = 0;
        std::uint32_t index_count = 0;
    };
    struct Texture {
        std::unique_ptr<plume::RenderTexture> texture;
        std::unique_ptr<plume::RenderDescriptorSet> descriptors;
    };
    // Resources RmlUi has released, kept until frames still in flight are done.
    struct Retired {
        std::uint64_t frame;
        std::unique_ptr<Geometry> geometry;
        std::unique_ptr<Texture> texture;
        std::unique_ptr<plume::RenderBuffer> buffer;
    };

    Rml::TextureHandle create_texture(const std::uint8_t* rgba, int width, int height);
    void apply_scissor();
    void collect_retired();

    plume::RenderDevice* device_;
    bool flip_y_ = false;
    std::unique_ptr<plume::RenderPipelineLayout> layout_;
    std::unique_ptr<plume::RenderPipeline> pipeline_;
    std::unique_ptr<plume::RenderShader> vertex_shader_;
    std::unique_ptr<plume::RenderShader> pixel_shader_;
    std::unique_ptr<plume::RenderSampler> sampler_;
    std::unique_ptr<Texture> white_;

    plume::RenderCommandList* list_ = nullptr;
    plume::RenderFramebuffer* framebuffer_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool scissor_enabled_ = false;
    Rml::Rectanglei scissor_{};
    std::array<float, 16> transform_{};
    std::uint64_t frame_ = 0;
    std::deque<Retired> retired_;
};

} // namespace blockplanet::ui
