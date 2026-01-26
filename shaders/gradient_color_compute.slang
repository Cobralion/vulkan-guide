[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

struct PushConstants
{
    float4 data0;
    float4 data1;
    float4 data2;
    float4 data3;
};

[[vk::push_constant]]
PushConstants pc;

[shader("compute")]
[numthreads(32, 32, 1)]
void main(uint3 threadId: SV_DispatchThreadID)
{

    uint2 texelCoord = uint2(threadId.xy);
    uint width, height;
    image.GetDimensions(width, height);

    float4 topColor = pc.data0;
    float4 bottomColor = pc.data1;

    if (texelCoord.x < width && texelCoord.y < height)
    {
        image[texelCoord] = lerp(topColor, bottomColor, float(texelCoord.y) / float(height));
    }
}