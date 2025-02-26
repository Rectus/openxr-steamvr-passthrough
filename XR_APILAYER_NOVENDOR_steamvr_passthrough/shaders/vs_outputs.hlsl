
struct VS_OUTPUT
{
	float4 position : SV_POSITION;
    float4 screenPos : TEXCOORD0;
	float2 projectionConfidence : TEXCOORD1;
    float2 cameraBlendConfidence : TEXCOORD2;
    float4 cameraReprojectedPos : TEXCOORD3;
    float4 crossCameraReprojectedPos : TEXCOORD4;
    float4 prevHMDFrameScreenPos : TEXCOORD5;
    float4 prevCameraFrameScreenPos : TEXCOORD6;
    float3 prevCameraFrameVelocity : TEXCOORD7;
    float2 cameraDepth : TEXCOORD8;
};