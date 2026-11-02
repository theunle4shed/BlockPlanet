// BlockPlanet menu layer: RmlUi geometry (docs/UI.md).

[[vk::push_constant]]
struct Constants {
    float4x4 transform;      // RmlUi's transform (identity when none)
    float2 translation;      // added to positions before the transform
    float2 pixel_to_clip;    // 2 / width, 2 / height
    float flip_y;            // 1 for Vulkan, -1 for Direct3D
} constants;

struct VSInput {
    float2 position : POSITION;
    float4 colour : COLOR;     // premultiplied alpha
    float2 uv : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 colour : COLOR;
    float2 uv : TEXCOORD;
};

VSOutput VSMain(VSInput input) {
    const float4 p = mul(constants.transform, float4(input.position + constants.translation, 0.0, 1.0));
    VSOutput output;
    // Pixels (origin top left) to clip space, keeping w for projective transforms.
    output.position = float4(p.x * constants.pixel_to_clip.x - p.w,
                             (p.y * constants.pixel_to_clip.y - p.w) * constants.flip_y,
                             0.0, p.w);
    output.colour = input.colour;
    output.uv = input.uv;
    return output;
}
