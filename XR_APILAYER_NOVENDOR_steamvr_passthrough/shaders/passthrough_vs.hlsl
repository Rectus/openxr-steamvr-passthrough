
struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 uvCoords : TEXCOORD0;
	float2 originalUVCoords : TEXCOORD1;
};

cbuffer vsConstantBuffer : register(b0)
{
	float4x4 g_cameraUVProjectionFar;
	float4x4 g_cameraUVProjectionNear;
};

VS_OUTPUT main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	// Single triangle from (0, 0) to (2, 2).
	float2 vertPos = float2((vertexID & 1) * 2, (vertexID >> 1) * 2);

	float posX = (vertPos.x - 0.5) * 2.0;
	float posY = (vertPos.y - 0.5) * 2.0;
	output.position = float4(posX, posY, 0.0, 1.0);

	// The UV transformation is non-linear in 2D space, 
	// so the transformation has to either be done in the pixel shader,
	// or pass the UVs as homogenous coordinates as shown here.
	output.uvCoords = mul(g_cameraUVProjectionFar, float4(posX, posY, 1.0, 1.0)).xyz;

	output.originalUVCoords = float2(vertPos.x, 1 - vertPos.y);

	return output;
}
