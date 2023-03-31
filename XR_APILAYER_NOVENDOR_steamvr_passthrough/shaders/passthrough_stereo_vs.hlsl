

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 clipSpaceCoords : TEXCOORD0;
	float3 screenCoords : TEXCOORD1;
	float projectionValidity : TEXCOORD2;
};

#ifdef VULKAN

[[vk::push_constant]]
cbuffer vsConstantBuffer
{
	float4x4 g_cameraProjectionToWorld;
    //float4x4 g_worldToCameraProjection;
    float4x4 g_worldToHMDProjection;
	float4 g_uvBounds;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
    uint g_viewIndex;
};

#else

cbuffer vsConstantBuffer : register(b0)
{
    float4x4 g_cameraProjectionToWorld;
    float4x4 g_worldToCameraProjection;
    float4x4 g_worldToHMDProjection;
	float4 g_uvBounds;
	float3 g_hmdViewWorldPos;
	float g_projectionDistance;
	float g_floorHeightOffset;
    uint g_viewIndex;
};
#endif

cbuffer vsPassConstantBuffer : register(b1)
{
    float4x4 g_disparityViewToWorldLeft;
    float4x4 g_disparityViewToWorldRight;
	float4x4 g_disparityToDepth;
	uint2 g_disparityTextureSize;
	float g_disparityDownscaleFactor;
    float g_cutoutFactor;
    float g_cutoutOffset;
    int g_disparityFilterWidth;
};

SamplerState g_samplerState : register(s0);
Texture2D<float2> g_disparityTexture : register(t0);



float gaussian(float2 value)
{
    return exp(-0.5 * dot(value /= (g_disparityFilterWidth * 2 * 0.25), value)) / 
        (2 * 3.14 * pow(g_disparityFilterWidth * 2 * 0.25, 2));
}


