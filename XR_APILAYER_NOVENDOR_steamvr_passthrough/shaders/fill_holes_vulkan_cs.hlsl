

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

RWTexture2D<float> g_disparity: register(u1);
RWTexture2D<float> g_confidence: register(u2);
RWTexture2D<float2> g_outputDisparity: register(u4);


[numthreads(32, 32, 1)]
void main(uint3 pos3 : SV_DispatchThreadID)
{
    int2 pos = pos3.xy;
    
    // Don't process outside image boundary.
    if(any(pos >= g_disparitySize))
    {
        return;
    }
    
    if (g_bHoleFillLastPass)
    {
        int2 outSize;        
        g_outputDisparity.GetDimensions(outSize.x, outSize.y);
        
        int2 inPos = (pos * g_disparitySize) / outSize;

        float disparity = g_disparity.Load(inPos);
        float confidence = g_confidence.Load(inPos);

        if (confidence <= 0.0 && (disparity <= g_minDisparity || disparity >= g_maxDisparity))
        {
            // Return the stored disparity value fro mthe confidence buffer if one exists.
            if (confidence < 0.5 && confidence >= 0.0)
            {
                disparity = g_minDisparity;
                confidence = -1.0;
            }
            else
            {
                disparity = -confidence;
                confidence = 0.0;
            }
        }

        g_outputDisparity[pos] = float2(disparity, confidence);
        return;
    }
    
    uint frameWidth = g_disparitySize.x / 2;
    
    float disparity = g_disparity.Load(pos);
    float confidence = g_confidence.Load(pos);
    
    // Filter large invalid disparites from the right edge of the image.
    int pixelsFromRightEdge = (pos.x > frameWidth) ? frameWidth * 2 - pos.x : frameWidth - pos.x;
    float maxDisp = lerp(g_minDisparity, g_maxDisparity, 
        min(1, pixelsFromRightEdge / ((g_maxDisparity - g_minDisparity) * 2048.0 / 16.0)));
    
    if (pixelsFromRightEdge < 2)
    {
        return;   
    }
 
    // Sample neighbors and temporarily store furthest disparity in the confidence buffer as a negative value.
    if (confidence <= 0.0 && (disparity <= g_minDisparity || disparity >= maxDisp))
    {
        float dispU = g_disparity.Load((uint2)(pos + int2(0, -1)));
        float dispD = g_disparity.Load((uint2)(pos + int2(0, 1))); 
        float dispL = g_disparity.Load((uint2)(pos + int2(-1, 0))); 
        float dispR = g_disparity.Load((uint2)(pos + int2(1, 0))); 
        
        float confU = g_confidence.Load((uint2)(pos + int2(0, -1)));
        float confD = g_confidence.Load((uint2)(pos + int2(0, 1))); 
        float confL = g_confidence.Load((uint2)(pos + int2(-1, 0))); 
        float confR = g_confidence.Load((uint2)(pos + int2(1, 0))); 
        
        if (confidence == 0.0)
        {      
            if (dispU > g_minDisparity && dispU < maxDisp)
            {
                confidence = dispU * -1.0;
            }
            else if (dispD > g_minDisparity && dispD < maxDisp)
            {
                confidence = dispD * -1.0;
            }
            else if (dispL > g_minDisparity && dispL < maxDisp)
            {
                confidence = dispL * -1.0;
            }
            else if (dispR > g_minDisparity && dispR < maxDisp)
            {
                confidence = dispR * -1.0;
            }
            else if (confU < 0.0)
            {
                confidence = confU;
            }
            else if (confD < 0.0)
            {
                confidence = confD;
            }
            else if (confL < 0.0)
            {
                confidence = confL;
            }
            else if (confR < 0.0)
            {
                confidence = confR;
            }
        }

        if (confU < 0.0 && confU > confidence)
        {
            confidence = confU;
        }
        if (confD < 0.0 && confD > confidence)
        {
            confidence = confD;
        }
        if (confL < 0.0 && confL > confidence)
        {
            confidence = confL;
        }
        if (confR < 0.0 && confR > confidence)
        {
            confidence = confR;
        }

        g_confidence[pos] = confidence;
    }
}
