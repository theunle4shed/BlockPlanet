#include "ui_renderer.h"

#include "grid_texture.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>

#include "stb_image.h"  // implementation compiled by RT64

#include "UiPS.hlsl.spirv.h"
#include "UiVS.hlsl.spirv.h"
#ifdef _WIN32
#include "UiPS.hlsl.dxil.h"
#include "UiVS.hlsl.dxil.h"
#endif

#include <algorithm>
#include <cstring>

namespace blockplanet::ui {
namespace {

using namespace plume;

// Mirrors the shader's push constants (UiVS.hlsl).
struct PushConstants {
    float transform[16];
    float translation[2];
    float pixel_to_clip[2];
    float flip_y;
    float padding[3];
};
static_assert(sizeof(PushConstants) == 96);

// How long a released resource (a texture, its upload buffer, geometry) is
// kept for frames still in flight (RT64 renders up to three ahead): 16
// frames and at least a second. Counted in frames alone it was too short on
// a slow driver (4 frames), and with the box art on, the frame count runs
// twice a present (the grid's under layer and the menus each begin one),
// which halved it; an upload buffer freed while its copy was still queued
// is the likely cause of a renderer crash or hang as the box art started
// (2026-11-11, llvmpipe).
constexpr std::uint64_t retire_frames = 16;
constexpr std::chrono::milliseconds retire_time{1000};
// Row pitch alignment for texture uploads (Direct3D needs 256 bytes).
constexpr std::uint32_t row_alignment_pixels = 64;

std::array<float, 16> identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

} // namespace

Renderer::Renderer(plume::RenderInterface* render_interface, plume::RenderDevice* device) : device_(device) {
    transform_ = identity();
    const auto format = render_interface->getCapabilities().shaderFormat;
    switch (format) {
    case RenderShaderFormat::SPIRV:
        vertex_shader_ = device_->createShader(UiVSBlobSPIRV, sizeof(UiVSBlobSPIRV), "VSMain", format);
        pixel_shader_ = device_->createShader(UiPSBlobSPIRV, sizeof(UiPSBlobSPIRV), "PSMain", format);
        break;
#ifdef _WIN32
    case RenderShaderFormat::DXIL:
        vertex_shader_ = device_->createShader(UiVSBlobDXIL, sizeof(UiVSBlobDXIL), "VSMain", format);
        pixel_shader_ = device_->createShader(UiPSBlobDXIL, sizeof(UiPSBlobDXIL), "PSMain", format);
        flip_y_ = true;
        break;
#endif
    default:
        return;  // no menus on this backend
    }

    RenderSamplerDesc sampler;
    sampler.minFilter = RenderFilter::LINEAR;
    sampler.magFilter = RenderFilter::LINEAR;
    sampler.addressU = RenderTextureAddressMode::CLAMP;
    sampler.addressV = RenderTextureAddressMode::CLAMP;
    sampler.addressW = RenderTextureAddressMode::CLAMP;
    sampler_ = device_->createSampler(sampler);

    const RenderDescriptorRange ranges[2] = {
        RenderDescriptorRange(RenderDescriptorRangeType::TEXTURE, 0, 1),
        RenderDescriptorRange(RenderDescriptorRangeType::SAMPLER, 1, 1)};
    const RenderDescriptorSetDesc set(ranges, 2);
    RenderPushConstantRange push;
    push.size = sizeof(PushConstants);
    push.stageFlags = RenderShaderStageFlag::VERTEX;
    RenderPipelineLayoutDesc layout;
    layout.descriptorSetDescs = &set;
    layout.descriptorSetDescsCount = 1;
    layout.pushConstantRanges = &push;
    layout.pushConstantRangesCount = 1;
    layout_ = device_->createPipelineLayout(layout);

    // RmlUi vertices: position (2 floats), premultiplied colour (4 bytes), uv.
    const RenderInputSlot slot(0, sizeof(Rml::Vertex));
    const RenderInputElement elements[3] = {
        RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32_FLOAT, 0, offsetof(Rml::Vertex, position)),
        RenderInputElement("COLOR", 0, 1, RenderFormat::R8G8B8A8_UNORM, 0, offsetof(Rml::Vertex, colour)),
        RenderInputElement("TEXCOORD", 0, 2, RenderFormat::R32G32_FLOAT, 0, offsetof(Rml::Vertex, tex_coord))};
    RenderBlendDesc blend;
    blend.blendEnabled = true;
    blend.srcBlend = RenderBlend::ONE;  // premultiplied alpha
    blend.dstBlend = RenderBlend::INV_SRC_ALPHA;
    blend.blendOp = RenderBlendOperation::ADD;
    blend.srcBlendAlpha = RenderBlend::ONE;
    blend.dstBlendAlpha = RenderBlend::INV_SRC_ALPHA;
    blend.blendOpAlpha = RenderBlendOperation::ADD;
    RenderGraphicsPipelineDesc pipeline;
    pipeline.pipelineLayout = layout_.get();
    pipeline.vertexShader = vertex_shader_.get();
    pipeline.pixelShader = pixel_shader_.get();
    pipeline.inputSlots = &slot;
    pipeline.inputSlotsCount = 1;
    pipeline.inputElements = elements;
    pipeline.inputElementsCount = 3;
    pipeline.renderTargetFormat[0] = RenderFormat::B8G8R8A8_UNORM;  // RT64's swap chain
    pipeline.renderTargetBlend[0] = blend;
    pipeline.renderTargetCount = 1;
    pipeline.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
    pipeline_ = device_->createGraphicsPipeline(pipeline);
}

