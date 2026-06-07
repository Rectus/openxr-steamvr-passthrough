
#version 450

#ifndef VULKAN // To make the VS GLSL extension shut up
#extension GL_KHR_vulkan_glsl: enable
#endif

layout(push_constant, std140 ) uniform csConstantBuffer
{
    bool bHoleFillLastPass;  

} g_push;

layout(constant_id = 0) const float g_minDisparity= 0.0;
layout(constant_id = 1) const float g_maxDisparity = 1.0;
layout(constant_id = 2) const bool g_bUseInputConfidence = true;


layout( binding = 1, r16_snorm ) uniform readonly image2D g_disparity;
layout( binding = 2, r16_snorm ) uniform image2D g_confidence;
layout( binding = 4, rg16_snorm ) uniform image2D g_outputDisparity;


layout( local_size_x = 32, local_size_y = 32, local_size_z = 1 ) in;

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    ivec2 inSize = imageSize(g_disparity);
    
    // Don't process outside image boundary.
    if(pos.x >= inSize.x || pos.y >= inSize.y)
    {
        return;
    }
    
    if (g_push.bHoleFillLastPass)
    {
        ivec2 outSize = imageSize(g_outputDisparity);
        
        ivec2 inPos = (pos * inSize) / outSize;

        float disparity = imageLoad(g_disparity, inPos).x;
        float confidence = imageLoad(g_confidence, inPos).x;

        if (confidence <= 0.0 && (disparity <= g_minDisparity || disparity >= g_maxDisparity))
        {
            // Return the stored disparity value from the confidence buffer if one exists.
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
        else if(!g_bUseInputConfidence) // Set confidence to 1 for areas that had valid disparity values
        {
            confidence = 1.0;
        }

        imageStore(g_outputDisparity, pos, vec4(disparity, confidence, 0, 0));
        return;
    }
    
    int frameWidth = inSize.x / 2;
    
    float disparity = imageLoad(g_disparity, pos).x;
    float confidence = imageLoad(g_confidence, pos).x;
    
    // Filter large invalid disparites from the right edge of the image.
    int pixelsFromRightEdge = (pos.x > frameWidth) ? frameWidth * 2 - pos.x : frameWidth - pos.x;
    float maxDisp = mix(g_minDisparity, g_maxDisparity, 
        min(1, pixelsFromRightEdge / ((g_maxDisparity - g_minDisparity) * 2048.0 / 16.0)));
    
    if (pixelsFromRightEdge < 2)
    {
        return;   
    }
 
    // Sample neighbors and temporarily store furthest disparity in the confidence buffer as a negative value.
    if (confidence <= 0.0 && (disparity <= g_minDisparity || disparity >= maxDisp))
    {
        float dispU = imageLoad(g_disparity, pos + ivec2(0, -1)).x;
        float dispD = imageLoad(g_disparity, pos + ivec2(0, 1)).x; 
        float dispL = imageLoad(g_disparity, pos + ivec2(-1, 0)).x; 
        float dispR = imageLoad(g_disparity, pos + ivec2(1, 0)).x; 
        
        float confU = imageLoad(g_confidence, pos + ivec2(0, -1)).x;
        float confD = imageLoad(g_confidence, pos + ivec2(0, 1)).x; 
        float confL = imageLoad(g_confidence, pos + ivec2(-1, 0)).x; 
        float confR = imageLoad(g_confidence, pos + ivec2(1, 0)).x; 
        
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

        imageStore(g_confidence, pos, vec4(confidence, 0, 0, 0));
    }
}
