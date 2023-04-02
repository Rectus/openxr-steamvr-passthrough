#pragma once


struct VertexFormatBasic
{
	VertexFormatBasic() 
	{
		VertexFormatBasic(0, 0, 0);
	}

	VertexFormatBasic(float x, float y, float z)
	{
		position[0] = x;
		position[1] = y;
		position[2] = z;
	}

	float position[3];
};

struct MeshTriangle
{
	MeshTriangle()
		: a(0)
		, b(0)
		, c(0)
	{}

	MeshTriangle(uint32_t inA, uint32_t inB, uint32_t inC)
		: a(inA)
		, b(inB)
		, c(inC)
	{}

	uint32_t a;
	uint32_t b;
	uint32_t c;
};

template <typename VertexFormat>
struct Mesh
{
	std::vector<VertexFormat> vertices;
	std::vector<MeshTriangle> triangles;
};


void MeshCreateCylinder(Mesh<VertexFormatBasic>& mesh, int numBoundaryVertices);
void MeshCreateGrid(Mesh<VertexFormatBasic>& mesh, int width, int height);