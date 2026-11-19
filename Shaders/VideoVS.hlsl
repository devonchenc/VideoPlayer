struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float2 positions[] =
    {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2(3.0, -1.0)
    };

    float2 uvs[] =
    {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.texcoord = uvs[vertexId];

    return output;
}