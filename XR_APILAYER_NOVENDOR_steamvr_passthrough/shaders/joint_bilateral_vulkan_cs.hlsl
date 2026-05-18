

[[vk::push_constant]] cbuffer csConstantBuffer
{
    int2 g_disparitySize;
    float g_minDisparity;
    float g_maxDisparity;
    bool g_bHoleFillLastPass;  

	float g_bilateralDispCutoff;
	uint g_bilateralDistance;
	bool g_bUseInputConfidence;
}

#define MAX_FILTER_DIST 10
#define WEIGHT_ARRAY_SIZE (MAX_FILTER_DIST * 2 + 1)

// Using the default sparse 16 byte aligned arrays for a bit of performance over packing them.
cbuffer FilterKernels : register(b0)
{
    float g_lumaWeights[256];
    float g_spaceWeights[WEIGHT_ARRAY_SIZE][WEIGHT_ARRAY_SIZE];
}


SamplerState g_samplerState : register(s3);
RWTexture2D<float> g_disparity: register(u1);
RWTexture2D<float> g_confidence: register(u2);
Texture2D<float> g_cameraFrame: register(t3);
RWTexture2D<float2> g_outputDisparity: register(u4);


bool CalculatePixel(inout float dispSum, inout float totalWeight, inout bool bCenterIsBackground, in int2 centerPos, in int2 offset, in float centerDisp, in float centerLuma, in bool bCenterValid, in float2 uvPos, in float2 pixelUVSize, in int radius)
{
    float sampleDisp = g_disparity.Load(centerPos + offset);
    float sampleConf = g_confidence.Load(centerPos + offset);

    bool bSampleValid = sampleDisp < g_maxDisparity && sampleDisp > g_minDisparity;

    // Disparity distance weight. Ignore small changes, but discard on discontinuities.
    bool bInDiscontinuity = bCenterValid && abs(centerDisp - sampleDisp) > g_bilateralDispCutoff;

    if (!bSampleValid || bInDiscontinuity)
    {
        // If the center pixel is on the background side of a discontinuity
        if (abs(offset.x) < 2 && abs(offset.y) < 2 && (!bSampleValid || sampleDisp > centerDisp)) { bCenterIsBackground = true; }
        return true;
    }


    float sampleWeight = 1.0; //bInDiscontinuity ? 0.0f : 1.0f;
            
    //if (sampleWeight <= 0 && sampleDisp > pixelDisp) { bIsBackgroundPixel = true; }

    float luma = g_cameraFrame.Sample(g_samplerState, uvPos + pixelUVSize * offset).x;

    // Weight of color distance
    float jointWeight = g_lumaWeights[min(int(abs(centerLuma - luma) * 255.0), 255)];

    // Weight of sample distance from center
    float spaceWeight = g_spaceWeights[offset.y + radius][offset.x + radius];

    float weight = spaceWeight * sampleWeight * jointWeight;
    //float weight = sampleWeight * jointWeight;

    dispSum += sampleDisp * weight;
    totalWeight += weight;

    return false;
}



[numthreads(32, 32, 1)]
void main(uint3 pos3 : SV_DispatchThreadID)
{
    float2 cameraFrameRes;
    
    
    float2 inSize;   
    int2 inSizeInt;
    g_disparity.GetDimensions(inSize.x, inSize.y);
    g_disparity.GetDimensions(inSizeInt.x, inSizeInt.y);
    
    float2 outSize;
    int2 outSizeInt;
    g_outputDisparity.GetDimensions(outSize.x, outSize.y);
    g_outputDisparity.GetDimensions(outSizeInt.x, outSizeInt.y);
    
    float2 pixelUVSize = float2(1.0 / inSize.x, 1.0 / inSize.y);

    int2 outPos = int2(pos3.xy);
    int2 inPos = (outPos * inSizeInt) / outSizeInt;
    float2 uvPos = (float2(outPos) + float2(0.5)) / outSize; // Sample pixel nearest to center of disparity pixel

    // Do nothing if outside of output bounds
    if(outPos.x >= outSizeInt.x || outPos.y >= outSizeInt.y)
    {
        return;
    }

    float pixelDisp = g_disparity.Load(inPos);
    float pixelConfidence = g_bUseInputConfidence ? abs(g_confidence.Load(inPos)) : 1.0; // Abs flips back hole fill negative conf

    float centerLuma =  g_cameraFrame.Sample(g_samplerState, uvPos).x;


    bool bPixelValid = pixelDisp < g_maxDisparity && pixelDisp > g_minDisparity;

    if (!bPixelValid)
    {
        pixelDisp = g_minDisparity;
    }

    int radius = int(g_bilateralDistance);

    float dispSum = pixelDisp;
    float totalWeight = g_spaceWeights[radius][radius];

    bool bIsBackgroundPixel = false;

    int discUp = min(inSizeInt.y - inPos.y, radius);
    int discDown = min(inPos.y, radius);
    int discLeft = min(inPos.x, radius);
    int discRight = min(inSizeInt.x - inPos.x, radius);

    for (int y = 0; y < min(inSizeInt.y - inPos.y, radius); y++)
    {
        
        int xStart = y == 0 ? 1 : 0;

        // Limit distance to circle
        int xEnd = min(int(sqrt(radius * radius - y * y)), min(inSizeInt.x - inPos.x, radius));

        // 
        for (int x = xStart; x < xEnd; x++)
        {
            //Sample all 4 circle quadrants. Limit sampling in same direction if a discontinuity is found.
            if(discUp >= y && discRight >= x)
            {
                if(CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, int2(x, y), pixelDisp, centerLuma, bPixelValid, uvPos, pixelUVSize, radius))
                {
                    discUp = y + 1;
                    discRight = y + 1;
                }
            }

            if(discDown >= y && discRight >= x)
            {
                if(CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, int2(x, -y), pixelDisp, centerLuma, bPixelValid, uvPos, pixelUVSize, radius))
                {
                    discDown = y + 1;
                    discRight = y + 1;
                }
            }

            if(discUp >= y && discLeft >= x)
            {
                if(CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, int2(-x, y), pixelDisp, centerLuma, bPixelValid, uvPos, pixelUVSize, radius))
                {
                    discUp = y + 1;
                    discLeft = y + 1;
                }
            }

            if(discDown >= y && discLeft >= x)
            {
                if(CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, int2(-x, -y), pixelDisp, centerLuma, bPixelValid, uvPos, pixelUVSize, radius))
                {
                    discDown = y + 1;
                    discLeft = y + 1;
                }
            }
        }
    }

    float disparity = (totalWeight > 0) ? (dispSum / totalWeight) : 0.0;

    float numSamples = min(inSizeInt.y - inPos.y, radius) - min(inPos.y, radius) + 
        min(inSizeInt.x - inPos.x, radius) - min(inPos.x, radius);

    float confidence = (bPixelValid && !bIsBackgroundPixel) ? min(totalWeight / numSamples * 1.0, pixelConfidence) : 0.0f;

    g_outputDisparity[outPos] = float2(disparity, confidence);
}
