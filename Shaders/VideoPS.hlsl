Texture2D<float> g_textureY : register(t0);
Texture2D<float> g_textureU : register(t1);
Texture2D<float> g_textureV : register(t2);

SamplerState g_sampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 PSMain(VSOutput input) : SV_TARGET
{
    float y = g_textureY.Sample(g_sampler, input.texcoord);
    float u = g_textureU.Sample(g_sampler, input.texcoord) - 0.5;
    float v = g_textureV.Sample(g_sampler, input.texcoord) - 0.5;

    float3 rgb;
    rgb.r = y + 1.402 * v;
    rgb.g = y - 0.344136 * u - 0.714136 * v;
    rgb.b = y + 1.772 * u;

    return float4(saturate(rgb), 1.0);
}