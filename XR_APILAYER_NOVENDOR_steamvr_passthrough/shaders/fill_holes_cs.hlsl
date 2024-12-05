
cbuffer csConstantBuffer : register(b0)
{
    uint g_disparityFrameWidth;
    bool g_bHoleFillLastPass;
    float g_minDisparity;
    float g_maxDisparity;
}

RWTexture2D<float2> g_disparityTexture: register(u0);

[numthreads(16, 16, 1)]
void main(uint3 pos : SV_DispatchThreadID)
{
    float2 dispConf = g_disparityTexture.Load(pos.xy);
    
    // Filter large invalid disparites from the right edge of the image.
    int pixelsFromRightEdge = (pos.x > g_disparityFrameWidth) ? g_disparityFrameWidth * 2 - pos.x : g_disparityFrameWidth - pos.x;
    float maxDisp = lerp(g_minDisparity, g_maxDisparity, min(1, pixelsFromRightEdge / ((g_maxDisparity - g_minDisparity) * 2048.0)));
    
    if (pixelsFromRightEdge < 2)
    {
        return;   
    }
 
    // Sample neighbors and temporarily store furthest disparity in the confidence map as a negative value.
    if (dispConf.y <= 0.0 && (dispConf.x <= g_minDisparity || dispConf.x >= maxDisp))
    {
        float2 dispConfU = g_disparityTexture.Load(pos.xy + uint2(0, -1));
        float2 dispConfD = g_disparityTexture.Load(pos.xy + uint2(0, 1));
        float2 dispConfL = g_disparityTexture.Load(pos.xy + uint2(-1, 0));
        float2 dispConfR = g_disparityTexture.Load(pos.xy + uint2(1, 0));
        
        if (dispConf.y == 0.0)
        {      
            if (dispConfU.x > g_minDisparity && dispConfU.x < maxDisp)
            {
                dispConf.y = dispConfU.x * -1.0;
            }
            else if (dispConfD.x > g_minDisparity && dispConfD.x < maxDisp)
            {
                dispConf.y = dispConfD.x * -1.0;
            }
            else if (dispConfL.x > g_minDisparity && dispConfL.x < maxDisp)
            {
                dispConf.y = dispConfL.x * -1.0;
            }
            else if (dispConfR.x > g_minDisparity && dispConfR.x < maxDisp)
            {
                dispConf.y = dispConfR.x * -1.0;
            }
            else if (dispConfU.y < 0.0)
            {
                dispConf.y = dispConfU.y;
            }
            else if (dispConfD.y < 0.0)
            {
                dispConf.y = dispConfD.y;
            }
            else if (dispConfL.y < 0.0)
            {
                dispConf.y = dispConfL.y;
            }
            else if (dispConfR.y < 0.0)
            {
                dispConf.y = dispConfR.y;
            }
        }

        if (dispConfU.y < 0.0 && dispConfU.y > dispConf.y)
        {
            dispConf.y = dispConfU.y;
        }
        if (dispConfD.y < 0.0 && dispConfD.y > dispConf.y)
        {
            dispConf.y = dispConfD.y;
        }
        if (dispConfL.y < 0.0 && dispConfL.y > dispConf.y)
        {
            dispConf.y = dispConfL.y;
        }
        if (dispConfR.y < 0.0 && dispConfR.y > dispConf.y)
        {
            dispConf.y = dispConfR.y;
        }

        // Return the stored disparity value if one exists.
        if (g_bHoleFillLastPass)
        {
            if (dispConf.y < 0.5 && dispConf.y >= 0.0)
            {
                dispConf.x = g_minDisparity;
                dispConf.y = -1.0;

            }
            else
            {
                dispConf.x = -dispConf.y;
                dispConf.y = 0.0;
            }
        }

        g_disparityTexture[pos.xy] = dispConf;
    }
    else if (g_bHoleFillLastPass)
    {
        dispConf.y = 0.4;
        g_disparityTexture[pos.xy] = dispConf;
    }
}