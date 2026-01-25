// Image2D in GLSL (rgba16f) maps to RWTexture2D<float4> in Slang
[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

[shader("compute")]
[numthreads(32, 32, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint width, height;
    image.GetDimensions(width, height);
    int2 size = int2(width, height);

    uint halfWidth = width / 2;
    uint halfHeight = height / 2;

    float4 color = float4(0.0, 0.0, 0.0, 1.0);

    if (threadId.x >= halfWidth && threadId.y >= halfHeight
    && threadId.x < halfWidth + 100 && threadId.y < halfHeight + 100) {
        color = float4(1.0, 0.0, 0.0, 1.0); // Red square in the center
    }

    image[threadId.xy] = color;
}