Renderer::~Renderer() = default;

void Renderer::begin_frame(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    ++frame_;
    collect_retired();
    list_ = list;
    framebuffer_ = framebuffer;
    width_ = framebuffer->getWidth();
    height_ = framebuffer->getHeight();
    transform_ = identity();
    scissor_enabled_ = false;
    if (!white_) {
        const std::uint8_t pixel[4] = {255, 255, 255, 255};
        const auto handle = create_texture(pixel, 1, 1);
        white_.reset(reinterpret_cast<Texture*>(handle));
    }
}

void Renderer::end_frame() {
    list_ = nullptr;
    framebuffer_ = nullptr;
}

Rml::CompiledGeometryHandle Renderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                      Rml::Span<const int> indices) {
    if (!pipeline_ || vertices.empty() || indices.empty()) return 0;
    auto geometry = std::make_unique<Geometry>();
    geometry->vertex_bytes = static_cast<std::uint32_t>(vertices.size() * sizeof(Rml::Vertex));
    geometry->index_count = static_cast<std::uint32_t>(indices.size());
    // Host-visible buffers: menu geometry is small and changes rarely.
    geometry->vertices = device_->createBuffer(RenderBufferDesc::VertexBuffer(geometry->vertex_bytes, RenderHeapType::UPLOAD));
    geometry->indices = device_->createBuffer(
        RenderBufferDesc::IndexBuffer(geometry->index_count * sizeof(int), RenderHeapType::UPLOAD));
    std::memcpy(geometry->vertices->map(), vertices.data(), geometry->vertex_bytes);
    geometry->vertices->unmap();
    std::memcpy(geometry->indices->map(), indices.data(), geometry->index_count * sizeof(int));
    geometry->indices->unmap();
    return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
}

void Renderer::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                              Rml::TextureHandle texture_handle) {
    if (list_ == nullptr || handle == 0 || !pipeline_) return;
    const auto* geometry = reinterpret_cast<const Geometry*>(handle);
    const auto* texture = texture_handle != 0 ? reinterpret_cast<const Texture*>(texture_handle) : white_.get();

    list_->setFramebuffer(framebuffer_);
    list_->setViewports(RenderViewport(0.0f, 0.0f, float(width_), float(height_)));
    apply_scissor();
    list_->setGraphicsPipelineLayout(layout_.get());
    list_->setPipeline(pipeline_.get());
    list_->setGraphicsDescriptorSet(texture->descriptors.get(), 0);
    PushConstants constants{};
    std::memcpy(constants.transform, transform_.data(), sizeof(constants.transform));
    constants.translation[0] = translation.x;
    constants.translation[1] = translation.y;
    constants.pixel_to_clip[0] = 2.0f / float(width_);
    constants.pixel_to_clip[1] = 2.0f / float(height_);
    constants.flip_y = flip_y_ ? -1.0f : 1.0f;
    list_->setGraphicsPushConstants(0, &constants);
    const RenderVertexBufferView vertex_view(geometry->vertices.get(), geometry->vertex_bytes);
    const RenderInputSlot slot(0, sizeof(Rml::Vertex));
    list_->setVertexBuffers(0, &vertex_view, 1, &slot);
    const RenderIndexBufferView index_view(geometry->indices.get(), geometry->index_count * sizeof(int),
                                           RenderFormat::R32_UINT);
    list_->setIndexBuffer(&index_view);
    list_->drawIndexedInstanced(geometry->index_count, 1, 0, 0, 0);
}

void Renderer::ReleaseGeometry(Rml::CompiledGeometryHandle handle) {
    if (handle == 0) return;
    retired_.push_back({frame_, std::chrono::steady_clock::now(), std::unique_ptr<Geometry>(reinterpret_cast<Geometry*>(handle)), nullptr, nullptr});
}

