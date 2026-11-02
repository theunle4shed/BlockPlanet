// BlockPlanet menu layer: RmlUi geometry (docs/UI.md). Untextured geometry
// uses a 1 x 1 white texture.

[[vk::binding(0, 0)]] Texture2D<float4> uiTexture : register(t0);
[[vk::binding(1, 0)]] SamplerState uiSampler : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float4 colour : COLOR;
    float2 uv : TEXCOORD;
};

float4 PSMain(PSInput input) : SV_TARGET {
    return input.colour * uiTexture.Sample(uiSampler, input.uv);
}