VS_OUTPUT main(float3 inPosition : POSITION, uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
    float2 disparityUVs = inPosition.xy * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
    uint3 uvPos = uint3(round(disparityUVs * g_disparityTextureSize), 0);
    
    //float2 dispConf = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs, 0);
    float2 dispConf = g_disparityTexture.Load(uvPos);
    float disparity = dispConf.x;
    float confidence = dispConf.y;

    output.projectionValidity = 1;
    //output.projectionValidity = confidence;

	// Disparity at the max projection distance
    float minDisparity = g_disparityToDepth[2][3] /
    (g_projectionDistance * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[3][2]);
    
    float maxDisparity = 0.0465;
    
    float defaultDisparity = g_disparityToDepth[2][3] /
    (min(2.0, g_projectionDistance) * 2048.0 * g_disparityDownscaleFactor * g_disparityToDepth[3][2]);

    if (disparity > maxDisparity || disparity < minDisparity)
    {
        // Hack that causes some artifacting. Ideally patch any holes or discard and render behind instead.
        disparity = defaultDisparity;
        //disparity = 0.002;
        //disparity = clamp(disparity, minDisparity, 0.0465);
        output.projectionValidity = -10000.0;
    }
    else if (inPosition.x < 0.01 || inPosition.x > 0.99 ||
        inPosition.y < 0.01 || inPosition.y > 0.99)
    {
        disparity = defaultDisparity;
        output.projectionValidity = -10000.0;
    }
    else if (confidence < 0.5)
    {
        
        float maxNeighborDisp = 0;
                
        float dispU = g_disparityTexture.Load(uvPos + uint3(0, -1, 0)).x;
        float dispD = g_disparityTexture.Load(uvPos + uint3(0, 1, 0)).x;

        if (g_viewIndex == 1)
        {
            float dispR = g_disparityTexture.Load(uvPos + uint3(1, 0, 0)).x;
            float dispUR = g_disparityTexture.Load(uvPos + uint3(1, -1, 0)).x;
            float dispDR = g_disparityTexture.Load(uvPos + uint3(1, 1, 0)).x;
        
            maxNeighborDisp = max(dispR, max(dispU, max(dispD, max(dispUR, dispDR))));
            //maxNeighborDisp = max(dispR, max(dispUR, dispDR));      
        }
        else
        {
            float dispL = g_disparityTexture.Load(uvPos + uint3(-1, 0, 0)).x;
            float dispUL = g_disparityTexture.Load(uvPos + uint3(-1, -1, 0)).x;
            float dispDL = g_disparityTexture.Load(uvPos + uint3(-1, 1, 0)).x;
        
            maxNeighborDisp = max(dispL, max(dispU, max(dispD, max(dispUL, dispDL))));
            //maxNeighborDisp = max(dispL, max(dispUL, dispDL));
        }
        output.projectionValidity = 1 + g_cutoutOffset + 1000 * g_cutoutFactor * (disparity - maxNeighborDisp);
        
        
        if (g_disparityFilterWidth > 1)
        {
            float totalWeight = 0;
            float outDisp = 0;
            float disparityDelta = 0;
        
            for (int x = -g_disparityFilterWidth; x < g_disparityFilterWidth; x++)
            {
                for (int y = -g_disparityFilterWidth; y < g_disparityFilterWidth; y++)
                {
                    float sampleDisp = g_disparityTexture.Load(uvPos + uint3(x, y, 0)).x;
                    
                //if (sampleDisp > disparity)
                //{
                    float weight = gaussian(float2(x, y));
                    totalWeight += weight;
                    outDisp += clamp(sampleDisp * weight, 0, maxDisparity);
                    
                    //float delta = (disparity - sampleDisp) * weight;
                    //if (((g_viewIndex == 0 && x < 0) || (g_viewIndex == 1 && x > 0)) && delta < disparityDelta)
                    //{
                    //    disparityDelta = delta;
                    //}
                //}
                
 
                }
            }
        
        //if (totalWeight > 0)
        //{
            //output.projectionValidity = 1 + g_cutoutOffset + 1000 * g_cutoutFactor * disparityDelta;
            disparity = outDisp / totalWeight;
        //}
        }
    }

    float2 texturePos = inPosition.xy * g_disparityTextureSize * float2(0.5, 1) * g_disparityDownscaleFactor;

	// Convert to int16 range with 4 bit fixed decimal: 65536 / 2 / 16
	disparity *= 2048.0 * g_disparityDownscaleFactor;
    float4 viewSpaceCoords = mul(g_disparityToDepth, float4(texturePos, disparity, 1.0));
	viewSpaceCoords.y = 1 - viewSpaceCoords.y;
	viewSpaceCoords.z *= -1;
	viewSpaceCoords /= viewSpaceCoords.w;
    viewSpaceCoords.z = sign(viewSpaceCoords.z) * min(abs(viewSpaceCoords.z), g_projectionDistance);

    float4 worldSpacePoint = 
		mul((g_uvBounds.x < 0.5) ? g_disparityViewToWorldLeft : g_disparityViewToWorldRight, viewSpaceCoords);
    
    
    // Clamp positions to floor height
    if ((worldSpacePoint.y / worldSpacePoint.w) < g_floorHeightOffset)
    {
        float3 ray = normalize(worldSpacePoint.xyz / worldSpacePoint.w - g_hmdViewWorldPos);
        
        float num = (dot(float3(0, 1, 0), float3(0, g_floorHeightOffset, 0)) - dot(float3(0, 1, 0), g_hmdViewWorldPos));
        float denom = dot(float3(0, 1, 0), ray);

        if (denom < 0)
        {
            worldSpacePoint = float4(g_hmdViewWorldPos + ray * num / denom, 1);
        }
    }
    
	
#ifndef VULKAN
    float4 outCoords = mul(g_worldToCameraProjection, worldSpacePoint);
	output.clipSpaceCoords = outCoords.xyw;
#endif
	
    output.position = mul(g_worldToHMDProjection, worldSpacePoint);
	output.screenCoords = output.position.xyw;
    
    
#ifdef VULKAN
	output.position.y *= -1.0;
#endif

	return output;
}
