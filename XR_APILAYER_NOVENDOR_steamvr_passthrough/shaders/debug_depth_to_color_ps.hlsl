
#include "common_ps.hlsl"
#include "vs_outputs.hlsl"
#include "util.hlsl"

Texture2D<float> g_inputTexture : register(t0);

float4 main(VS_OUTPUT input) : SV_TARGET
{
    float2 screenUvs = Remap(input.screenPos.xy, float2(-1.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0));
    
    float3 textureSize;
    g_inputTexture.GetDimensions(0, textureSize.x, textureSize.y, textureSize.z);
    
    float depth = g_inputTexture.Load(int3(floor(screenUvs * textureSize.xy), 0));
	
    return float4(depth, depth, depth, 1);
}
