#include "RmlMesh.h"
#include "RmlUi/Core/Vertex.h"
#include "RmlUi/Core/Span.h"

void FRmlMesh::Setup(Rml::Span<const Rml::Vertex> InVertices, Rml::Span<const int> InIndices)
{
	const int32 NumVerts = (int32)InVertices.size();
	const int32 NumIdx   = (int32)InIndices.size();

	// copy vertices
	Vertices.SetNumUninitialized(NumVerts);
	for (int32 i = 0; i < NumVerts; ++i)
	{
		const Rml::Vertex& V = InVertices[i];
		Vertices[i] = FVertexData(
			FVector2f(V.position.x, V.position.y),
			FVector2f(V.tex_coord.x, V.tex_coord.y),
			FColor(V.colour.red, V.colour.green, V.colour.blue, V.colour.alpha));
	}

	// copy indices (uint16 limits meshes to 65535 vertices)
	Indices.SetNumUninitialized(NumIdx);
	for (int32 i = 0; i < NumIdx; ++i)
	{
		checkf(InIndices[i] >= 0 && InIndices[i] <= MAX_uint16,
			TEXT("RmlUI index %d out of uint16 range (value: %d)"), i, InIndices[i]);
		Indices[i] = (uint16)InIndices[i];
	}

	NumVertices  = NumVerts;
	NumTriangles = NumIdx / 3;
}

FVertexDeclarationRHIRef FRmlMesh::GetMeshDeclaration()
{
	static FVertexDeclarationRHIRef VertexDeclarationRHI;
	if (!VertexDeclarationRHI.IsValid())
	{
		FVertexDeclarationElementList Elements;
		uint16 Stride = sizeof(FVertexData);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FVertexData, Position), VET_Float2, 0, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FVertexData, UV),       VET_Float2, 1, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FVertexData, Color),    VET_UByte4N, 2, Stride));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	return VertexDeclarationRHI;
}