Rml::TextureHandle Renderer::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
    // The menu grid's tiles (grid_texture.h): made in memory, not a file.
    if (source == "blockplanet-grid-tile") {
        const auto& atlas = grid_tile_atlas();
        if (atlas.rgba.empty()) return 0;
        dimensions = {atlas.width, atlas.height};
        return create_texture(atlas.rgba.data(), atlas.width, atlas.height);
    }
    // Read through RmlUi's file interface, decode with stb_image.
    Rml::FileInterface* files = Rml::GetFileInterface();
    Rml::FileHandle file = files->Open(source);
    // RmlUi drops the leading '/' of an absolute path (files BlockPlanet makes
    // in its data directory, such as the title's lines): try it with one.
    if (!file && !source.empty() && source[0] != '/') file = files->Open("/" + source);
    if (!file) return 0;
    const std::size_t size = files->Length(file);
    std::vector<std::uint8_t> bytes(size);
    files->Read(bytes.data(), size, file);
    files->Close(file);
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (pixels == nullptr) return 0;
    // RmlUi expects premultiplied alpha.
    for (int i = 0; i < width * height; ++i) {
        const unsigned a = pixels[i * 4 + 3];
        for (int c = 0; c < 3; ++c) pixels[i * 4 + c] = static_cast<stbi_uc>((pixels[i * 4 + c] * a + 127) / 255);
    }
    dimensions = {width, height};
    const auto handle = create_texture(pixels, width, height);
    stbi_image_free(pixels);
    return handle;
}

Rml::TextureHandle Renderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) {
    return create_texture(source.data(), dimensions.x, dimensions.y);
}

void Renderer::ReleaseTexture(Rml::TextureHandle handle) {
    if (handle == 0) return;
    retired_.push_back({frame_, std::chrono::steady_clock::now(), nullptr, std::unique_ptr<Texture>(reinterpret_cast<Texture*>(handle)), nullptr});
}

Rml::TextureHandle Renderer::create_texture(const std::uint8_t* rgba, int width, int height) {
    if (list_ == nullptr || !pipeline_ || width <= 0 || height <= 0) return 0;
    auto texture = std::make_unique<Texture>();
    texture->texture = device_->createTexture(
        RenderTextureDesc::Texture2D(std::uint32_t(width), std::uint32_t(height), 1, RenderFormat::R8G8B8A8_UNORM));

    const std::uint32_t row_pixels = (std::uint32_t(width) + row_alignment_pixels - 1) / row_alignment_pixels * row_alignment_pixels;
    const std::uint32_t row_bytes = row_pixels * 4;
    auto staging = device_->createBuffer(RenderBufferDesc::UploadBuffer(std::uint64_t(row_bytes) * std::uint32_t(height)));
    auto* mapped = static_cast<std::uint8_t*>(staging->map());
    for (int y = 0; y < height; ++y) std::memcpy(mapped + std::size_t(y) * row_bytes, rgba + std::size_t(y) * width * 4, std::size_t(width) * 4);
    staging->unmap();

    // The copy ends the current render pass; the next draw starts it again.
    list_->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture->texture.get(), RenderTextureLayout::COPY_DEST));
    list_->copyTextureRegion(
        RenderTextureCopyLocation::Subresource(texture->texture.get(), 0, 0),
        RenderTextureCopyLocation::PlacedFootprint(staging.get(), RenderFormat::R8G8B8A8_UNORM, std::uint32_t(width),
                                                   std::uint32_t(height), 1, row_pixels, 0),
        0, 0, 0, nullptr);
    list_->barriers(RenderBarrierStage::GRAPHICS,
                    RenderTextureBarrier(texture->texture.get(), RenderTextureLayout::SHADER_READ));

    const RenderDescriptorRange ranges[2] = {
        RenderDescriptorRange(RenderDescriptorRangeType::TEXTURE, 0, 1),
        RenderDescriptorRange(RenderDescriptorRangeType::SAMPLER, 1, 1)};
    texture->descriptors = device_->createDescriptorSet(RenderDescriptorSetDesc(ranges, 2));
    texture->descriptors->setTexture(0, texture->texture.get(), RenderTextureLayout::SHADER_READ);
    texture->descriptors->setSampler(1, sampler_.get());
    // The staging buffer must outlive the copy.
    retired_.push_back({frame_, std::chrono::steady_clock::now(), nullptr, nullptr, std::move(staging)});
    return reinterpret_cast<Rml::TextureHandle>(texture.release());
}

void Renderer::EnableScissorRegion(bool enable) { scissor_enabled_ = enable; }

void Renderer::SetScissorRegion(Rml::Rectanglei region) { scissor_ = region; }

void Renderer::SetTransform(const Rml::Matrix4f* transform) {
    if (transform == nullptr) {
        transform_ = identity();
        return;
    }
    std::memcpy(transform_.data(), transform->data(), sizeof(float) * 16);
}

void Renderer::apply_scissor() {
    if (!scissor_enabled_) {
        list_->setScissors(RenderRect(0, 0, std::int32_t(width_), std::int32_t(height_)));
        return;
    }
    const int left = std::clamp(scissor_.Left(), 0, int(width_));
    const int top = std::clamp(scissor_.Top(), 0, int(height_));
    const int right = std::clamp(scissor_.Right(), left, int(width_));
    const int bottom = std::clamp(scissor_.Bottom(), top, int(height_));
    list_->setScissors(RenderRect(left, top, right, bottom));
}

void Renderer::collect_retired() {
    const auto now = std::chrono::steady_clock::now();
    while (!retired_.empty() && retired_.front().frame + retire_frames <= frame_ && now - retired_.front().at >= retire_time)
        retired_.pop_front();
}

} // namespace blockplanet::ui
