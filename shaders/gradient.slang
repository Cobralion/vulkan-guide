// Image2D in GLSL (rgba16f) maps to RWTexture2D<float4> in Slang
[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

[shader("compute")]
[numthreads(32, 32, 1)]
void main(uint3 threadId : SV_DispatchThreadID, 
          uint3 localId  : SV_GroupThreadID)
{
    // SV_DispatchThreadID is the equivalent of gl_GlobalInvocationID
    int2 texelCoord = int2(threadId.xy);
    
    // Get dimensions of the texture
    uint width, height;
    image.GetDimensions(width, height);
    int2 size = int2(width, height);

    // Boundary check
    if (texelCoord.x < size.x && texelCoord.y < size.y)
    {
        float4 color = float4(1.0, 1.0, 0.0, 1.0); // Default Yellow

        // SV_GroupThreadID is the equivalent of gl_LocalInvocationID
        if (localId.x != 0 && localId.y != 0)
        {
            color.x = float(texelCoord.x) / float(size.x);
            color.y = float(texelCoord.y) / float(size.y);
        }
        else
        {
            color = float4(0.0, 0.0, 0.0, 1.0); // Black borders for workgroups
        }

        // imageStore equivalent in Slang/HLSL syntax
        image[texelCoord] = color;
    }
}
