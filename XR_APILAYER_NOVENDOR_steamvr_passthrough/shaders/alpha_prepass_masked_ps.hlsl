
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"


#ifdef VULKAN

SamplerState g_samplerState : register(s7);
Texture2DArray g_texture : register(t7);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t8);

#else

SamplerState g_samplerState : register(s0);
Texture2DArray g_texture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);
Texture2D<float4> g_cameraValidation : register(t2);
Texture2D<float> g_depthMap : register(t3);
Texture2D<float> g_crossDepthMap : register(t4);

#endif


[earlydepthstencil]
float main(VS_OUTPUT input) : SV_TARGET
{	
	float4 color;
	
    bool bInvertOutput = g_bMaskedInvert;

	if (g_bMaskedUseCamera)
	{
		float2 outUvs;
        
#ifndef VULKAN
		if(g_bUseFullscreenQuad) // Get camera coordinates directly
        {
			float2 screenUvs = Remap(input.screenPos.xy, float2(-1.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0));
    
			float depth = g_depthMap.Sample(g_samplerState, screenUvs);
            bool selectMainCamera = true;
            
			if(g_doCutout)
            {  
                float4 cameraValidation = g_cameraValidation.Sample(g_samplerState, screenUvs);
                float2 cameraBlendValidity = cameraValidation.zw;
				
				bool selectMainCamera = cameraBlendValidity.x >= cameraBlendValidity.y;
                
                if(!selectMainCamera)
                {
                    depth = g_crossDepthMap.Sample(g_samplerState, screenUvs);
                }

            }
            
            float4 clipSpacePos = float4(input.screenPos.xy, depth, 1.0);  
            float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);
            float4 cameraClipSpacePos = mul((g_cameraViewIndex == 0) == selectMainCamera ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);
            
	        outUvs = Remap(cameraClipSpacePos.xy / cameraClipSpacePos.w, -1.0, 1.0, 0.0, 1.0);
            
            if(selectMainCamera)
            {
                outUvs = Remap(outUvs, 0.0, 1.0, g_uvBounds.xy, g_uvBounds.zw);
                outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
            }
            else
            {
                outUvs = Remap(outUvs, 0.0, 1.0, g_crossUVBounds.xy, g_crossUVBounds.zw);
                outUvs = clamp(outUvs, g_crossUVBounds.xy, g_crossUVBounds.zw);
            }
        }
		else  // Get camera coordinates from vertex shader
#endif
        {
            outUvs = Remap(input.cameraReprojectedPos.xy / input.cameraReprojectedPos.w, -1.0, 1.0, 0.0, 1.0);
            outUvs = Remap(outUvs, 0.0, 1.0, g_uvBounds.xy, g_uvBounds.zw);
            outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
        }

        if (g_bUseFisheyeCorrection)
        {
            float2 correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, outUvs);
            outUvs += correction;
        }
        else
        {
            outUvs.y = 1 - outUvs.y;
        }
		
		color = g_texture.Sample(g_samplerState, float3(outUvs.xy, 0));
        bInvertOutput = !bInvertOutput;
    }
	else
	{
        float2 outUvs = Remap(input.screenPos.xy / input.screenPos.w, float2(-1.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0));

        color = g_texture.Sample(g_samplerState, 
			float3((outUvs * (g_uvPrepassBounds.zw - g_uvPrepassBounds.xy) + g_uvPrepassBounds.xy), float(g_arrayIndex)));
    }

	float3 difference = LinearRGBtoLAB_D65(color.xyz) - LinearRGBtoLAB_D65(g_maskedKey);

    float2 distChromaSqr = float2(pow(difference.y, 2), pow(difference.z, 2));
	float fracChromaSqr = pow(g_maskedFracChroma, 2);

	float distChroma = smoothstep(fracChromaSqr, fracChromaSqr + pow(g_maskedSmooth, 2), (distChromaSqr.x + distChromaSqr.y));
	float distLuma = smoothstep(g_maskedFracLuma, g_maskedFracLuma + g_maskedSmooth, abs(difference.x));
	
    float outAlpha = bInvertOutput ? 1.0 - max(distChroma, distLuma) : max(distChroma, distLuma);
	
    return outAlpha;

}
