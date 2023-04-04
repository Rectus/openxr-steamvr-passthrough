
#include "pch.h"
#include "mesh.h"


// Generate a cylinder with radius and height 1.
void MeshCreateCylinder(Mesh<VertexFormatBasic>& mesh, int numBoundaryVertices)
{
	uint32_t numVertices = numBoundaryVertices * 2 + 2;

	mesh.vertices.resize(0);
	mesh.vertices.reserve(numVertices);
	mesh.triangles.resize(0);
	mesh.triangles.reserve(numBoundaryVertices * 4);

	float radianStep = -2.0f * MATH_PI / (float)numBoundaryVertices;

	mesh.vertices.emplace_back(0.0f, 1.0f, 0.0f);

	uint32_t index = 1;

	for (int i = 0; i < numBoundaryVertices; i++)
	{
		int32_t nextSliceIndex = (i == numBoundaryVertices - 1) ? 1 : index + 2;
		
		mesh.vertices.emplace_back(cosf(radianStep * i), 1.0f, sinf(radianStep * i));
		mesh.vertices.emplace_back(cosf(radianStep * i), 0.0f, sinf(radianStep * i));

		mesh.triangles.emplace_back(0, index, nextSliceIndex);
		mesh.triangles.emplace_back(index, index + 1, nextSliceIndex);
		mesh.triangles.emplace_back(index + 1, nextSliceIndex + 1, nextSliceIndex);
		mesh.triangles.emplace_back(index + 1, numVertices - 1, nextSliceIndex + 1);

		index += 2;
	}

	mesh.vertices.emplace_back(0.0f, 0.0f, 0.0f);

}


void MeshCreateGrid(Mesh<VertexFormatBasic>& mesh, int width, int height)
{
	mesh.vertices.resize(0);
	mesh.vertices.reserve((width + 1) * (height + 1));
	mesh.triangles.resize(0);
	mesh.triangles.reserve(width * height * 2);

	float stepX = 1.0f / (float)width;
	float stepY = 1.0f / (float)height;

	uint32_t index = 0;

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			// Mark border vertices with z = 1
			float z = (x == 0 || x == (width - 1) || y == 0 || y == (height - 1)) ? 1.0f : 0.0f;

			mesh.vertices.emplace_back(x * stepX, y * stepY, z);

			if (x < width - 1 && y < height -1 )
			{
				mesh.triangles.emplace_back(index, index + 1, index + width + 1);
				mesh.triangles.emplace_back(index, index + width + 1, index + width);
			}

			index++;
		}
	}
}


void MeshCreateHexGrid(Mesh<VertexFormatBasic>& mesh, int width, int height)
{
	mesh.vertices.resize(0);
	mesh.vertices.reserve((width + 1) * (height + 1));
	mesh.triangles.resize(0);
	mesh.triangles.reserve(width * height * 2);

	float stepX = 1.0f / (float)width;
	float stepY = 1.0f / (float)height;

	uint32_t index = 0;

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			// Mark border vertices with z = 1
			float z = (x == 0 || x == (width - 1) || y == 0 || y == (height - 1)) ? 1.0f : 0.0f;

			if (y % 2 == 0)
			{
				mesh.vertices.emplace_back(x * stepX, y * stepY, z);

				if (x < width - 1 && y < height - 1)
				{
					mesh.triangles.emplace_back(index, index + 1, index + width);
					mesh.triangles.emplace_back(index + 1, index + width + 1, index + width);
				}
			}
			else
			{
				mesh.vertices.emplace_back(x * stepX + 0.5 * stepX, y * stepY, z);

				if (x < width - 1 && y < height - 1)
				{
					mesh.triangles.emplace_back(index, index + 1, index + width + 1);
					mesh.triangles.emplace_back(index, index + width + 1, index + width);
				}
			}

			index++;
		}
	}
}