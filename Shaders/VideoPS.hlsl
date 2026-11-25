struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 PSMain(VertexShaderOutput input) : SV_TARGET
{
    return g_texture.Sample(g_sampler, input.texcoord);
}
