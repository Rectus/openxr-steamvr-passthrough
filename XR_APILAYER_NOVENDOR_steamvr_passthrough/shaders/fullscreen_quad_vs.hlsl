
struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float2 uvCoords : TEXCOORD0;
};


VS_OUTPUT main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	// Single triangle from (0, 0) to (2, 2).
	output.uvCoords = float2((vertexID & 1) * 2, (vertexID >> 1) * 2);

	float posX = (output.uvCoords.x - 0.5) * 2.0;
	float posY = (output.uvCoords.y - 0.5) * 2.0;
	output.position = float4(posX, posY, 0.0, 1.0);

	output.uvCoords.y = 1 - output.uvCoords.y;

	return output;
}
