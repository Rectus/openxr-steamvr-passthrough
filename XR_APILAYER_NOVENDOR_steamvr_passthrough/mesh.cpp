
#include "pch.h"
#include "mesh.h"


#define BORDER_SIZE 3

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
			// Mark border vertices
			float z = 0.0f;
				
			if (x < BORDER_SIZE || x >= (width - BORDER_SIZE) || y < BORDER_SIZE || y >= (height - BORDER_SIZE))
			{
				float size = (float)BORDER_SIZE;

				float low = fmaxf((size - x) / size, (size - y) / size);
				float high = -fmin(0.0f, fminf((width - size - x - 1) / size, (height - size - y - 1) / size));

				z = fmaxf(low, high);
			}

			//float z = (x < BORDER_SIZE || x >= (width - BORDER_SIZE) || y < BORDER_SIZE || y >= (height - BORDER_SIZE)) ? 1.0f : 0.0f;

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
			// Mark border vertices
			float z = 0.0f;

			if (x < BORDER_SIZE || x >= (width - BORDER_SIZE) || y < BORDER_SIZE || y >= (height - BORDER_SIZE))
			{
				float size = (float)BORDER_SIZE;

				float low = fmaxf((size - x) / size, (size - y) / size);
				float high = -fmin(0.0f, fminf((width - size - x - 1) / size, (height - size - y - 1) / size));

				z = fmaxf(low, high);
			}

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
				mesh.vertices.emplace_back(x * stepX + 0.5f * stepX, y * stepY, z);

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

void MeshCreateRenderModel(Mesh<VertexFormatBasic>& mesh, vr::RenderModel_t* renderModel)
{
	mesh.vertices.resize(renderModel->unVertexCount);
	mesh.triangles.resize(renderModel->unTriangleCount);

	for (unsigned int i = 0; i < renderModel->unVertexCount; i++)
	{
		mesh.vertices[i].position[0] = renderModel->rVertexData[i].vPosition.v[0];
		mesh.vertices[i].position[1] = renderModel->rVertexData[i].vPosition.v[1];
		mesh.vertices[i].position[2] = renderModel->rVertexData[i].vPosition.v[2];
	}

	for (unsigned int i = 0; i < renderModel->unTriangleCount; i++)
	{
		mesh.triangles[i].a = renderModel->rIndexData[i * 3];
		mesh.triangles[i].b = renderModel->rIndexData[i * 3 + 1];
		mesh.triangles[i].c = renderModel->rIndexData[i * 3 + 2];
	}
}