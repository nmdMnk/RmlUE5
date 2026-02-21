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
}

void FRmlMesh::BuildMesh()
{
	// Only record counts here — RHI buffers are created lazily in DrawMesh() on the render thread.
	// RHICreateVertexBuffer / RHICreateIndexBuffer require IsInRenderingThread().
	if (Vertices.Num() == 0 || Indices.Num() == 0) return;
	check(Indices.Num() % 3 == 0);
	NumVertices  = Vertices.Num();
	NumTriangles = Indices.Num() / 3;
}

void FRmlMesh::ReleaseMesh()
{
	VertexBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();
}

void FRmlMesh::DrawMesh(FRHICommandList& RHICmdList)
{
	check(IsInRenderingThread());

	if (NumVertices == 0 || NumTriangles == 0) return;

	// Create RHI buffers lazily on the render thread (can't create them in BuildMesh
	// which runs on the game thread via CompileGeometry / SRmlWidget::OnPaint).
	if (!VertexBufferRHI.IsValid())
	{
		const int32 VerticesBufferSize = sizeof(FVertexData) * Vertices.Num();
		FRHIResourceCreateInfo VtxInfo(TEXT("RmlVertexBuffer"), &Vertices);
		VertexBufferRHI = RHICreateVertexBuffer(VerticesBufferSize, BUF_Static, VtxInfo);

		const int32 IndexBufferSize = sizeof(uint16) * Indices.Num();
		FRHIResourceCreateInfo IdxInfo(TEXT("RmlIndexBuffer"), &Indices);
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), IndexBufferSize, BUF_Static, IdxInfo);
	}

	RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, 0, 0, NumVertices, 0, NumTriangles, 1);
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
