struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float2 position = float2((vertexId << 1) & 2, vertexId & 2);
    output.texcoord = position;
    output.position = float4(position.x * 2.0 - 1.0, 1.0 - position.y * 2.0, 0.0, 1.0);

    return output;
}