

#version 450
//#extension GL_EXT_scalar_block_layout : enable

#ifndef VULKAN // To make the VS GLSL extension shut up
#extension GL_KHR_vulkan_glsl: enable
#endif


layout(constant_id = 0) const float g_minDisparity= 0.0;
layout(constant_id = 1) const float g_maxDisparity = 1.0;
layout(constant_id = 2) const bool g_bUseInputConfidence = true;
layout(constant_id = 3) const float g_bilateralDispCutoff = 0.01;
layout(constant_id = 4) const uint g_bilateralDistance = 5;

#define MAX_FILTER_DIST 10
#define LUMA_WEIGHT_CLAMP 48

// Using the default sparse 16 byte aligned arrays for a bit of performance over packing them.
layout( binding = 0, std140 ) uniform FilterKernels
{
    float lumaWeights[LUMA_WEIGHT_CLAMP];
    float spaceWeights[MAX_FILTER_DIST][MAX_FILTER_DIST]; // One quadrant of the symmetric filter values.
} g_kernels;

layout( binding = 1, r16_snorm ) uniform readonly image2D g_disparity;
layout( binding = 2, r16_snorm ) uniform image2D g_confidence;
layout( binding = 3 ) uniform sampler2D g_cameraFrame;
layout( binding = 4, rg16_snorm ) uniform image2D g_outputDisparity;



float ReadDisparity(in ivec2 pos)
{
    float disparity = imageLoad(g_disparity, pos).x;
    float confidence = imageLoad(g_confidence, pos).x;
    
    // Restore disparity from holefill confidence buffer.
    if (confidence < 0.0)
    {
        disparity = -confidence;
    }

    // Interpret invalid pixels as being far away to avoid filtering issues
    if(disparity >= g_maxDisparity)
    {
        disparity = g_minDisparity;
    }
    return disparity;
}

float ReadDisparityConfidence(out float confidence, in ivec2 pos)
{
    float disparity = imageLoad(g_disparity, pos).x;
    confidence = imageLoad(g_confidence, pos).x;
    
    // Restore disparity from holefill confidence buffer.
    if (confidence < 0.0)
    {
        disparity = -confidence;
        confidence = -1.0;
    }

    // Interpret invalid pixels as being far away to avoid filtering issues
    if(disparity >= g_maxDisparity)
    {
        disparity = g_minDisparity;
    }
    return disparity;
}


void CalculatePixel(inout float dispSum, inout float totalWeight, inout bool bCenterIsBackground, in ivec2 centerPos, in ivec2 offset, in float centerDisp, in float centerLuma, in bool bCenterValid, in vec2 uvPos, in vec2 pixelUVSize)
{
    float confidence;
    float sampleDisp = ReadDisparityConfidence(confidence, centerPos + offset);

    // Discard smaples that are invalid, or from hole filling when we have better data
    if (sampleDisp <= g_minDisparity || (bCenterValid && confidence < 0.0))
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

    float luma = texture(g_cameraFrame, uvPos + pixelUVSize * offset).x;

    // Weight of color distance
    float jointWeight = g_kernels.lumaWeights[min(int(abs(centerLuma - luma) * 255.0), LUMA_WEIGHT_CLAMP - 1)];

    // Weight of sample distance from center
    float spaceWeight = g_kernels.spaceWeights[abs(offset.y)][abs(offset.x)];

    float weight = spaceWeight * jointWeight;

    dispSum += sampleDisp * weight;
    totalWeight += weight;
}



layout( local_size_x = 32, local_size_y = 32, local_size_z = 1 ) in;

void main()
{
    vec2 inSize = imageSize(g_disparity);
    ivec2 inSizeInt = ivec2(inSize);
    
    vec2 outSize = imageSize(g_outputDisparity);
    ivec2 outSizeInt = ivec2(outSize);
    
    vec2 pixelUVSize = vec2(1.0 / inSize.x, 1.0 / inSize.y);

    ivec2 outPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 inPos = (outPos * inSizeInt) / outSizeInt;
    vec2 uvPos = (vec2(outPos) + vec2(0.5, 0.5)) / outSize; // Sample pixel nearest to center of output disparity pixel.
    vec2 centerUVPos = (vec2(inPos) + vec2(0.5, 0.5)) / inSize; // Use the center of the input pixel for the bilateral samples.

    // Do nothing if outside of output bounds
    if(outPos.x >= outSizeInt.x || outPos.y >= outSizeInt.y)
    {
        return;
    }

    float pixelConfidence;
    float pixelDisparity = ReadDisparityConfidence(pixelConfidence, inPos);
    
    bool bPixelValid =  pixelDisparity > g_minDisparity;
    
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
        vec2 pos = uvPos * inSize;
        ivec2 offset = ivec2(round(mod(pos, 1.0)) * vec2(2.0) - vec2(1.0));
        float neighborH = ReadDisparity(inPos + ivec2(offset.x, 0));
        float neighborV = ReadDisparity(inPos + ivec2(0, offset.y));

        if(neighborH > g_minDisparity && neighborV > g_minDisparity &&
            abs(neighborH - pixelDisparity) > g_bilateralDispCutoff && 
            abs(neighborV - pixelDisparity) > g_bilateralDispCutoff)
        {
            pixelDisparity = min(neighborH, neighborV);
            pixelConfidence = 0.0;
        }
    }

    float centerLuma = texture(g_cameraFrame, uvPos).x;
    
    int radius = int(g_bilateralDistance);

    float dispSum = pixelDisparity;
    float totalWeight = g_kernels.spaceWeights[0][0];
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
            CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, ivec2(x, y), pixelDisparity, centerLuma, bPixelValid, centerUVPos, pixelUVSize);
            CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, ivec2(x, -y), pixelDisparity, centerLuma, bPixelValid, centerUVPos, pixelUVSize);
            CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, ivec2(-x, y), pixelDisparity, centerLuma, bPixelValid, centerUVPos, pixelUVSize);
            CalculatePixel(dispSum, totalWeight, bIsBackgroundPixel, inPos, ivec2(-x, -y), pixelDisparity, centerLuma, bPixelValid, centerUVPos, pixelUVSize);
            
            numSamples += 4;
        }
    }

    float disparity = dispSum / totalWeight;

   float confidence = (bPixelValid && !bIsBackgroundPixel) ? min(totalWeight / numSamples, pixelConfidence) : 0.0f;

    imageStore(g_outputDisparity, outPos, vec4(disparity, confidence, 0, 0));
}
