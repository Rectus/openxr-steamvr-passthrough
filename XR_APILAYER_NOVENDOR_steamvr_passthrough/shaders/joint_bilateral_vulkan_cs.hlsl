

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
#define LUMA_WEIGHT_CLAMP 48

// Using the default sparse 16 byte aligned arrays for a bit of performance over packing them.
cbuffer FilterKernels : register(b0)
{
    float g_lumaWeights[LUMA_WEIGHT_CLAMP];
    float g_spaceWeights[MAX_FILTER_DIST][MAX_FILTER_DIST]; // One quadrant of the symmetric filter values.
}


SamplerState g_samplerState : register(s3);
RWTexture2D<float> g_disparity: register(u1);
RWTexture2D<float> g_confidence: register(u2);
Texture2D<float> g_cameraFrame: register(t3);
RWTexture2D<float2> g_outputDisparity: register(u4);


float ReadDisparity(in int2 pos)
{
    float disparity = g_disparity.Load(pos);
    float confidence = g_confidence.Load(pos);
    
    // Restore disparity from holefill confidence buffer.
    if (confidence < 0.0)
    {
        disparity = -confidence;
    }
    return disparity;
}

float ReadDisparityConfidence(out float confidence, in int2 pos)
{
    float disparity = g_disparity.Load(pos);
    confidence = g_confidence.Load(pos);
    
    // Restore disparity from holefill confidence buffer.
    if (confidence < 0.0)
    {
        disparity = -confidence;
        confidence = 0.0;
    }  
    return disparity;
}


void CalculatePixel(inout float dispSum, inout float totalWeight, inout bool bCenterIsBackground, in int2 centerPos, in int2 offset, in float centerDisp, in float centerLuma, in bool bCenterValid, in float2 uvPos, in float2 pixelUVSize)
{
    float sampleDisp = ReadDisparity(centerPos + offset);

    bool bSampleValid = sampleDisp < g_maxDisparity && sampleDisp > g_minDisparity;

    if (!bSampleValid)
    {
        return;
    }
    
    // Disparity distance weight. Ignore small changes, but discard on discontinuities.
    bool bInDiscontinuity = bCenterValid && abs(centerDisp - sampleDisp) > g_bilateralDispCutoff;

    if (bInDiscontinuity)
    {
        // If the center pixel is on the background side of a discontinuity
        //if (abs(offset.x) < 2 && abs(offset.y) < 2 && (sampleDisp > centerDisp)) 
        if (sampleDisp > centerDisp + g_bilateralDispCutoff) 
        { 
            bCenterIsBackground = true; 
        }
        return;
    }

    float luma = g_cameraFrame.Sample(g_samplerState, uvPos + pixelUVSize * offset).x;

    // Weight of color distance
    float jointWeight = g_lumaWeights[min(int(abs(centerLuma - luma) * 255.0), LUMA_WEIGHT_CLAMP - 1)];

    // Weight of sample distance from center
    float spaceWeight = g_spaceWeights[abs(offset.y)][abs(offset.x)];

    float weight = spaceWeight * jointWeight;

    dispSum += sampleDisp * weight;
    totalWeight += weight;
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
    float2 uvPos = (float2(outPos) + float2(0.5, 0.5)) / outSize; // Sample pixel nearest to center of output disparity pixel.
    float2 centerUVPos = (float2(inPos) + float2(0.5, 0.5)) / inSize; // Use the center of the input pixel for the bilateral samples.

    // Do nothing if outside of output bounds
    if(outPos.x >= outSizeInt.x || outPos.y >= outSizeInt.y)
    {
        return;
    }

    float pixelConfidence;
    float pixelDisparity = ReadDisparityConfidence(pixelConfidence, inPos);
    
    bool bPixelValid = pixelDisparity < g_maxDisparity && pixelDisparity > g_minDisparity;
    
    if(!g_bUseInputConfidence)
    {
        pixelConfidence = bPixelValid ? 1.0: 0.0;
    }
    
    if (!bPixelValid)
    {
        pixelDisparity = g_minDisparity;
    }
    // Sample nearest neighbor pixels to detect disontinuities on the output pixel when upscaling.
    else if(outSizeInt.x > inSizeInt.x)
    {
        float2 pos = uvPos * inSize;
        int2 offset = int2(round(pos % 1.0) * float2(2.0) - float2(1.0));
        float neighborH = ReadDisparity(inPos + int2(offset.x, 0));
        float neighborV = ReadDisparity(inPos + int2(0, offset.y));

        if(abs(neighborH - pixelDisparity) > g_bilateralDispCutoff && 
            abs(neighborV - pixelDisparity) > g_bilateralDispCutoff)
        {
            pixelDisparity = clamp(min(neighborH, neighborV), g_minDisparity, g_maxDisparity);
            pixelConfidence = 0.0;
        }
    }

    float centerLuma =  g_cameraFrame.Sample(g_samplerState, uvPos).x;
    
    int radius = int(g_bilateralDistance);

    float dispSum = pixelDisparity;
    float totalWeight = g_spaceWeights[0][0];
    float numSamples = 1.0;

    bool bIsBackgroundPixel = false;

    for (int y = 0; y < min(inSizeInt.y - inPos.y, radius); y++)
    {        
        int xStart = y == 0 ? 1 : 0;

        // Limit distance to circle
        int xEnd = min(int(sqrt(radius * radius - y * y)), min(inSizeInt.x - inPos.x, radius));

        for (int x = xStart; x < xEnd; x++)
        {
            //Sample all 4 circle quadrants. 
            CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, int2(x, y), pixelDisparity, centerLuma, bPixelValid, centerUVPos, pixelUVSize);
            CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, int2(x, -y), pixelDisparity, centerLuma, bPixelValid, centerUVPos, pixelUVSize);
            CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, int2(-x, y), pixelDisparity, centerLuma, bPixelValid, centerUVPos, pixelUVSize);
            CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, int2(-x, -y), pixelDisparity, centerLuma, bPixelValid, centerUVPos, pixelUVSize);
            
            numSamples += 4;
        }
    }

    float disparity = dispSum / totalWeight;

    float confidence = (bPixelValid && !bIsBackgroundPixel) ? min(totalWeight / numSamples, pixelConfidence) : 0.0f;

    g_outputDisparity[outPos] = float2(disparity, confidence);
}
