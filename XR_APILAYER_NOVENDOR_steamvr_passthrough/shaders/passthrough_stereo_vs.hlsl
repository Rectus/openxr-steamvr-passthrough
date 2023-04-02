

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
    bool g_bProjectBorders;
    bool g_bFindDiscontinuities;
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
    

    // Project the border vertices to the edges of the screen to cover any gaps from reprojection.
    if (g_bProjectBorders && inPosition.z == 1)
    {
        output.position = float4(inPosition.xy * float2(2, -2) + float2(-1, 1), 1, 1);
        output.screenCoords = output.position.xyw;
#ifndef VULKAN
        float4 worldPos = mul(g_cameraProjectionToWorld, float4(inPosition.xy * 2 - 1, 1, 1));
        output.clipSpaceCoords = mul(g_worldToCameraProjection, worldPos).xyw;
#endif
        output.projectionValidity = -1;
        
#ifdef VULKAN
	output.position.y *= -1.0;
#endif      
        return output;
    }
    
    
    float2 disparityUVs = inPosition.xy * (g_uvBounds.zw - g_uvBounds.xy) + g_uvBounds.xy;
    uint3 uvPos = uint3(round(disparityUVs * g_disparityTextureSize), 0);
    
    //float2 dispConf = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs, 0);
    float2 dispConf = g_disparityTexture.Load(uvPos);
    float disparity = dispConf.x;
    float confidence = dispConf.y;

    output.projectionValidity = 1;

    
    
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
        output.projectionValidity = -10000.0;
    }
    
    if (inPosition.x < 0.01 || inPosition.x > 0.99 ||
        inPosition.y < 0.01 || inPosition.y > 0.99)
    {
        disparity = minDisparity;
        output.projectionValidity = -10000.0;
    }
    else if (confidence < 0.5)
    {
        
        // Sample neighboring pixels using clamped Sobel filter, and cut out any areas with discontinuities.
        if (g_bFindDiscontinuities)
        {                      
            float2 fac = 0.5 / g_disparityTextureSize;
            
            float dispU = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(0, -1) * fac, 0).x;
            float dispD = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(0, 1) * fac, 0).x;
            float dispL = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(-1, 0) * fac, 0).x;
            float dispR = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(1, 0) * fac, 0).x;
            
            float dispUL = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(-1, -1) * fac, 0).x;
            float dispDL = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(-1, 1) * fac, 0).x;
            float dispUR = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(1, -1) * fac, 0).x;
            float dispDR = g_disparityTexture.SampleLevel(g_samplerState, disparityUVs + float2(1, 1) * fac, 0).x;
            
            float filterX = (g_viewIndex == 0) ? max(0, dispUL + dispL * 2 + dispDL - dispUR - dispR * 2 - dispDR) :
                max(0, -dispUL - dispL * 2 - dispDL + dispUR + dispR * 2 + dispDR);
            
            float filterY = dispUL + dispU * 2 + dispUR - dispDL - dispD * 2 - dispDR;
            
            float filter = sqrt(pow(filterX, 2) + pow(filterY, 2));          

            output.projectionValidity = 1 + g_cutoutOffset - 100 * g_cutoutFactor * filter;
        }
        
        // Filter any uncertain areas with a gaussian blur.
        if (g_disparityFilterWidth > 0)
        {
            float totalWeight = 0;
            float outDisp = 0;
            float disparityDelta = 0;
        
            for (int x = -g_disparityFilterWidth; x <= g_disparityFilterWidth; x++)
            {
                for (int y = -g_disparityFilterWidth; y <= g_disparityFilterWidth; y++)
                {
                    float sampleDisp = g_disparityTexture.Load(uvPos + uint3(x, y, 0)).x;
                    float weight = gaussian(float2(x, y));
                    totalWeight += weight;
                    outDisp += clamp(sampleDisp, minDisparity, maxDisparity) * weight;
                }
            }

        disparity = outDisp / totalWeight;
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
