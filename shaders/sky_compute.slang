// Starfield Compute Shader

// Binding the output image (u-register for RW access)
[[vk::binding(0, 0)]]
RWTexture2D<float4> outputImage;

// Deterministic pseudo-random noise
float Noise2d(float2 x)
{
    float xhash = cos(x.x * 37.0);
    float yhash = cos(x.y * 57.0);
    return frac(415.92653 * (xhash + yhash));
}

// Generates sparse star points based on a threshold
float NoisyStarField(float2 vSamplePos, float fThreshhold)
{
    float StarVal = Noise2d(vSamplePos);
    if (StarVal >= fThreshhold)
        StarVal = pow((StarVal - fThreshhold) / (1.0 - fThreshhold), 6.0);
    else
        StarVal = 0.0;
    return StarVal;
}

// Bilinear interpolation for a stabilized star field
float StableStarField(float2 vSamplePos, float fThreshhold)
{
    float2 fractPos = frac(vSamplePos);
    float2 floorSample = floor(vSamplePos);    
    
    float v1 = NoisyStarField(floorSample, fThreshhold);
    float v2 = NoisyStarField(floorSample + float2(0.0, 1.0), fThreshhold);
    float v3 = NoisyStarField(floorSample + float2(1.0, 0.0), fThreshhold);
    float v4 = NoisyStarField(floorSample + float2(1.0, 1.0), fThreshhold);

    return v1 * (1.0 - fractPos.x) * (1.0 - fractPos.y)
         + v2 * (1.0 - fractPos.x) * fractPos.y
         + v3 * fractPos.x * (1.0 - fractPos.y)
         + v4 * fractPos.x * fractPos.y;
}

void mainImage(out float4 fragColor, float2 fragCoord, float2 iResolution)
{
    // Gradient sky background
    float3 vColor = float3(0.1, 0.2, 0.4) * (fragCoord.y / iResolution.y);

    // Threshold [0.99, 0.9999] for density control
    float StarFieldThreshhold = 0.97;

    // Displacement for crawling motion (simulating time increment of 1.0)
    float xRate = 0.2;
    float yRate = -0.06;
    float2 vSamplePos = fragCoord + float2(xRate, yRate);
    
    float StarVal = StableStarField(vSamplePos, StarFieldThreshhold);
    vColor += float3(StarVal);
    
    fragColor = float4(vColor, 1.0);
}

[shader("compute")]
[numthreads(32, 32, 1)]
void main(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint2 texelCoord = dispatchThreadID.xy;
    uint2 size;
    outputImage.GetDimensions(size.x, size.y);

    if (texelCoord.x < size.x && texelCoord.y < size.y)
    {
        float4 color;
        mainImage(color, (float2)texelCoord, (float2)size);

        outputImage[texelCoord] = color;
    }
}
