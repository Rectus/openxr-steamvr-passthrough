
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"
#include "fullscreen_util.hlsl"


SamplerState g_samplerState : register(s0);
Texture2D<float4> g_cameraFrameTexture : register(t0);
Texture2D<float2> g_fisheyeCorrectionTexture : register(t1);
Texture2D<float4> g_cameraValidation : register(t2);
Texture2D<float> g_depthMap : register(t3);

struct PS_Output
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

PS_Output main(VS_OUTPUT input)
{
    float2 screenUvs = Remap(input.screenPos.xy, float2(-1.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0));
    
    float depth = g_depthMap.Sample(g_samplerState, screenUvs);
    float4 cameraValidation = g_cameraValidation.Sample(g_samplerState, screenUvs);
    
    float projectionConfidence = cameraValidation.x;

    
    bool bIsDiscontinuityFiltered = false;
    
    [branch]
    if(g_depthContourStrength > 0)
    {
        depth = sobel_discontinuity_adjust(g_depthMap, g_samplerState, depth, screenUvs, bIsDiscontinuityFiltered);
    }
    
    float4 clipSpacePos = float4(input.screenPos.xy, depth, 1.0);
    
    float4 worldProjectionPos = mul(g_HMDProjectionToWorld, clipSpacePos);
    
    float4 cameraClipSpacePos = mul((g_cameraViewIndex == 0) ? g_worldToCameraFrameProjectionLeft : g_worldToCameraFrameProjectionRight, worldProjectionPos);
    
    if (g_bUseDepthCutoffRange)
    {
        float4 worldHMDEyePos = mul(g_HMDProjectionToWorld, float4(0, 0, g_bHasReversedDepth ? 1 : 0, 1));
        float depthMeters = distance(worldProjectionPos.xyz / worldProjectionPos.w, worldHMDEyePos.xyz / worldHMDEyePos.w);
        clip(depthMeters - g_depthCutoffRange.x);
        clip(g_depthCutoffRange.y - depthMeters);
    }  

	// Convert from homogenous clip space coordinates to 0-1.
	float2 outUvs = Remap(cameraClipSpacePos.xy / cameraClipSpacePos.w, -1.0, 1.0, 0.0, 1.0);

    if (g_bClampCameraFrame)
    {
        clip(cameraClipSpacePos.z);
        clip(outUvs);
        clip(1 - outUvs);
    }
    
    float2 correction = 0;
    
    if (g_bUseFisheyeCorrection)
    {
        // Remap and clamp to frame UV bounds.
        outUvs = Remap(outUvs, 0.0, 1.0, g_uvBounds.xy, g_uvBounds.zw);
        outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
        
        correction = g_fisheyeCorrectionTexture.Sample(g_samplerState, outUvs);
        outUvs += correction;
    }
    else
    {
        // Remap and clamp to frame UV bounds.
        outUvs = Remap(outUvs, float2(0.0, 1.0), float2(1.0, 0.0), g_uvBounds.xy, g_uvBounds.zw);
        outUvs = clamp(outUvs, g_uvBounds.xy, g_uvBounds.zw);
    }
      
    float3 rgbColor = g_cameraFrameTexture.Sample(g_samplerState, outUvs).xyz;
    
    if (g_sharpness != 0.0)
    {
        float3 textureSize;
        g_cameraFrameTexture.GetDimensions(0, textureSize.x, textureSize.y, textureSize.z);
        rgbColor *= 1 + g_sharpness * 4;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(-1, 0) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(1, 0) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, -1) / textureSize.xy).xyz * g_sharpness;
        rgbColor -= g_cameraFrameTexture.Sample(g_samplerState, outUvs + float2(0, 1) / textureSize.xy).xyz * g_sharpness;
    }
    
	if (g_bDoColorAdjustment)
	{
		// Using CIELAB D65 to match the EXT_FB_passthrough adjustments.
		float3 labColor = LinearRGBtoLAB_D65(rgbColor);
		float LPrime = clamp((labColor.x - 50.0) * g_contrast + 50.0, 0.0, 100.0);
		float LBis = clamp(LPrime + g_brightness, 0.0, 100.0);
		float2 ab = labColor.yz * g_saturation;

		rgbColor = LABtoLinearRGB_D65(float3(LBis, ab.xy));
	}

    if (g_bDebugDepth)
    {
        float debugDepth = g_bHasReversedDepth ? depth : pow(abs(depth), g_depthRange.y * 5.0);
        rgbColor = float3(debugDepth, debugDepth, debugDepth);
    }
    if (g_debugOverlay == 1) // Confidence
    {
        if (projectionConfidence < 0.0)
        {
            rgbColor.r += 0.5;
        }
        else if (projectionConfidence > 0.0)
        {
            rgbColor.g += projectionConfidence * 0.25;
        }
        else
        {
            rgbColor.b += 0.25;
        }
    }
    else if (g_debugOverlay == 5) // Discontinuity filtering
    {
        if (bIsDiscontinuityFiltered)
        {
            rgbColor.b += 1.0;
        }
    }
    
    PS_Output output;
    
    rgbColor = g_bPremultiplyAlpha ? rgbColor * g_opacity : rgbColor;
	
    output.color = float4(rgbColor, g_opacity);
    output.depth = depth;
    
    return output;
}
