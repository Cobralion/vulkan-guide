[shader("vertex")]
void main(
    uint vertexID: SV_VertexID,
    out float3 outColor: COLOR0,
    out float4 position : SV_Position
)
{
    const float3 positions[3] = {
        float3(1.f, 1.f, 0.0f),
        float3(-1.f, 1.f, 0.0f),
        float3(0.f, -1.f, 0.0f)
    };

    const float3 colors[3] = {
        float3(1.0f, 0.0f, 0.0f), // red
        float3(0.0f, 1.0f, 0.0f), // green
        float3(0.0f, 0.0f, 1.0f)  // blue
    };

    position = float4(positions[vertexID], 1.0f);
    outColor = colors[vertexID];
}
